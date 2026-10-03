#include "limits.hpp"
#include "limits_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace limits
{
	namespace
	{
		using namespace limits_rules;

		std::uint8_t* base = nullptr; // XMen2.exe, when it is at 0x400000

		// What status() reads; 0 = not known (the code around it isn't the retail build's).
		DWORD actor_live_va = 0;
		int actor_cap = stock_actor_slots;
		DWORD name_count_offset = 0;
		int name_cap = stock_resource_names;
		bool motions_readable = false;
		bool igb_readable = false;
		int item_cap = stock_item_enhancements;
		bool items_readable = false;
		int style_cap = stock_fight_styles;
		DWORD style_count_offset = 0;

		std::uint8_t* at(const DWORD va)
		{
			return base + (va - image_base);
		}

		DWORD address_of(const void* pointer)
		{
			return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(pointer));
		}

		// No C++ objects in these: the reads are guarded, in case this isn't XMen2.exe at all.
		bool bytes_match(const DWORD va, const std::string_view hex)
		{
			__try
			{
				return matches(at(va), hex);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool read_dword(const DWORD address, DWORD& value)
		{
			__try
			{
				std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof(value));
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool write_dword(const DWORD address, const DWORD value)
		{
			__try
			{
				std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), &value, sizeof(value));
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		std::optional<DWORD> read(const DWORD address)
		{
			DWORD value = 0;
			return address && read_dword(address, value) ? std::optional<DWORD>(value) : std::nullopt;
		}

		// The first site or guard whose bytes aren't the retail build's; 0 when they all are.
		template <typename Sites>
		DWORD first_mismatch(const Sites& sites)
		{
			for (const auto& s : sites)
			{
				if (!bytes_match(s.va, s.hex))
				{
					return s.va;
				}
			}
			return 0;
		}

		DWORD names_mismatch()
		{
			if (const DWORD va = first_mismatch(name_sites))
			{
				return va;
			}
			return first_mismatch(name_guards);
		}

		DWORD actors_mismatch()
		{
			if (const DWORD va = first_mismatch(actor_sites))
			{
				return va;
			}
			if (const DWORD va = first_mismatch(actor_references))
			{
				return va;
			}
			if (const DWORD va = first_mismatch(actor_guards))
			{
				return va;
			}
			return bytes_match(find_next_40.va, find_next_40.hex) ? 0 : find_next_40.va;
		}

		DWORD styles_mismatch()
		{
			return style_first_mismatch([](const DWORD va, const std::string_view hex) { return bytes_match(va, hex); });
		}

		DWORD items_mismatch()
		{
			if (const DWORD va = first_mismatch(item_sites))
			{
				return va;
			}
			if (const DWORD va = first_mismatch(item_guards))
			{
				return va;
			}
			if (!bytes_match(item_clone_call.va, item_clone_call.hex))
			{
				return item_clone_call.va;
			}
			return bytes_match(item_find_next.va, item_find_next.hex) ? 0 : item_find_next.va;
		}

		// Writes every operand. The pages they are on are made writable first, all of them, so a refusal
		// leaves the code as it was; then every write; then the pages' protection goes back, in reverse
		// order. Each page is unprotected and restored exactly once: the sites share pages, and restoring
		// one site's protection before writing the next re-locks the page for it (the options menu crashed
		// at startup that way, options_menu.cpp).
		bool patch(const std::vector<operand_write>& writes, const char* refused)
		{
			SYSTEM_INFO system{};
			GetSystemInfo(&system);
			const std::uintptr_t page_size = system.dwPageSize;
			std::vector<std::uintptr_t> pages;
			for (const auto& w : writes)
			{
				const auto first = reinterpret_cast<std::uintptr_t>(at(w.va));
				pages.push_back(first & ~(page_size - 1));
				pages.push_back((first + w.size - 1) & ~(page_size - 1));
			}
			std::ranges::sort(pages);
			pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

			std::vector<DWORD> protection(pages.size());
			for (std::size_t i = 0; i < pages.size(); ++i)
			{
				if (!VirtualProtect(reinterpret_cast<void*>(pages[i]), page_size, PAGE_EXECUTE_READWRITE, &protection[i]))
				{
					logger::write("limits: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", static_cast<unsigned long>(pages[i]), GetLastError(), refused);
					for (std::size_t j = i; j-- > 0;)
					{
						VirtualProtect(reinterpret_cast<void*>(pages[j]), page_size, protection[j], &protection[j]);
					}
					return false;
				}
			}
			for (const auto& w : writes)
			{
				apply_write(base, w);
			}
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

		// The game's constructor, __thiscall; __fastcall with an unused edx calls it the same way.
		using constructor_t = void*(__fastcall*)(void* self, void* edx);

		bool construct_name_table(void* table)
		{
			__try
			{
				reinterpret_cast<constructor_t>(at(name_table_constructor))(table, nullptr);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		DWORD dword_at(const void* object, const DWORD offset)
		{
			DWORD value = 0;
			std::memcpy(&value, static_cast<const std::uint8_t*>(object) + offset, sizeof(value));
			return value;
		}

		// The name table: patched for `capacity` names, then built in a block of the DLL by the game's
		// own constructor and put where the getter looks (0x7ac244), so the getter hands it out and never
		// allocates one of its own (or registers the free that goes with that) - nothing depends on how
		// much room the game's memory pool 0xe has for a table twice the size.
		bool raise_names(const int capacity)
		{
			constexpr const char* stays = "the resource name table stays at 450 names";
			if (!base)
			{
				logger::write("limits: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", stays);
				return false;
			}
			if (const DWORD va = names_mismatch())
			{
				logger::write("limits: XMen2.exe doesn't have the expected code at 0x%08lX (not the retail build?) - %s", va, stays);
				return false;
			}
			DWORD existing = 0;
			if (!read_dword(name_table_pointer, existing) || existing)
			{
				logger::write("limits: the game has built its resource name table already (0x%08lX) - %s", existing, stays);
				return false;
			}

			const auto layout = name_layout_for(capacity);
			void* table = VirtualAlloc(nullptr, layout.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); // zero-filled, as the game's allocator hands it out
			if (!table)
			{
				logger::write("limits: can't allocate %lu bytes for the resource name table (error %lu) - %s", layout.size, GetLastError(), stays);
				return false;
			}
			const auto writes = name_writes(layout);
			if (!patch(writes, stays))
			{
				VirtualFree(table, 0, MEM_RELEASE);
				return false;
			}
			name_cap = capacity;
			name_count_offset = layout.count;

			// From here on the code is patched: if the table can't be put in place, the getter builds one
			// itself at the first lookup, at the new size (its allocation is one of the fields).
			const bool built = construct_name_table(table);
			if (!built || dword_at(table, 0) != name_table_vtable || dword_at(table, name_pool_offset + layout.node_ring_count) != static_cast<DWORD>(capacity) ||
			    dword_at(table, layout.count) != 0)
			{
				logger::write("limits: ERROR: the game's constructor (0x55af00) %s the new resource name table - the game builds its own at first use, for %d names (from its memory pool 0xe)",
				              built ? "didn't build" : "crashed on", capacity);
				return true;
			}
			if (!write_dword(name_table_pointer, address_of(table)))
			{
				logger::write("limits: ERROR: can't store the resource name table at 0x%08lX - the game builds its own at first use, for %d names (from its memory pool 0xe)", name_table_pointer,
				              capacity);
				return true;
			}
			logger::write("limits: resource name table raised from 450 to %d names - built at 0x%08lX (%lu bytes) by the game's own constructor (0x55af00) for its getter (0x55af80, "
			              "pointer at 0x7ac244); %zu fields patched; its count is at +0x%lX",
			              capacity, address_of(table), layout.size, writes.size(), layout.count);
			return true;
		}

		// The actor table: its code patched for `slots`, the getter, the atexit thunk and the two
		// findNext calls pointed at a zero-filled block and a clone of the DLL's. The game builds the
		// block with its own constructor at the first call of the getter, as it did the static.
		bool raise_actors(const int slots)
		{
			constexpr const char* stays = "the actor table stays at 40 slots";
			if (const DWORD va = actors_mismatch())
			{
				logger::write("limits: XMen2.exe doesn't have the expected code at 0x%08lX (not the retail build?) - %s", va, stays);
				return false;
			}
			DWORD built = 0;
			if (!read_dword(actor_init_guard, built) || (built & 1))
			{
				logger::write("limits: the game has built its actor table already (0x7b7a58 = 0x%08lX) - %s", built, stays);
				return false;
			}

			const auto layout = actor_layout_for(slots);
			const auto clone_size = hex_size(find_next_40.hex);
			auto* object = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, layout.object_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)); // zero-filled: the pool constructor clears two bitmap dwords only
			auto* clone = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, clone_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			const auto release = [&]
			{
				if (object) VirtualFree(object, 0, MEM_RELEASE);
				if (clone) VirtualFree(clone, 0, MEM_RELEASE);
			};
			if (!object || !clone)
			{
				logger::write("limits: can't allocate the actor table (error %lu) - %s", GetLastError(), stays);
				release();
				return false;
			}
			std::memcpy(clone, at(find_next_40.va), clone_size); // compared with the retail bytes above
			finish_clone(clone, find_next_40, layout, address_of(clone));
			DWORD old_protection = 0;
			if (!VirtualProtect(clone, clone_size, PAGE_EXECUTE_READ, &old_protection))
			{
				logger::write("limits: can't make the findNext clone executable (error %lu) - %s", GetLastError(), stays);
				release();
				return false;
			}
			FlushInstructionCache(GetCurrentProcess(), clone, clone_size);

			const auto writes = actor_writes(layout, address_of(object), address_of(clone));
			if (!patch(writes, stays))
			{
				release();
				return false;
			}
			actor_cap = slots;
			actor_live_va = address_of(object) + 4 + layout.live;
			logger::write("limits: actor table raised from 40 to %d slots (skins and animation databases) - CAnimMotionCache moves from 0x7b05e8 to 0x%08lX (%lu bytes, id mask 0x%lX), "
			              "built and torn down by the game's own code; live count at 0x%08lX (was 0x7b79ac); %zu fields, 3 references and 2 calls patched, bitset<40>::findNext "
			              "cloned to 0x%08lX with its %zu",
			              slots, address_of(object), layout.object_size, layout.id_mask, actor_live_va, actor_sites.size(), address_of(clone), find_next_40.fields.size());
			return true;
		}

		// The loader's end, in place of its call of the record walk (0x47ce80, __thiscall, no arguments):
		// the walk, then the affix counts against the random pickers' stack arrays.
		using record_walk_t = void(__fastcall*)(void* manager, void* edx);

		void __fastcall loader_end(void* manager, void* /*edx*/)
		{
			reinterpret_cast<record_walk_t>(static_cast<std::uintptr_t>(item_record_walk))(manager, nullptr);
			DWORD prefixes = 0, suffixes = 0;
			if (manager && read_dword(address_of(manager) + item_prefix_count, prefixes) && read_dword(address_of(manager) + item_suffix_count, suffixes) && !affixes_fit(prefixes, suffixes))
			{
				logger::write_once("limits:affixes", "limits: WARNING: Data/items has %lu prefixes and %lu suffixes - the game's random affix pickers (0x47e690, 0x47ee90) keep room for 375 of each on the stack, "
				                                     "whatever the enhancement pool holds; past 375 they overrun it. Keep the prefixes and the suffixes at 375 or fewer",
				                   static_cast<unsigned long>(prefixes), static_cast<unsigned long>(suffixes));
			}
		}

		// The item manager's enhancement record pool: its records and bitmap moved to the end of a bigger
		// manager (allocated by the game at its first use, after this), its code patched for `records`,
		// the pool clear's findNext pointed at a clone of the DLL's.
		bool raise_items(const int records)
		{
			constexpr const char* stays = "the item enhancement pool stays at 375 records";
			if (const DWORD va = items_mismatch())
			{
				logger::write("limits: XMen2.exe doesn't have the expected code at 0x%08lX (not the retail build?) - %s", va, stays);
				return false;
			}
			DWORD built = 0;
			if (!read_dword(item_manager_pointer, built) || built)
			{
				logger::write("limits: the game has built its item manager already (0x72a514 = 0x%08lX) - %s", built, stays);
				return false;
			}
			const auto layout = item_layout_for(records);
			const auto clone_size = hex_size(item_find_next.hex);
			auto* clone = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, clone_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (!clone)
			{
				logger::write("limits: can't allocate the findNext clone (error %lu) - %s", GetLastError(), stays);
				return false;
			}
			std::memcpy(clone, at(item_find_next.va), clone_size); // compared with the retail bytes above
			finish_item_clone(clone, layout);
			DWORD old_protection = 0;
			if (!VirtualProtect(clone, clone_size, PAGE_EXECUTE_READ, &old_protection))
			{
				logger::write("limits: can't make the findNext clone executable (error %lu) - %s", GetLastError(), stays);
				VirtualFree(clone, 0, MEM_RELEASE);
				return false;
			}
			FlushInstructionCache(GetCurrentProcess(), clone, clone_size);
			if (!patch(item_writes(layout, address_of(clone), address_of(reinterpret_cast<const void*>(&loader_end))), stays))
			{
				VirtualFree(clone, 0, MEM_RELEASE);
				return false;
			}
			item_cap = records;
			logger::write("limits: item enhancement pool raised from 375 to %d records - the item manager grows from 0x7a00 to 0x%lX bytes (allocated by the game at its first use), "
			              "its records move from +0x2594 to +0x%lX and their bitmap from +0x602c to +0x%lX (%lu dwords); %zu fields and 2 calls patched, bitset<375>::findNext cloned to 0x%08lX "
			              "with its %zu; records in use at [0x72a514]+0x6064; the loader's end (0x4806ad) checks the prefix and suffix counts against the random pickers' 375",
			              records, layout.manager_size, layout.records_offset, layout.bitmap_offset, layout.bitmap_dwords, item_sites.size(), address_of(clone), item_find_next.fields.size());
			return true;
		}

		void* __fastcall construct_style_ring(void* self, void*)
		{
			return style_ring_construct(self, style_cap);
		}

		DWORD __fastcall push_style_ring(void* self, void*, const DWORD* value)
		{
			return style_ring_push(self, *value, style_cap);
		}

		bool raise_styles(const int capacity)
		{
			constexpr const char* stays = "the fighting style registry stays at 19 entries";
			if (!base)
			{
				logger::write("limits: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", stays);
				return false;
			}
			if (const DWORD va = styles_mismatch())
			{
				logger::write("limits: XMen2.exe doesn't have the expected code at 0x%08lX (not the retail build?) - %s", va, stays);
				return false;
			}
			DWORD existing = 0;
			if (!read_dword(style_manager_pointer, existing) || existing)
			{
				logger::write("limits: the game has built its fighting style manager already (0x%08lX) - %s", existing, stays);
				return false;
			}
			const auto layout = style_layout_for(capacity);
			const auto writes = style_writes(layout, address_of(reinterpret_cast<const void*>(&construct_style_ring)), address_of(reinterpret_cast<const void*>(&push_style_ring)));
			style_cap = capacity; // the helpers must see the new size before any patched caller can run
			if (!patch(writes, stays))
			{
				style_cap = stock_fight_styles;
				return false;
			}
			style_count_offset = layout.count;
			logger::write("limits: fighting style registry raised from 19 to %d entries - manager allocated by the game at first use grows from 0x29a18 to 0x%lX bytes; "
			              "tree nodes and free ring expanded, styles at +0x%lX, bitmap at +0x%lX, auxiliary manager at +0x%lX; %zu fields and 2 ring calls patched; count at +0x%lX",
			              capacity, layout.manager_size, layout.styles, layout.bitmap, layout.auxiliary, style_sites.size(), layout.count);
			return true;
		}

		std::optional<std::string> ini_value(const wchar_t* key)
		{
			return ini::text(L"Limits", key); // the fix's one rule, ini_rules.hpp
		}

		std::string counter(const std::optional<DWORD> live, const int cap)
		{
			return live ? std::to_string(static_cast<int>(*live)) + "/" + std::to_string(cap) : std::string("-");
		}
	}

	void install(const HMODULE game)
	{
		base = reinterpret_cast<std::uintptr_t>(game) == image_base ? reinterpret_cast<std::uint8_t*>(game) : nullptr;
		if (base)
		{
			// The counters status() reports, where the code that uses them is the retail build's.
			if (!first_mismatch(actor_sites) && !first_mismatch(actor_guards))
			{
				actor_live_va = actor_live_retail;
			}
			if (!names_mismatch())
			{
				name_count_offset = name_layout_for(stock_resource_names).count;
			}
			motions_readable = !first_mismatch(motion_counter_guards);
			igb_readable = !first_mismatch(igb_counter_guards);
			items_readable = !items_mismatch();
			if (!styles_mismatch()) style_count_offset = style_layout_for(stock_fight_styles).count;
		}

		const auto actor_slots = ini_value(L"ActorSlots");
		const auto resource_names = ini_value(L"ResourceNames");
		const auto item_enhancements = ini_value(L"ItemEnhancements");
		const auto fight_styles = ini_value(L"FightStyles");
		if (!actor_slots && !resource_names && !item_enhancements && !fight_styles)
		{
			logger::write("limits: the game's own caps - 40 actor slots, 450 resource names, 375 item enhancements (no [Limits] in xml2-fix.ini)");
			return;
		}
		const auto chosen = decide(actor_slots ? std::optional<std::string_view>(*actor_slots) : std::nullopt,
		                           resource_names ? std::optional<std::string_view>(*resource_names) : std::nullopt,
		                           item_enhancements ? std::optional<std::string_view>(*item_enhancements) : std::nullopt,
		                           fight_styles ? std::optional<std::string_view>(*fight_styles) : std::nullopt);
		for (const auto& note : chosen.notes)
		{
			logger::write("limits: %s", note.c_str());
		}
		if (chosen.fight_styles > stock_fight_styles) raise_styles(chosen.fight_styles);
		// The item enhancement pool: on its own, before the game's first item load (the manager is built
		// at its first use, long after DllMain).
		if (chosen.item_enhancements > stock_item_enhancements && base)
		{
			raise_items(chosen.item_enhancements);
		}
		if (chosen.actor_slots == stock_actor_slots && chosen.resource_names == stock_resource_names)
		{
			logger::write("limits: the game's own caps - 40 actor slots, 450 resource names");
			return;
		}

		// The name table first: the actor table needs it.
		const bool names_raised = chosen.resource_names > stock_resource_names && raise_names(chosen.resource_names);

		// TODO(IGB cache): the IGB cache's raise (limits_rules.hpp, igb-cache.md sections 1-7) goes
		// here, after the name table (which its records take names in too) and on the same terms.

		if (chosen.actor_slots > stock_actor_slots)
		{
			if (names_raised)
			{
				raise_actors(chosen.actor_slots);
			}
			else
			{
				logger::write("limits: the actor table stays at 40 slots - %d slots need a resource name table of at least %d names, and it wasn't raised", chosen.actor_slots,
				              names_needed(chosen.actor_slots));
			}
		}
	}

	std::string status()
	{
		std::optional<DWORD> names;
		if (name_count_offset)
		{
			if (const auto table = read(name_table_pointer); table && *table)
			{
				names = read(*table + name_count_offset);
			}
		}
		std::optional<DWORD> motions;
		if (motions_readable)
		{
			if (const auto pool = read(motion_pool_pointer); pool && *pool)
			{
				motions = read(*pool + motion_pool_live);
			}
		}
		std::optional<DWORD> items;
		if (items_readable)
		{
			if (const auto manager = read(item_manager_pointer); manager && *manager)
			{
				items = read(*manager + item_count_offset);
			}
		}
		std::optional<DWORD> styles;
		if (style_count_offset)
		{
			if (const auto manager = read(style_manager_pointer); manager && *manager) styles = read(*manager + style_count_offset);
		}
		return "actors " + counter(read(actor_live_va), actor_cap) + "; names " + counter(names, name_cap) + "; motions " + counter(motions, motion_pool_capacity) + "; igb " +
		       counter(igb_readable ? read(igb_live_retail) : std::nullopt, igb_capacity) + "; items " + counter(items, item_cap) + "; styles " + counter(styles, style_cap);
	}
}
