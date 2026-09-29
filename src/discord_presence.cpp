#include "discord_presence.hpp"
#include "discord_ipc.hpp"
#include "discord_rules.hpp"
#include "iat_hook.hpp"
#include "log.hpp"
#include "mod_order.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace discord_presence
{
	namespace
	{
		using namespace discord_rules;

		// Set before the thread starts; read only after (the thread itself adds the party id).
		game which = game::xml2;
		std::string client_id;
		display show;
		extras presence_extras;
		bool game_readable = false; // every guard matched: the presence reads the game's state
		DWORD pid = 0;              // for the Discord client on this PC only (args.pid), never in the activity

		HANDLE stop_event = nullptr; // the game is quitting
		HANDLE done_event = nullptr; // the presence's thread has cleared the activity (or has nothing to clear)
		HANDLE thread = nullptr;
		DWORD thread_id = 0;

		// ---- xml2-fix.ini -------------------------------------------------------------------------

		std::optional<std::string> ini_text(const wchar_t* section, const wchar_t* key)
		{
			const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
			constexpr wchar_t absent[] = L"\x7f";
			wchar_t value[512]{};
			GetPrivateProfileStringW(section, key, absent, value, static_cast<DWORD>(std::size(value)), ini.c_str());
			if (std::wcscmp(value, absent) == 0)
			{
				return std::nullopt;
			}
			std::string narrow;
			for (const wchar_t* p = value; *p; ++p)
			{
				narrow += *p < 128 ? static_cast<char>(*p) : '?';
			}
			return narrow;
		}

		std::optional<std::string_view> view(const std::optional<std::string>& text)
		{
			return text ? std::optional<std::string_view>(*text) : std::nullopt;
		}

		// The port's scripts, in the game folder or in a mod that loads: mods\load-order.txt decides, as it
		// does for the mod loader (a "-Name" mod, or a leftover .staging folder, doesn't count).
		bool has_x1_scripts()
		{
			std::error_code ignored;
			const auto& dir = logger::module_dir();
			if (std::filesystem::is_directory(dir / L"Scripts" / L"x1", ignored))
			{
				return true;
			}
			try
			{
				return mod_order::enabled_mod_has_files(dir / L"mods", std::filesystem::path(L"Scripts") / L"x1");
			}
			catch (...)
			{
				return false; // out of memory: the other clues decide
			}
		}

		std::int64_t unix_now()
		{
			FILETIME now{};
			GetSystemTimeAsFileTime(&now);
			ULARGE_INTEGER ticks{};
			ticks.LowPart = now.dwLowDateTime;
			ticks.HighPart = now.dwHighDateTime;
			return static_cast<std::int64_t>((ticks.QuadPart - 116444736000000000ULL) / 10000000ULL);
		}

		// ---- The game's memory, from this thread --------------------------------------------------

		// No C++ objects here: the copy is guarded.
		bool guarded_copy(const DWORD va, void* out, const std::size_t size)
		{
			__try
			{
				std::memcpy(out, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(va)), size);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool bytes_match(const guard& g)
		{
			__try
			{
				return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(g.va)), g.hex);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		const guard* first_mismatch_guarded()
		{
			for (const auto& g : guards)
			{
				if (!bytes_match(g))
				{
					return &g;
				}
			}
			for (const auto& g : frame_rate_rules::screen_guards)
			{
				if (!bytes_match(g))
				{
					return &g;
				}
			}
			return nullptr;
		}

		// The game's memory as game_reader reads it: only committed, readable pages without a guard page
		// among them (touching another thread's stack guard page would take it away), and every copy
		// SEH-guarded, as the game frees and moves things while this thread reads.
		struct game_memory
		{
			bool read(const DWORD va, void* out, const std::size_t size)
			{
				if (va < 0x10000 || size == 0 || va + size < va)
				{
					return false;
				}
				for (DWORD at = va; at < va + size;)
				{
					MEMORY_BASIC_INFORMATION region{};
					if (!VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), &region, sizeof(region)) || region.State != MEM_COMMIT ||
					    (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
					    !(region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
					{
						return false;
					}
					const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
					if (end <= at)
					{
						return false;
					}
					at = end > 0xffffffffu ? 0xffffffffu : static_cast<DWORD>(end);
				}
				return guarded_copy(va, out, size);
			}
		};

		// ---- The thread ---------------------------------------------------------------------------

		constexpr unsigned changes_to_log = 100;
		constexpr DWORD quit_wait_ms = 1500; // the most quitting waits for the presence to be cleared

		// Everything the presence's thread does. No C++ exception leaves it: a failure while reading the
		// game or talking to Discord (out of memory in the 32-bit game, say) drops that one round - the
		// connection is closed and Discord looked for again later - instead of taking XMen2.exe down.
		class presence
		{
		public:
			void run()
			{
				pipe_.cancel_on(stop_event);
				try
				{
					// The online party's id: random, made here - off the loader lock, before anything is sent.
					presence_extras.party_id = random_party_id();
				}
				catch (...)
				{
					// None: Discord shows no "(2 of 4)" online, and nothing else changes.
				}

				if (WaitForSingleObject(stop_event, static_cast<DWORD>(first_connect_ms)) != WAIT_OBJECT_0)
				{
					for (;;)
					{
						const std::uint64_t now = GetTickCount64();
						read_game();
						try
						{
							talk(now);
						}
						catch (...)
						{
							pipe_.close();
							sent_.reset();
							next_attempt_ = now + retry_ms;
							if (!talk_failed_)
							{
								logger::write("discord: ERROR: talking to Discord failed (out of memory?) - connection closed, looking again every 20 s");
								talk_failed_ = true;
							}
						}
						if (WaitForSingleObject(stop_event, static_cast<DWORD>(sample_ms)) == WAIT_OBJECT_0)
						{
							break;
						}
					}
				}
				clear();
			}

		private:
			discord_ipc::connection pipe_;
			gate pacing_;
			settle settled_;
			party_hold hold_;
			game_memory memory_;
			game_reader<game_memory> reader_{memory_};
			std::optional<activity> wanted_; // settled from the game's state
			std::optional<activity> sent_;   // on this connection
			std::uint64_t next_attempt_ = 0;
			std::string last_problem_;
			std::string last_refusal_;
			unsigned nonce_ = 0;
			unsigned changes_ = 0;
			bool read_failed_ = false;
			bool talk_failed_ = false;

			// What the game is doing.
			void read_game()
			{
				if (!game_readable)
				{
					return;
				}
				try
				{
					if (auto state = reader_.read())
					{
						hold_.apply(*state); // the team menu's picks show once it's accepted
						if (auto shown = settled_.seen(build(*state, show)))
						{
							wanted_ = std::move(shown);
						}
					}
				}
				catch (...)
				{
					if (!read_failed_)
					{
						logger::write("discord: ERROR: reading the game's state failed - the presence stays as it is");
						read_failed_ = true;
					}
				}
			}

			// Discord: connect when it's time, handle what it sent, send the activity when it changed.
			void talk(const std::uint64_t now)
			{
				const activity target = game_readable ? wanted_.value_or(in_the_menus()) : activity{};
				if (!pipe_.is_open() && now >= next_attempt_)
				{
					std::string name;
					std::string problem;
					if (pipe_.open(client_id, name, problem))
					{
						logger::write("discord: connected as %.*s (%s)", static_cast<int>(title_of(which).size()), title_of(which).data(), name.c_str());
						last_problem_.clear();
						last_refusal_.clear();
						sent_.reset();
						pacing_.reset();
					}
					else
					{
						next_attempt_ = now + retry_ms;
						if (problem != last_problem_)
						{
							logger::write("discord: %s - looking again every %u s", problem.c_str(), static_cast<unsigned>(retry_ms / 1000));
							last_problem_ = problem;
						}
					}
				}
				if (!pipe_.is_open())
				{
					return;
				}
				bool said_why = false;
				std::vector<message> incoming;
				if (pipe_.receive(incoming))
				{
					for (const auto& m : incoming)
					{
						if (m.op == op_ping)
						{
							pipe_.send(op_pong, m.json);
						}
						else if (m.op == op_close)
						{
							logger::write("discord: Discord closed the connection (%s: %s) - looking again every %u s", json_value(m.json, "code").value_or("?").c_str(),
							              json_value(m.json, "message").value_or("no reason given").c_str(), static_cast<unsigned>(retry_ms / 1000));
							pipe_.close();
							said_why = true;
							break;
						}
						else if (m.op == op_frame && json_value(m.json, "evt") == "ERROR")
						{
							const auto why = json_value(m.json, "message").value_or("no message");
							if (why != last_refusal_)
							{
								logger::write("discord: Discord refused the presence (%s)", why.c_str());
								last_refusal_ = why;
							}
						}
					}
				}
				if (pipe_.is_open() && (!sent_ || *sent_ != target) && pacing_.may_send(now))
				{
					if (pipe_.send(op_frame, set_activity_json(pid, target, presence_extras, ++nonce_)))
					{
						pacing_.sent(now);
						sent_ = target;
						if (changes_ < changes_to_log)
						{
							logger::write("discord: presence -> %s", describe(target).c_str());
						}
						else if (changes_ == changes_to_log)
						{
							logger::write("discord: (further presence changes aren't logged)");
						}
						++changes_;
					}
				}
				if (!pipe_.is_open())
				{
					if (!said_why)
					{
						if (!pipe_.problem().empty())
						{
							logger::write("discord: the pipe %s - closed, looking again every %u s", pipe_.problem().c_str(), static_cast<unsigned>(retry_ms / 1000));
						}
						else
						{
							logger::write("discord: the connection to Discord is gone - looking again every %u s", static_cast<unsigned>(retry_ms / 1000));
						}
					}
					last_problem_ = "gone";
					next_attempt_ = now + retry_ms;
				}
			}

			// The game is quitting: clear the activity rather than leave it to Discord noticing the pipe
			// close - quickly, as the game waits for it (at most quit_wait_ms).
			void clear()
			{
				try
				{
					pipe_.cancel_on(nullptr); // the stop event is set: waiting for Discord's answer is wanted now
					if (pipe_.is_open())
					{
						if (pipe_.send(op_frame, clear_activity_json(pid, ++nonce_), 300))
						{
							pipe_.wait(300);
						}
						pipe_.close();
						logger::write("discord: presence cleared (the game is quitting)");
					}
				}
				catch (...)
				{
					pipe_.close(); // Discord clears it when the pipe closes
				}
			}
		};

		DWORD WINAPI run(void*)
		{
			try
			{
				presence().run();
			}
			catch (...)
			{
				// Only a presence that couldn't be made (out of memory); its pipe, if any, is closed.
			}
			SetEvent(done_event);
			return 0;
		}

		// ---- Quitting -----------------------------------------------------------------------------

		using exit_process_t = void(WINAPI*)(UINT);
		exit_process_t real_game_exit = nullptr; // XMen2.exe's own import of ExitProcess: its CRT's abort path
		exit_process_t real_crt_exit = nullptr;  // msvcr71.dll's: exit() and _exit(), a normal quit (WinMain returns)

		// Stops the presence's thread and waits - at most quit_wait_ms - for it to clear the activity and
		// close the pipe. Only an event is waited on, not the thread's end, so this holds even when the
		// caller has the loader lock (a thread's end needs it); a second call finds the event set.
		void stop_presence()
		{
			if (!stop_event || !done_event || GetCurrentThreadId() == thread_id)
			{
				return;
			}
			SetEvent(stop_event);
			WaitForSingleObject(done_event, quit_wait_ms);
		}

		void WINAPI game_exit_process(const UINT code)
		{
			stop_presence();
			real_game_exit(code);
		}

		void WINAPI crt_exit_process(const UINT code)
		{
			stop_presence();
			real_crt_exit(code);
		}
	}

	void install(const HMODULE game_module)
	{
		if (!parse_switch(view(ini_text(L"Discord", L"Enabled")), true))
		{
			logger::write("discord: off ([Discord] Enabled in xml2-fix.ini)");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game_module) != limits_rules::image_base)
		{
			logger::write("discord: XMen2.exe isn't loaded at 0x400000 (not the game?) - no presence");
			return;
		}
		wchar_t exe[MAX_PATH]{};
		GetModuleFileNameW(game_module, exe, MAX_PATH);
		const auto exe_name = std::filesystem::path(exe).filename().wstring();
		const guard* mismatch = first_mismatch_guarded();
		if (mismatch && _wcsicmp(exe_name.c_str(), L"XMen2.exe") != 0)
		{
			logger::write("discord: this isn't XMen2.exe (%ls) - no presence", exe_name.c_str());
			return;
		}
		game_readable = mismatch == nullptr;

		game_clues clues;
		clues.override_value = ini_text(L"Discord", L"Game").value_or("");
		clues.x1_scripts = has_x1_scripts();
		clues.postgame_script = ini_text(L"Game", L"PostgameScript").value_or("");
		clues.save_folder = ini_text(L"Game", L"SaveFolder").value_or("");
		const auto choice = choose_game(clues);
		which = choice.which;
		if (!choice.note.empty())
		{
			logger::write("discord: %s", choice.note.c_str());
		}
		client_id = std::string(client_id_for(which));
		const std::string own_id(value_text(ini_text(L"Discord", L"ClientId").value_or("")));
		if (!own_id.empty())
		{
			if (valid_client_id(own_id))
			{
				client_id = own_id;
			}
			else
			{
				logger::write("discord: [Discord] ClientId=%s isn't an application id (15 to 20 digits) - ignored", own_id.c_str());
			}
		}
		show.zone = parse_switch(view(ini_text(L"Discord", L"ShowZone")), true);
		show.party = parse_switch(view(ini_text(L"Discord", L"ShowParty")), true);
		const auto art = choose_images(view(ini_text(L"Discord", L"LargeImage")), view(ini_text(L"Discord", L"SmallImage")));
		if (!art.note.empty())
		{
			logger::write("discord: %s", art.note.c_str());
		}
		presence_extras.large_image = art.large;
		presence_extras.small_image = art.badge;
		pid = GetCurrentProcessId();
		presence_extras.start = unix_now(); // the elapsed time counts from the game's start, not each zone's
		presence_extras.large_text = std::string(title_of(which));
		// The online party's id is the presence's thread's to make (random, off the loader lock).

		if (mismatch)
		{
			logger::write("discord: XMen2.exe doesn't have the expected code at 0x%08lX (%s; not the retail build?) - the presence shows the game's name only", mismatch->va,
			              mismatch->what);
		}

		stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		thread = stop_event && done_event ? CreateThread(nullptr, 0, &run, nullptr, 0, &thread_id) : nullptr;
		if (!thread)
		{
			logger::write("discord: ERROR: couldn't start the presence's thread (error %lu) - no presence", GetLastError());
			return;
		}
		// Quitting clears the presence first. A normal quit returns from WinMain into msvcr71.dll's exit(),
		// which calls ExitProcess through msvcr71.dll's own import; the game's own import is its CRT's
		// abort path. Both are hooked (by name: no addresses of the game's).
		real_game_exit = reinterpret_cast<exit_process_t>(iat_hook::hook(game_module, "KERNEL32.dll", "ExitProcess", 0, reinterpret_cast<void*>(&game_exit_process)));
		real_crt_exit = reinterpret_cast<exit_process_t>(
			iat_hook::hook(GetModuleHandleW(L"msvcr71.dll"), "KERNEL32.dll", "ExitProcess", 0, reinterpret_cast<void*>(&crt_exit_process)));
		std::string notes;
		if (art.large.empty())
		{
			notes += "; no images (LargeImage=none)";
		}
		else
		{
			if (art.large != large_image_key) notes += "; LargeImage=" + art.large;
			if (art.badge) notes += art.badge->empty() ? "; no badges (SmallImage=none)" : "; SmallImage=" + *art.badge;
		}
		if (!real_crt_exit)
		{
			notes += real_game_exit ? "; msvcr71.dll's ExitProcess isn't hooked - on a normal quit Discord clears it when the game's pipe closes"
			                        : "; ExitProcess isn't hooked - Discord clears it when the game's pipe closes";
		}
		logger::write("discord: presence on as %.*s (application %s; %s)%s%s%s", static_cast<int>(title_of(which).size()), title_of(which).data(), client_id.c_str(),
		              choice.why.c_str(), show.zone ? "" : "; ShowZone=0", show.party ? "" : "; ShowParty=0", notes.c_str());
	}
}
