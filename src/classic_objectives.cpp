#include "classic_objectives.hpp"
#include "classic_objectives_rules.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "game_version.hpp"
#include "limits_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace classic_objectives
{
	namespace
	{
		using namespace classic_objectives_rules;
		using lookup_t = bool(__thiscall*)(const void* record);
		using records_t = const unsigned char*(__thiscall*)(void* mission);
		using getter_t = void*(__cdecl*)();
		using guard_t = bool(__cdecl*)();

		bool installed = false;
		int window_offset = 0; // set when the objectives page opens; 0 until then and on the results page
		constexpr std::size_t max_records = 128; // the engine loads at most 75 per act (0x4899e0)

		struct records
		{
			std::array<objective, max_records> entries{};
			std::size_t count = 0;
			const objective* begin() const { return entries.data(); }
			const objective* end() const { return entries.data() + count; }
		};

		const unsigned char* record_array(int& count)
		{
			count = 0;
			void* mission = reinterpret_cast<getter_t>(mission_getter)();
			if (!mission) return nullptr;
			const auto* base = reinterpret_cast<records_t>((*static_cast<void***>(mission))[0x34 / 4])(mission);
			if (!base) return nullptr;
			count = *reinterpret_cast<const int*>(base + record_count_offset);
			if (count < 0) count = 0;
			if (count > static_cast<int>(max_records)) count = static_cast<int>(max_records);
			return base;
		}

		records read(const unsigned char* base, int count)
		{
			records r;
			for (int i = 0; i < count; ++i)
			{
				const unsigned char* rec = base + static_cast<std::size_t>(i) * record_stride;
				const bool top = rec[parent_offset] == 0;
				const bool major = (rec[flags_offset] & major_bit) != 0;
				const bool hidden = reinterpret_cast<lookup_t>(hidden_lookup)(rec);
				r.entries[r.count++] = {major && top, hidden, reinterpret_cast<lookup_t>(complete_lookup)(rec),
				                        !major && top && !hidden};
			}
			return r;
		}

		// The native selector/page walks ask about one record at a time (their own loops keep the order and the
		// end test). base = the first record, as both loops hold it.
		bool __cdecl fills(const unsigned char* base, const unsigned char* rec) noexcept
		{
			__try
			{
				if (!base || rec < base) return false;
				const auto index = static_cast<std::size_t>(rec - base) / record_stride;
				int count = *reinterpret_cast<const int*>(base + record_count_offset);
				if (count < 0 || index >= static_cast<std::size_t>(count) || index >= max_records) return false;
				// The draw asks seven walks a frame: read only the records up to this one (the rank needs no more).
				const auto r = read(base, static_cast<int>(index) + 1);
				return fills_slot(r, index, window_offset);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		// Entering the objectives mode (PDA mode 1): choose the window and the page to open on, then the native
		// refresh runs, which focuses the remembered item of this mode when it names a filled slot.
		void __cdecl prepare(unsigned char* menu) noexcept
		{
			__try
			{
				window_offset = 0;
				if (reinterpret_cast<guard_t>(results_guard)()) return;
				int count = 0;
				const auto* base = record_array(count);
				if (!base) return;
				const auto r = read(base, count);
				const auto p = plan_for(r);
				window_offset = p.offset;
				const int mode = *reinterpret_cast<const int*>(menu + mode_offset);
				char name[remembered_focus_size];
				if (mode < 0 || mode > 2 || !slot_name(p.focus, name)) return;
				std::memcpy(reinterpret_cast<char*>(remembered_focus) + mode * remembered_focus_size, name, sizeof name);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { window_offset = 0; }
		}

		DWORD selector_counted_va = selector_counted, selector_skip_va = selector_skip;
		DWORD page_counted_va = page_counted, page_skip_va = page_skip, refresh_va = enter_refresh;

		// 0x5cce98: esi = record + 0x20, ebx = the first record.
		__declspec(naked) void selector_stub()
		{
			__asm
			{
				pushad
				lea eax, [esi - 0x20]
				push eax
				push ebx
				call fills
				add esp, 8
				test al, al
				popad
				jz skip
				jmp dword ptr [selector_counted_va]
			skip:
				jmp dword ptr [selector_skip_va]
			}
		}

		// 0x5ce148: esi = record + 0x1aa, edi = the first record. The counted path expects eax = the record's name.
		__declspec(naked) void page_stub()
		{
			__asm
			{
				pushad
				lea eax, [esi - 0x1aa]
				push eax
				push edi
				call fills
				add esp, 8
				test al, al
				popad
				jz skip
				lea eax, [esi - 0x16a]
				jmp dword ptr [page_counted_va]
			skip:
				jmp dword ptr [page_skip_va]
			}
		}

		// 0x5ce484 call: ecx = the PDA menu; the native refresh returns to the mode switch.
		__declspec(naked) void enter_stub()
		{
			__asm
			{
				pushad
				push ecx
				call prepare
				add esp, 4
				popad
				jmp dword ptr [refresh_va]
			}
		}

		std::vector<unsigned char> jump(std::uint32_t site, const void* target, std::size_t size, unsigned char op = 0xe9)
		{
			std::vector<unsigned char> bytes(size, 0x90);
			bytes[0] = op;
			const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - site - 5;
			std::memcpy(bytes.data() + 1, &delta, 4);
			return bytes;
		}

		std::vector<unsigned char> hex_bytes(std::string_view hex)
		{
			std::vector<unsigned char> out(hex.size() / 2);
			for (std::size_t i = 0; i < out.size(); ++i)
			{
				auto nibble = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
				out[i] = static_cast<unsigned char>(nibble(hex[2 * i]) * 16 + nibble(hex[2 * i + 1]));
			}
			return out;
		}
	}

	bool requested() { return ini::flag(L"Game", L"ClassicObjectives", false); }

	void install(HMODULE game)
	{
		if (installed || !requested()) return;
		if (reinterpret_cast<std::uintptr_t>(game) != 0x400000)
		{
			logger::write("classic objectives: unsupported executable; the native PDA objective page is kept");
			return;
		}
		xml1_hook_transaction::memory memory;
		for (const auto& g : guards)
		{
			std::array<unsigned char, 32> bytes{};
			if (!memory.read(g.va, bytes.data(), g.hex.size() / 2) || !limits_rules::matches(bytes.data(), g.hex))
			{
				logger::write("classic objectives: guard refused at %08X (%s); the native PDA objective page is kept", g.va, g.what);
				return;
			}
		}
		const std::vector<xml1_hook_transaction::patch> patches{
			{selector_test, hex_bytes(guards[0].hex), jump(selector_test, &selector_stub, 14)},
			{page_test, hex_bytes(guards[2].hex), jump(page_test, &page_stub, 9)},
			{enter_call, hex_bytes(guards[4].hex), jump(enter_call, &enter_stub, 5, 0xe8)},
		};
		bool safe = true;
		installed = xml1_hook_transaction::apply(memory, patches, &safe);
		if (!safe) game_version::refuse_startup();
		logger::write("classic objectives: %s (visible objectives only, the page opens on the current one)",
		              installed ? "enabled" : "could not be installed; the native PDA objective page is kept");
	}
}
