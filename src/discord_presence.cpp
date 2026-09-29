#include "discord_presence.hpp"
#include "discord_ipc.hpp"
#include "discord_rules.hpp"
#include "iat_hook.hpp"
#include "log.hpp"

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

		// Set before the thread starts; read only after.
		game which = game::xml2;
		std::string client_id;
		display show;
		extras presence_extras;
		bool game_readable = false; // every guard matched: the presence reads the game's state
		DWORD pid = 0;

		HANDLE stop_event = nullptr;
		HANDLE thread = nullptr;

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

		// The port's scripts, in the game folder or in a mod's.
		bool has_x1_scripts()
		{
			std::error_code ignored;
			const auto& dir = logger::module_dir();
			if (std::filesystem::is_directory(dir / L"Scripts" / L"x1", ignored))
			{
				return true;
			}
			for (std::filesystem::directory_iterator mod(dir / L"mods", ignored), end; !ignored && mod != end; mod.increment(ignored))
			{
				if (mod->is_directory(ignored) && std::filesystem::is_directory(mod->path() / L"Scripts" / L"x1", ignored))
				{
					return true;
				}
			}
			return false;
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

		DWORD WINAPI run(void*)
		{
			discord_ipc::connection pipe;
			gate pacing;
			settle settled;
			game_memory memory;
			game_reader<game_memory> reader(memory);
			std::optional<activity> wanted; // settled from the game's state
			std::optional<activity> sent;   // on this connection
			std::uint64_t next_attempt = 0;
			std::string last_problem;
			std::string last_refusal;
			unsigned nonce = 0;
			unsigned changes = 0;
			bool read_failed = false;

			if (WaitForSingleObject(stop_event, static_cast<DWORD>(first_connect_ms)) != WAIT_OBJECT_0)
			{
				for (;;)
				{
					const std::uint64_t now = GetTickCount64();

					// What the game is doing.
					if (game_readable)
					{
						std::optional<snapshot> state;
						try
						{
							state = reader.read();
						}
						catch (...)
						{
							if (!read_failed)
							{
								logger::write("discord: ERROR: reading the game's state failed - the presence stays as it is");
								read_failed = true;
							}
						}
						if (state)
						{
							if (auto shown = settled.seen(build(*state, show)))
							{
								wanted = std::move(shown);
							}
						}
					}
					const activity target = game_readable ? wanted.value_or(in_the_menus()) : activity{};

					// Discord.
					if (!pipe.is_open() && now >= next_attempt)
					{
						std::string name;
						std::string problem;
						if (pipe.open(client_id, name, problem))
						{
							logger::write("discord: connected as %.*s (%s)", static_cast<int>(title_of(which).size()), title_of(which).data(), name.c_str());
							last_problem.clear();
							last_refusal.clear();
							sent.reset();
							pacing.reset();
						}
						else
						{
							next_attempt = now + retry_ms;
							if (problem != last_problem)
							{
								logger::write("discord: %s - looking again every %u s", problem.c_str(), static_cast<unsigned>(retry_ms / 1000));
								last_problem = problem;
							}
						}
					}
					if (pipe.is_open())
					{
						bool said_why = false;
						std::vector<message> incoming;
						if (pipe.receive(incoming))
						{
							for (const auto& m : incoming)
							{
								if (m.op == op_ping)
								{
									pipe.send(op_pong, m.json);
								}
								else if (m.op == op_close)
								{
									logger::write("discord: Discord closed the connection (%s: %s) - looking again every %u s", json_value(m.json, "code").value_or("?").c_str(),
									              json_value(m.json, "message").value_or("no reason given").c_str(), static_cast<unsigned>(retry_ms / 1000));
									pipe.close();
									said_why = true;
									break;
								}
								else if (m.op == op_frame && json_value(m.json, "evt") == "ERROR")
								{
									const auto why = json_value(m.json, "message").value_or("no message");
									if (why != last_refusal)
									{
										logger::write("discord: Discord refused the presence (%s)", why.c_str());
										last_refusal = why;
									}
								}
							}
						}
						if (pipe.is_open() && (!sent || *sent != target) && pacing.may_send(now))
						{
							if (pipe.send(op_frame, set_activity_json(pid, target, presence_extras, ++nonce)))
							{
								pacing.sent(now);
								sent = target;
								if (changes < changes_to_log)
								{
									logger::write("discord: presence -> %s", describe(target).c_str());
								}
								else if (changes == changes_to_log)
								{
									logger::write("discord: (further presence changes aren't logged)");
								}
								++changes;
							}
						}
						if (!pipe.is_open())
						{
							if (!said_why)
							{
								logger::write("discord: the connection to Discord is gone - looking again every %u s", static_cast<unsigned>(retry_ms / 1000));
							}
							last_problem = "gone";
							next_attempt = now + retry_ms;
						}
					}

					if (WaitForSingleObject(stop_event, static_cast<DWORD>(sample_ms)) == WAIT_OBJECT_0)
					{
						break;
					}
				}
			}

			// The game is quitting: clear the activity rather than leave it to Discord noticing the pipe close.
			if (pipe.is_open())
			{
				if (pipe.send(op_frame, clear_activity_json(pid, ++nonce), 300))
				{
					pipe.wait(300);
				}
				pipe.close();
				logger::write("discord: presence cleared (the game is quitting)");
			}
			return 0;
		}

		// ---- Quitting -----------------------------------------------------------------------------

		using exit_process_t = void(WINAPI*)(UINT);
		exit_process_t real_exit_process = nullptr;

		// The game's own ExitProcess (its CRT's exit): the presence's thread still runs here, so it
		// clears the activity and closes the pipe before the process goes.
		void WINAPI game_exit_process(const UINT code)
		{
			if (stop_event && thread)
			{
				SetEvent(stop_event);
				WaitForSingleObject(thread, 1500);
			}
			real_exit_process(code);
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
		for (const auto& [key, field] : {std::pair{L"LargeImage", &presence_extras.large_image}, std::pair{L"SmallImage", &presence_extras.small_image}})
		{
			const std::string asset(value_text(ini_text(L"Discord", key).value_or("")));
			if (asset.empty())
			{
				continue;
			}
			if (valid_asset(asset))
			{
				*field = asset;
			}
			else
			{
				logger::write("discord: [Discord] %ls isn't an asset key (no spaces or quotes, at most 256 characters) - ignored", key);
			}
		}
		pid = GetCurrentProcessId();
		presence_extras.start = unix_now(); // the elapsed time counts from the game's start, not each zone's
		presence_extras.large_text = std::string(title_of(which));
		char party[32];
		std::snprintf(party, sizeof(party), "xml2fix-%08lx%08lx", static_cast<unsigned long>(pid), static_cast<unsigned long>(GetTickCount()));
		presence_extras.party_id = party;

		if (mismatch)
		{
			logger::write("discord: XMen2.exe doesn't have the expected code at 0x%08lX (%s; not the retail build?) - the presence shows the game's name only", mismatch->va,
			              mismatch->what);
		}

		stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		thread = stop_event ? CreateThread(nullptr, 0, &run, nullptr, 0, nullptr) : nullptr;
		if (!thread)
		{
			logger::write("discord: ERROR: couldn't start the presence's thread (error %lu) - no presence", GetLastError());
			return;
		}
		real_exit_process = reinterpret_cast<exit_process_t>(iat_hook::hook(game_module, "KERNEL32.dll", "ExitProcess", 0, reinterpret_cast<void*>(&game_exit_process)));
		logger::write("discord: presence on as %.*s (application %s; %s)%s%s%s", static_cast<int>(title_of(which).size()), title_of(which).data(), client_id.c_str(),
		              choice.why.c_str(), show.zone ? "" : "; ShowZone=0", show.party ? "" : "; ShowParty=0",
		              real_exit_process ? "" : "; the game doesn't import ExitProcess - Discord clears it when the game's pipe closes");
	}
}
