#include "new_game.hpp"
#include "new_game_plus_rules.hpp"

#include "log.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace new_game
{
	namespace
	{
		constexpr std::uintptr_t image_base = 0x400000;  // XMen2.exe has no relocations
		constexpr std::uint32_t empty_name = 0x681968;    // "" - the game's own "no hero" string
		constexpr std::uint8_t push_imm32 = 0x68;

		struct operand
		{
			std::uintptr_t address;  // the imm32 of a push
			std::uint32_t expected;  // the retail string it points at
		};

		// startFirstMission: two pushes per party slot (research/heroes/roster.md section 3).
		constexpr std::array<std::array<operand, 2>, 4> team_slots{{
			{{{0x4a7b42, 0x682504}, {0x4a7b51, 0x682504}}},  // "magneto"
			{{{0x4a7b7c, 0x682548}, {0x4a7b8b, 0x682548}}},  // "cyclops"
			{{{0x4a7bb6, 0x6824a8}, {0x4a7bc5, 0x6824a8}}},  // "wolverine"
			{{{0x4a7bf0, 0x6824c4}, {0x4a7bff, 0x6824c4}}},  // "storm"
		}};

		// resetgame's default unlocks, in the game's order.
		constexpr std::array<operand, 17> default_unlocks{{
			{0x5f30c3, 0x682504}, {0x5f30e5, 0x682564}, {0x5f3107, 0x68250c}, {0x5f3129, 0x6824a8},
			{0x5f314b, 0x682548}, {0x5f316d, 0x6824c4}, {0x5f319b, 0x682550}, {0x5f31bd, 0x6824bc},
			{0x5f31df, 0x6824f4}, {0x5f3201, 0x682534}, {0x5f3223, 0x6824dc}, {0x5f3245, 0x6824ec},
			{0x5f3267, 0x682520}, {0x5f3289, 0x6824b4}, {0x5f32ab, 0x6824cc}, {0x5f32cd, 0x6a37c4},
			{0x5f32ef, 0x6a37b4},
		}};

		// The folder format strings: 0x55e760 sprintf's the save folder (then mkdir -p, 0x629850), the
		// screenshot code at 0x4019f0 the same way. The Demo variants (0x55e78b, 0x4019cb) only run in the demo.
		constexpr operand save_folder_format{0x55e7ae, 0x69ab14};       // "%s\Activision\X-Men Legends 2\Save\"
		constexpr operand screenshot_folder_format{0x4019f1, 0x680048}; // "%s\Activision\X-Men Legends 2\Screenshots\"

		// Names the patched pushes point at: they must live as long as the process.
		char team_names[4][32]{};
		char save_format[128]{};
		char screenshot_format[128]{};

		bool readable_and_expected(const operand& op)
		{
			__try
			{
				const auto* at = reinterpret_cast<const std::uint8_t*>(op.address);
				std::uint32_t value = 0;
				std::memcpy(&value, at, sizeof(value));
				return at[-1] == push_imm32 && value == op.expected;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool write(const std::uintptr_t address, const std::uint32_t value)
		{
			auto* target = reinterpret_cast<void*>(address);
			DWORD old_protect = 0;
			if (!VirtualProtect(target, sizeof(value), PAGE_EXECUTE_READWRITE, &old_protect))
			{
				return false;
			}
			std::memcpy(target, &value, sizeof(value));
			VirtualProtect(target, sizeof(value), old_protect, &old_protect);
			FlushInstructionCache(GetCurrentProcess(), target, sizeof(value));
			return true;
		}

		std::string trim_lower(std::string s)
		{
			const auto first = s.find_first_not_of(" \t");
			const auto last = s.find_last_not_of(" \t");
			s = first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
			for (auto& c : s)
			{
				c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			}
			return s;
		}

		std::vector<std::string> split_team(const std::string& list)
		{
			std::vector<std::string> names;
			std::string current;
			for (const char c : list)
			{
				if (c == ',')
				{
					names.push_back(trim_lower(current));
					current.clear();
				}
				else
				{
					current += c;
				}
			}
			names.push_back(trim_lower(current));
			names.resize(team_slots.size());  // up to four; missing slots empty
			return names;
		}

		void seat_team(const std::string& list)
		{
			for (const auto& slot : team_slots)
			{
				for (const auto& op : slot)
				{
					if (!readable_and_expected(op))
					{
						logger::write("new game: startFirstMission at 0x%08X isn't the retail code - team left as the game has it",
						              static_cast<unsigned>(op.address));
						return;
					}
				}
			}

			const auto names = split_team(list);
			std::string described;
			for (size_t i = 0; i < team_slots.size(); ++i)
			{
				std::uint32_t pointer = empty_name;
				if (!names[i].empty())
				{
					strncpy_s(team_names[i], names[i].c_str(), _TRUNCATE);
					pointer = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(team_names[i]));
				}
				for (const auto& op : team_slots[i])
				{
					if (!write(op.address, pointer))
					{
						logger::write("new game: ERROR: couldn't write 0x%08X (error %lu)", static_cast<unsigned>(op.address), GetLastError());
						return;
					}
				}
				described += (i ? ", " : "") + (names[i].empty() ? std::string("(empty)") : names[i]);
			}
			logger::write("new game: starting team %s ([Game] NewGameTeam; startFirstMission 0x4a7b10)", described.c_str());
		}

		void clear_default_unlocks()
		{
			for (const auto& op : default_unlocks)
			{
				if (!readable_and_expected(op))
				{
					logger::write("new game: resetgame at 0x%08X isn't the retail code - default unlocks left as the game has them",
					              static_cast<unsigned>(op.address));
					return;
				}
			}
			for (const auto& op : default_unlocks)
			{
				if (!write(op.address, empty_name))
				{
					logger::write("new game: ERROR: couldn't write 0x%08X (error %lu)", static_cast<unsigned>(op.address), GetLastError());
					return;
				}
			}
			logger::write("new game: resetgame unlocks no heroes ([Game] ResetUnlocks=0; the mod's scripts unlock them)");
		}

		const limits_rules::guard* first_new_game_plus_mismatch()
		{
			__try
			{
				return new_game_plus_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(image_base));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &new_game_plus_rules::guards[0];
			}
		}

		// [Game] NewGamePlus=0: setDifficultyLevel starts the game with the default statistics even when
		// the profile has Hard unlocked, instead of offering XML2's saved-statistics choice (new_game_plus_rules.hpp).
		void skip_new_game_plus()
		{
			using namespace new_game_plus_rules;
			constexpr const char* as_before = "New Game keeps XML2's saved-statistics choice once Hard is unlocked";
			if (const auto* g = first_new_game_plus_mismatch())
			{
				logger::write("new game: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
				return;
			}
			auto* target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(offer_branch));
			DWORD old_protect = 0;
			if (!VirtualProtect(target, patched_branch.size(), PAGE_EXECUTE_READWRITE, &old_protect))
			{
				logger::write("new game: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", offer_branch, GetLastError(), as_before);
				return;
			}
			apply(reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
			VirtualProtect(target, patched_branch.size(), old_protect, &old_protect);
			FlushInstructionCache(GetCurrentProcess(), target, patched_branch.size());
			logger::write("new game: no New Game+ ([Game] NewGamePlus=0): with Hard unlocked, New Game starts with the default statistics "
			              "instead of offering saved ones (setDifficultyLevel's branch at 0x%08lX always goes to 0x%08lX)",
			              offer_branch, start_path);
		}

		void move_save_folder(const std::string& folder)
		{
			if (!valid_save_folder(folder))
			{
				logger::write("new game: [Game] SaveFolder=%s isn't a plain folder name (no \\ / : * ? \" < > | %%, at most %zu characters) - saves stay in X-Men Legends 2",
				              folder.c_str(), save_folder_max);
				return;
			}
			if (!readable_and_expected(save_folder_format) || !readable_and_expected(screenshot_folder_format))
			{
				logger::write("new game: the save folder code isn't the retail code - saves stay in X-Men Legends 2");
				return;
			}
			sprintf_s(save_format, "%%s\\Activision\\%s\\Save\\", folder.c_str());
			sprintf_s(screenshot_format, "%%s\\Activision\\%s\\Screenshots\\", folder.c_str());
			if (!write(save_folder_format.address, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(save_format))) ||
			    !write(screenshot_folder_format.address, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(screenshot_format))))
			{
				logger::write("new game: ERROR: couldn't patch the save folder (error %lu)", GetLastError());
				return;
			}
			logger::write("new game: saves, settings.dat (hero unlocks) and screenshots in Documents\\Activision\\%s ([Game] SaveFolder), not X-Men Legends 2's",
			              folder.c_str());
		}
	}

	void install(const HMODULE game)
	{
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			return;
		}
		const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
		wchar_t team[160]{};
		GetPrivateProfileStringW(L"Game", L"NewGameTeam", L"", team, static_cast<DWORD>(std::size(team)), ini.c_str());
		if (team[0])
		{
			std::string narrow;
			for (const wchar_t* p = team; *p; ++p)
			{
				narrow += *p < 128 ? static_cast<char>(*p) : '?';
			}
			seat_team(narrow);
		}
		if (GetPrivateProfileIntW(L"Game", L"ResetUnlocks", 1, ini.c_str()) == 0)
		{
			clear_default_unlocks();
		}
		if (GetPrivateProfileIntW(L"Game", L"NewGamePlus", 1, ini.c_str()) == 0)
		{
			skip_new_game_plus();
		}
		wchar_t folder[save_folder_max + 2]{}; // one over the limit, so a longer name is refused, not cut
		GetPrivateProfileStringW(L"Game", L"SaveFolder", L"", folder, static_cast<DWORD>(std::size(folder)), ini.c_str());
		if (folder[0])
		{
			std::string narrow;
			for (const wchar_t* p = folder; *p; ++p)
			{
				narrow += *p < 128 ? static_cast<char>(*p) : '\x7f'; // non-ASCII is refused by valid_save_folder
			}
			move_save_folder(narrow);
		}
	}
}
