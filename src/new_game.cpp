#include "new_game.hpp"

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

		// Names the patched pushes point at: they must live as long as the process.
		char team_names[4][32]{};

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
	}
}
