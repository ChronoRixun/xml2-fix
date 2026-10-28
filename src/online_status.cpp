#include "online_status.hpp"

#include "log.hpp"

#include <Windows.h>

#include <cstring>
#include <mutex>

namespace online_status
{
	namespace
	{
		namespace core = online_status_core;
		std::mutex lock;
		bool started = false;
		core::status last;
		std::wstring file_path;
		// Roster-only changes coalesce to one write per second (docs/online-party.md section 9).
		bool party_dirty = false;
		DWORD party_written = 0;
		DWORD (WINAPI *clock)() = &GetTickCount;
		core::utc process_start()
		{
			FILETIME created{}, exited{}, kernel{}, user{};
			SYSTEMTIME t{};
			if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) || !FileTimeToSystemTime(&created, &t)) return {};
			return {int(t.wYear), int(t.wMonth), int(t.wDay), int(t.wHour), int(t.wMinute), int(t.wSecond)};
		}

		std::wstring status_path()
		{
			wchar_t exe[MAX_PATH]{};
			const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
			if (n == 0 || n >= MAX_PATH) return {};
			std::wstring path(exe, n);
			path.resize(path.find_last_of(L"\\/") + 1);
			return path + L"online-status.json";
		}
		core::utc now_utc()
		{
			SYSTEMTIME t{};
			GetSystemTime(&t);
			return {int(t.wYear), int(t.wMonth), int(t.wDay), int(t.wHour), int(t.wMinute), int(t.wSecond)};
		}
		// Caller holds `lock`.
		void flush()
		{
			party_dirty = false;
			party_written = clock();
			if (file_path.empty()) return;
			if (!write_atomic(file_path, core::json(last, now_utc())))
				logger::write_once("online-status-write", "online: online-status.json could not be written (error %lu)", GetLastError());
		}
	}

	bool write_atomic(const std::wstring& path, const std::string& text)
	{
		const std::wstring temp = path + L".tmp";
		const HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) return false;
		DWORD written = 0;
		if (!WriteFile(h, text.data(), DWORD(text.size()), &written, nullptr) || written != text.size() || !FlushFileBuffers(h))
		{
			CloseHandle(h);
			DeleteFileW(temp.c_str());
			return false;
		}
		// Rename over the old file with POSIX semantics (Windows 10 1709+): works while a launcher holds the old file
		// open for reading with delete sharing. FILE_RENAME_INFORMATION_EX: Flags, RootDirectory, FileNameLength, FileName.
		bool moved = false;
		{
			const std::size_t bytes = path.size() * sizeof(wchar_t);
			std::string buffer(sizeof(DWORD) + sizeof(HANDLE) + sizeof(DWORD) + bytes + sizeof(wchar_t), '\0');
			auto* raw = buffer.data();
			const DWORD flags = 0x1 /* REPLACE_IF_EXISTS */ | 0x2 /* POSIX_SEMANTICS */, length = DWORD(bytes);
			std::memcpy(raw, &flags, sizeof(flags));
			std::memcpy(raw + sizeof(DWORD) + sizeof(HANDLE), &length, sizeof(length));
			std::memcpy(raw + sizeof(DWORD) + sizeof(HANDLE) + sizeof(DWORD), path.c_str(), bytes);
			moved = SetFileInformationByHandle(h, FILE_INFO_BY_HANDLE_CLASS(22) /* FileRenameInfoEx */, raw, DWORD(buffer.size())) != 0;
		}
		CloseHandle(h);
		// Older Windows: a plain replace, retried briefly while a reader holds the old file.
		for (int attempt = 0; attempt < 5 && !moved; ++attempt)
		{
			moved = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
			if (!moved) Sleep(10);
		}
		if (!moved) DeleteFileW(temp.c_str());
		return moved;
	}

	bool remove_launch_keys(const std::wstring& ini_path)
	{
		const BOOL host = WritePrivateProfileStringW(L"Online", L"Host", nullptr, ini_path.c_str());
		const BOOL code = WritePrivateProfileStringW(L"Online", L"JoinCode", nullptr, ini_path.c_str());
		// Public / RoomName go with Host=broker (contract v1.3): one start's choice, never the next one's.
		const BOOL listed = WritePrivateProfileStringW(L"Online", L"Public", nullptr, ini_path.c_str());
		const BOOL name = WritePrivateProfileStringW(L"Online", L"RoomName", nullptr, ini_path.c_str());
		return host && code && listed && name;
	}
	std::wstring game_ini() { return (logger::module_dir() / L"xml2-fix.ini").wstring(); }

	void begin(core::role who)
	{
		std::lock_guard g(lock);
		started = true;
		file_path = status_path();
		last = {};
		last.who = who;
		last.process.pid = GetCurrentProcessId();
		last.process.started = process_start();
		party_dirty = false;
	}
	bool active()
	{
		std::lock_guard g(lock);
		return started;
	}
	core::error last_error()
	{
		std::lock_guard g(lock);
		return last.why;
	}
	core::state current()
	{
		std::lock_guard g(lock);
		return last.now;
	}

	void set(core::state now, const std::string& code, int players, core::error why, const std::string& message)
	{
		std::lock_guard g(lock);
		if (!started) return;
		// An error is final for this run: later broker or connection events do not hide it from the launcher
		// (only the process exit writes `ended`).
		// The first error is the run's answer: later events (a room closing after a refusal, the broker going away)
		// do not replace it. Only the process exit adds `ended`.
		if (last.now == core::state::ended) return;
		if (last.now == core::state::error)
		{
			// v1 keeps its first error for the run; a later in-game attempt is the party block's news only.
			if (!last.party.known) return;
			auto attempt = last.party;
			attempt.now = now;
			attempt.why = now == core::state::error ? (why == core::error::none ? core::error::internal : why) : core::error::none;
			if (attempt == last.party) return;
			++attempt.generation;
			last.party = attempt;
			flush();
			return;
		}
		core::status next = last;
		if (next.party.known)
		{
			next.party.now = now;
			next.party.why = now == core::state::error ? (why == core::error::none ? core::error::internal : why) : core::error::none;
		}
		next.now = now;
		next.code = code;
		next.players = players;
		next.why = now == core::state::error ? (why == core::error::none ? core::error::internal : why) : core::error::none;
		next.message = message;
		static bool written = false;
		if (written && next.now == last.now && next.code == last.code && next.players == last.players && next.why == last.why && next.message == last.message &&
		    next.party == last.party)
			return;
		if (!(next.party == last.party)) ++next.party.generation;
		written = true;
		last = next;
		flush();
		logger::write("online: status %s%s%s (players %d)", core::name(last.now), last.why == core::error::none ? "" : " ", last.why == core::error::none ? "" : core::name(last.why), last.players);
	}
	void error(core::error why, const std::string& message)
	{
		std::string code;
		int players = 0;
		{
			std::lock_guard g(lock);
			code = last.code;
			players = last.players;
		}
		set(core::state::error, code, players, why, message);
	}
	void players(int count)
	{
		core::state now;
		std::string code;
		{
			std::lock_guard g(lock);
			if (!started || last.players == count || last.now == core::state::error || last.now == core::state::ended) return;
			now = last.now;
			code = last.code;
		}
		set(now, code, count);
	}

	void room(bool public_room, const std::string& name)
	{
		std::lock_guard g(lock);
		if (!started || (last.public_room == public_room && last.room_name == name)) return;
		last.public_room = public_room;
		last.room_name = name;
		// An error or ended file keeps its last word; the listing is only news while the run goes on.
		if (last.now != core::state::error && last.now != core::state::ended) flush();
	}

	void end()
	{
		std::unique_lock g(lock, std::try_to_lock);
		if (!g.owns_lock() || !started || last.now == core::state::ended) return;
		// The process is exiting. A run that ended in an error keeps its id and message beside `ended`.
		if (last.now != core::state::error)
		{
			last.why = core::error::none;
			last.message = "the game closed";
		}
		last.now = core::state::ended;
		if (last.party.known)
		{
			last.party.now = core::state::ended;
			last.party.phase = "left";
			last.party.roster.clear();
			++last.party.generation;
		}
		flush();
	}

	void attempt()
	{
		std::lock_guard g(lock);
		if (!started || last.now == core::state::ended) return;
		last.party.known = true;
		last.party.now = core::state::connecting;
		last.party.why = core::error::none;
		last.party.phase = "connecting";
		last.party.table = -1;
		last.party.roster.clear();
		++last.party.generation;
		flush(); // rare (one per Host / Join / Retry); the set() that follows writes only what then differs
		logger::write("online: status attempt %u (party block)", unsigned(last.party.generation));
	}

	void party(const std::string& phase, int table, const std::vector<core::roster_row>& roster)
	{
		std::lock_guard g(lock);
		if (!started || !last.party.known || last.now == core::state::ended) return;
		if (last.party.phase == phase && last.party.table == table && last.party.roster == roster) return;
		last.party.phase = phase;
		last.party.table = table;
		last.party.roster = roster;
		++last.party.generation;
		party_dirty = true;
		if (clock() - party_written >= 1000) flush();
	}

	void tick()
	{
		std::unique_lock g(lock, std::try_to_lock);
		if (!g.owns_lock() || !started || !party_dirty || clock() - party_written < 1000) return;
		flush();
	}

	void set_clock_for_test(DWORD (WINAPI *c)()) { std::lock_guard g(lock); clock = c ? c : &GetTickCount; }
	void restart_for_test() { std::lock_guard g(lock); started = false; last = {}; file_path.clear(); party_dirty = false; party_written = 0; }
	void path_for_test(const std::wstring& path) { std::lock_guard g(lock); file_path = path; }
	core::status snapshot() { std::lock_guard g(lock); return last; }
}
