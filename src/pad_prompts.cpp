#include "pad_prompts.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "pad_prompts_rules.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pad_prompts
{
	namespace
	{
		using namespace pad_prompts_rules;

		constexpr const char* as_before = "the game's own prompts";

		mode chosen_mode = mode::automatic;
		bool colours = true;
		activity seen;                             // the game thread's, from the poll
		std::array<int, players> last_shown{{-1, -1, -1, -1}}; // for the log

		// The labels handed back: the renderer reads one before it asks for the next, as with the game's single
		// buffer (0xa68c18); a few in turn all the same.
		char labels[8][160]{};
		std::size_t next_label = 0;

		// The power wheel's tokens: they must live as long as the process.
		char wheel_names[wheel_replacements.size()][8]{};

		// The colour site's table and stub.
		std::array<std::uint16_t, 256> colour_table = label_colours();

		template <typename T>
		T at(const DWORD va)
		{
			return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));
		}

		const char* stock_label(const int code)
		{
			return at<const char*(__cdecl*)(int)>(label_function)(code);
		}

		const std::uint8_t* binding_map(const std::uint32_t row, const int player)
		{
			const auto* configuration = at<std::uint8_t* const*>(maps_global)[row * players + static_cast<std::uint32_t>(player)];
			return configuration ? configuration + map_in_configuration : nullptr;
		}

		shown shown_for(const int player, const std::uint8_t* play_map)
		{
			const shown s = choose(chosen_mode, has_keyboard(play_map), pad_of(play_map), seen);
			auto& last = last_shown[static_cast<std::size_t>(player)];
			if (last != static_cast<int>(s))
			{
				last = static_cast<int>(s);
				logger::write("prompts: player %d's prompts show the %s", player + 1, s == shown::pad ? "pad" : "keyboard");
			}
			return s;
		}

		char* next_buffer()
		{
			char* buffer = labels[next_label];
			next_label = (next_label + 1) % std::size(labels);
			return buffer;
		}

		// In place of 0x619e30, called from CStrings::get (cdecl, the code pushed, the label returned).
		const char* __cdecl prompt_label(const int code)
		{
			const auto* input = *at<std::uint8_t* const*>(input_pointer);
			if (!input)
			{
				return at<const char*>(blank_label);
			}
			const auto t = prompt_target(code);
			const int action = action_of(t.code);
			if (action < 0)
			{
				return at<const char*>(blank_label);
			}
			const std::uint32_t row = t.play_row ? 0 : *at<const std::uint32_t*>(row_global);
			const auto* map = row < static_cast<std::uint32_t>(rows) ? binding_map(row, t.player) : nullptr;
			const auto* play_map = binding_map(0, t.player);
			if (!map || !play_map)
			{
				return stock_label(code);
			}

			const auto slots = read_bindings(map, action);
			const int slot = shown_for(t.player, play_map) == shown::pad ? pad_slot(slots) : keyboard_slot(slots);
			if (slot < 0)
			{
				return at<const char*>(unbound_label);
			}
			const auto& b = slots[static_cast<std::size_t>(slot)];
			char* label = next_buffer();
			if (pad_device(b.device))
			{
				strncpy_s(label, sizeof(labels[0]), pad_label(b.control, colours).c_str(), _TRUNCATE);
				return label;
			}
			using key_name_t = const char*(__fastcall*)(const void* input, void* edx, std::uint32_t device, std::uint32_t control);
			const char* name = at<key_name_t>(key_name_function)(input, nullptr, b.device, b.control);
			if (!name)
			{
				return at<const char*>(no_name_label);
			}
			_snprintf_s(label, sizeof(labels[0]), _TRUNCATE, "[%s]", name);
			return label;
		}

		// In place of the call to the per-frame poll (0x6285c0, __thiscall, no arguments): the poll, then what it read.
		void __fastcall poll_input(void* input, void* edx)
		{
			at<void(__fastcall*)(void*, void*)>(poll_function)(input, edx);
			if (input)
			{
				seen.sample(static_cast<const std::uint8_t*>(input));
			}
		}

		std::uint8_t* game_image()
		{
			return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base));
		}

		const guard* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(game_image());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}

		DWORD address_of(const void* pointer)
		{
			return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(pointer));
		}

		// The colour stub in executable memory of its own; 0 if there's none to be had.
		DWORD place_colour_stub()
		{
			const auto bytes = colour_stub(address_of(colour_table.data()));
			void* memory = VirtualAlloc(nullptr, bytes.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
			if (!memory)
			{
				return 0;
			}
			std::copy(bytes.begin(), bytes.end(), static_cast<std::uint8_t*>(memory));
			DWORD old_protect = 0;
			if (!VirtualProtect(memory, bytes.size(), PAGE_EXECUTE_READ, &old_protect))
			{
				VirtualFree(memory, 0, MEM_RELEASE);
				return 0;
			}
			FlushInstructionCache(GetCurrentProcess(), memory, bytes.size());
			return address_of(memory);
		}

		// Every write at once: the pages they are on are made writable first, all of them, so a refusal leaves the
		// game as it was; then every write; then the pages' protection goes back, in reverse order.
		bool patch(const std::vector<byte_write>& writes)
		{
			SYSTEM_INFO system{};
			GetSystemInfo(&system);
			const std::uintptr_t page_size = system.dwPageSize;
			std::vector<std::uintptr_t> pages;
			for (const auto& w : writes)
			{
				const auto first = reinterpret_cast<std::uintptr_t>(game_image() + (w.va - image_base));
				pages.push_back(first & ~(page_size - 1));
				pages.push_back((first + w.bytes.size() - 1) & ~(page_size - 1));
			}
			std::ranges::sort(pages);
			pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

			std::vector<DWORD> protection(pages.size());
			for (std::size_t i = 0; i < pages.size(); ++i)
			{
				if (!VirtualProtect(reinterpret_cast<void*>(pages[i]), page_size, PAGE_EXECUTE_READWRITE, &protection[i]))
				{
					logger::write("prompts: ERROR: can't unprotect XMen2.exe at 0x%08lX (error %lu) - %s", static_cast<unsigned long>(pages[i]), GetLastError(), as_before);
					for (std::size_t j = i; j-- > 0;)
					{
						VirtualProtect(reinterpret_cast<void*>(pages[j]), page_size, protection[j], &protection[j]);
					}
					return false;
				}
			}
			apply(game_image(), writes);
			for (const auto page : pages)
			{
				FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(page), page_size);
			}
			for (std::size_t i = pages.size(); i-- > 0;)
			{
				VirtualProtect(reinterpret_cast<void*>(pages[i]), page_size, protection[i], &protection[i]);
			}
			return true;
		}

		std::string read_ini(const wchar_t* key)
		{
			return ini::text(L"Input", key).value_or(""); // the fix's one rule, ini_rules.hpp
		}
	}

	void install(const HMODULE game)
	{
		const auto mode_value = read_ini(L"Prompts");
		const auto m = parse_mode(mode_value);
		if (!m.error.empty())
		{
			logger::write("prompts: [Input] Prompts=%s %s - %s", mode_value.c_str(), m.error.c_str(), as_before);
			return;
		}
		if (m.value == mode::off)
		{
			logger::write("prompts: %s ([Input] Prompts=off) - nothing patched", as_before);
			return;
		}
		const auto colour_value = read_ini(L"PromptColors");
		const auto c = parse_switch(colour_value);
		if (!c.error.empty())
		{
			logger::write("prompts: [Input] PromptColors=%s %s - the face buttons in their colours", colour_value.c_str(), c.error.c_str());
		}
		chosen_mode = m.value;
		colours = c.error.empty() ? c.value : true;

		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("prompts: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("prompts: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}

		addresses dll;
		dll.label = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&prompt_label));
		dll.poll = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&poll_input));
		for (std::size_t i = 0; i < wheel_replacements.size(); ++i)
		{
			wheel_replacements[i].copy(wheel_names[i], sizeof(wheel_names[i]) - 1);
			dll.wheel[i] = address_of(wheel_names[i]);
		}
		if (colours)
		{
			dll.colour_stub = place_colour_stub();
			if (!dll.colour_stub)
			{
				logger::write("prompts: no memory for the colour stub (error %lu) - the face buttons as [A] [B] [X] [Y]", GetLastError());
				colours = false;
			}
		}
		if (!patch(writes_for(dll)))
		{
			return;
		}
		logger::write("prompts: button prompts show %s ([Input] Prompts=%s) - the label call (0x4bd739) and the input poll's (0x61c479) patched, "
		              "the power wheel reads $ATTAC9 $SMAS9 $GUAR9 and $SMASH / $MOVE the bindings of play; %s",
		              chosen_mode == mode::automatic ? "the device each player last used" : chosen_mode == mode::pad ? "the pad" : "the keyboard",
		              describe(chosen_mode),
		              colours ? "A B X Y in the Xbox colours (0x5ef777)" : "every pad button as [A]-style text ([Input] PromptColors=0)");
	}
}
