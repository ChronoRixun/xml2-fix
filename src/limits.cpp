#include "resident_stats.hpp"
#include "limits.hpp"
#include "motion_pool.hpp"
#include "effect_templates.hpp"
#include "primitives_per_effect.hpp"
#include "script_pools.hpp"
#include "power_pools.hpp"
#include "item_capacity.hpp"
#include "limits_agreement_rules.hpp"
#include "gameplay_agreement.hpp"
#include "game_version.hpp"
#include "limits_rules.hpp"
#include "style_manager_plan.hpp"
#include "move_triggers_rules.hpp"
#include "move_triggers_runtime.hpp"
#include "fight_moves.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "patch_transaction.hpp"
#include "effect_pools.hpp"
#include "model_pools.hpp"
#include "particle_scratch.hpp"

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
		DWORD igb_live_va = 0;
		int igb_cap = stock_igb_records;
		bool igb_requested = false; // [Limits] IGBCache asked for a raise (rule 31 registers)
		bool igb_installed = false;
		limits_agreement_rules::capacities wanted_caps = limits_agreement_rules::stock;
		int item_cap = stock_item_enhancements;
		bool items_readable = false;
		int style_cap = stock_fight_styles;
		int event_cap = stock_style_events;
		int wanted_events = stock_style_events;
		bool manager_verified = true;
		std::optional<style_layout> published_manager;
		DWORD style_refusals = 0, event_refusals = 0;
		int curve_cap = stock_effect_curves;
		DWORD curve_live_va = 0;
		int move_trigger_cap = move_triggers_rules::stock_capacity;
		DWORD* move_trigger_arrays = nullptr;
		move_triggers_rules::actor_times* actor_trigger_times = nullptr;
		SRWLOCK actor_trigger_lock = SRWLOCK_INIT;
		DWORD actor_trigger_epoch = 0;
		bool actor_trigger_trace = false;
		void(__cdecl* original_retire_times)() = nullptr;
		using initialize_times_t = void(__fastcall*)(void*, void*, void*, float);
		using tick_times_t = void(__fastcall*)(void*, void*, float);
		initialize_times_t original_initialize_times = nullptr;
		tick_times_t original_tick_times = nullptr;
		using start_move_t = void(__fastcall*)(void*, void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
		start_move_t original_start_move = nullptr;

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

		DWORD igb_mismatch()
		{
			if (const DWORD va = first_mismatch(igb_sites))
			{
				return va;
			}
			if (const DWORD va = first_mismatch(igb_references))
			{
				return va;
			}
			if (const DWORD va = first_mismatch(igb_guards))
			{
				return va;
			}
			return bytes_match(find_next_200.va, find_next_200.hex) ? 0 : find_next_200.va;
		}

		DWORD styles_mismatch()
		{
			return style_first_mismatch([](const DWORD va, const std::string_view hex) { return bytes_match(va, hex); });
		}

		DWORD curves_mismatch()
		{
			return curve_first_mismatch([](const DWORD va, const std::string_view hex) { return bytes_match(va, hex); });
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

		// The IGB info cache: its code patched for `records`, the getter (its constructor's `this`, the vtable
		// store, the returned object), the atexit thunk and the three findNext calls pointed at a zero-filled
		// block and a clone of the DLL's. The game builds the block with its own constructor at the first call
		// of the getter (from the CPrecacheMgr constructor, in its start-up), as it did the static, and tears
		// it down at exit with its own destructor.
		bool raise_igb(const int records)
		{
			constexpr const char* stays = "the IGB cache stays at 200 records";
			if (!base)
			{
				logger::write("limits: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", stays);
				return false;
			}
			if (const DWORD va = igb_mismatch())
			{
				logger::write("limits: XMen2.exe doesn't have the expected code at 0x%08lX (not the retail build?) - %s", va, stays);
				return false;
			}
			DWORD built = 0;
			if (!read_dword(igb_init_guard, built) || (built & 1))
			{
				logger::write("limits: the game has built its IGB cache already (0x7bf6c8 = 0x%08lX) - %s", built, stays);
				return false;
			}
			const auto layout = igb_layout_for(records);
			const auto clone_size = hex_size(find_next_200.hex);
			auto* object = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, layout.object_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)); // zero-filled: the pool constructor clears seven bitmap dwords only
			auto* clone = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, clone_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			const auto release = [&]
			{
				if (object) VirtualFree(object, 0, MEM_RELEASE);
				if (clone) VirtualFree(clone, 0, MEM_RELEASE);
			};
			if (!object || !clone)
			{
				logger::write("limits: can't allocate the IGB cache (error %lu) - %s", GetLastError(), stays);
				release();
				return false;
			}
			std::memcpy(clone, at(find_next_200.va), clone_size); // compared with the retail bytes above
			finish_clone(clone, find_next_200, layout, address_of(clone));
			DWORD old_protection = 0;
			if (!VirtualProtect(clone, clone_size, PAGE_EXECUTE_READ, &old_protection))
			{
				logger::write("limits: can't make the findNext clone executable (error %lu) - %s", GetLastError(), stays);
				release();
				return false;
			}
			FlushInstructionCache(GetCurrentProcess(), clone, clone_size);

			const auto writes = igb_writes(layout, address_of(object), address_of(clone));
			if (!patch(writes, stays))
			{
				release();
				return false;
			}
			igb_cap = records;
			igb_live_va = address_of(object) + 4 + layout.live;
			igb_installed = true;
			logger::write("limits: IGB cache raised from 200 to %d records - CIGBInfoCache2 moves from 0x7b7fb0 to 0x%08lX (%lu bytes, id mask 0x%lX), built and torn down by the game's own code; "
			              "live count at 0x%08lX (was 0x7bf39c); %zu fields, 4 references and 3 calls patched, bitset<200>::findNext cloned to 0x%08lX with its %zu",
			              records, address_of(object), layout.object_size, layout.id_mask, igb_live_va, igb_sites.size(), address_of(clone), find_next_200.fields.size());
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

		bool raise_styles(const int capacity, const int events)
		{
			if (!base || styles_mismatch()) return false;
			DWORD existing = 0;
			if (!read_dword(style_manager_pointer, existing) || existing)
			{
				logger::write("limits: style manager already exists; FightStyles and StyleEvents refused");
				return false;
			}
			const auto layout = manager_layout(capacity, events);
			const bool needs_caves = capacity > 32 || events != stock_style_events;
			auto* storage = needs_caves ? static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 6*128, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE)) : nullptr;
			if (needs_caves && !storage) return false;
			auto plan = style_manager_plan::make(layout, address_of(reinterpret_cast<const void*>(&construct_style_ring)),
				address_of(reinterpret_cast<const void*>(&push_style_ring)), address_of(storage),
				address_of(&style_refusals), address_of(&event_refusals));
			for (unsigned i=0;i<plan.code.size();++i)
				if (!plan.code[i].empty()) std::memcpy(storage+i*128,plan.code[i].data(),plan.code[i].size());
			if (storage) {
				FlushInstructionCache(GetCurrentProcess(),storage,6*128);
				DWORD protection = 0;
				if (!VirtualProtect(storage,6*128,PAGE_EXECUTE_READ,&protection)) { VirtualFree(storage,0,MEM_RELEASE); return false; }
			}
			// Guard the singleton in the transaction itself, immediately before the first write.
			plan.transaction.guards.push_back({style_manager_pointer,{0,0,0,0},"style manager must not exist"});
			plan.transaction.publish = [layout]() {
				style_cap=layout.capacity; event_cap=layout.events.capacity;
				published_manager=layout;
			};
			plan.transaction.abandon = [storage]() { if (storage) VirtualFree(storage,0,MEM_RELEASE); };
			patch_transaction::transaction transaction;
			transaction.add(std::move(plan.transaction));
			const auto result=transaction.commit();
			logger::write("limits: style manager styles %d; events %d; size 0x%lX; %s: %s",
				capacity,events,layout.manager_size,patch_transaction::name(result),transaction.report().c_str());
			if (result==patch_transaction::outcome::rollback_failed) {
				published_manager.reset(); game_version::refuse_startup();
			}
			return result==patch_transaction::outcome::committed;
		}

		// The effect curve pool: its code patched for `capacity`, the getter (and the preset loader's
		// copy of it) and the atexit thunk pointed at a zero-filled block of the DLL. The game builds the
		// block with its own constructor at the first call of the getter, as it did the static, and tears
		// it down at exit.
		bool raise_curves(const int capacity)
		{
			constexpr const char* stays = "the effect curve pool stays at 900 curves";
			if (!base)
			{
				logger::write("limits: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", stays);
				return false;
			}
			if (const DWORD va = curves_mismatch())
			{
				logger::write("limits: XMen2.exe doesn't have the expected code at 0x%08lX (not the retail build?) - %s", va, stays);
				return false;
			}
			DWORD built = 0;
			if (!read_dword(curve_init_guard, built) || (built & 1))
			{
				logger::write("limits: the game has built its effect curve pool already (0x706678 = 0x%08lX) - %s", built, stays);
				return false;
			}
			const auto layout = curve_layout_for(capacity);
			void* object = VirtualAlloc(nullptr, layout.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); // zero-filled, as the static was
			if (!object)
			{
				logger::write("limits: can't allocate %lu bytes for the effect curve pool (error %lu) - %s", layout.size, GetLastError(), stays);
				return false;
			}
			const auto writes = curve_writes(layout, address_of(object));
			if (!patch(writes, stays))
			{
				VirtualFree(object, 0, MEM_RELEASE);
				return false;
			}
			curve_cap = capacity;
			curve_live_va = address_of(object) + layout.live;
			logger::write("limits: effect curve pool raised from 900 to %d curves - the pool moves from 0x6fe4b8 to 0x%08lX (%lu bytes), built and torn down by the game's own code; "
			              "curves in use at 0x%08lX (was 0x706440); %zu fields and %zu references patched, bitset<900>::findNext in place; the 140 presets stay",
			              capacity, address_of(object), layout.size, curve_live_va, curve_sites.size(), curve_references.size());
			return true;
		}

		void* __fastcall construct_move(void* self, void*)
		{
			using namespace move_triggers_rules;
			// The only native constructor caller operates on this pool; checked before patching.
			const auto index = move_triggers_rules::slot(fight_moves::descriptor(), address_of(self));
			if (!index)
			{
				logger::write("limits: MoveTriggers: unexpected move address 0x%08lX - refusing unsafe storage", address_of(self));
				RaiseFailFastException(nullptr, nullptr, 0);
				return nullptr;
			}
			reinterpret_cast<constructor_t>(at(constructor))(self, nullptr);
			auto* array = move_trigger_arrays + *index * move_trigger_cap;
			std::memset(array, 0, static_cast<std::size_t>(move_trigger_cap) * sizeof(DWORD));
			write_dword(address_of(self) + array_pointer, address_of(array));
			return self;
		}

		move_triggers_rules::actor_times* actor_times_for(void* actor)
		{
			using namespace move_triggers_rules;
			AcquireSRWLockExclusive(&actor_trigger_lock);
			auto* slot = times_for(std::span(actor_trigger_times, timer_slots), address_of(actor));
			ReleaseSRWLockExclusive(&actor_trigger_lock);
			if (!slot) logger::write_once("move-trigger-cache-full", "limits: MoveTriggers: actor timing cache full (%lu live addresses); actor 0x%08lX uses native nineteen triggers", timer_slots, address_of(actor));
			return slot;
		}

		void trace_actor_times(const char* event, void* actor)
		{
			if (!actor_trigger_trace) return;
			AcquireSRWLockExclusive(&actor_trigger_lock);
			const auto occupancy = std::count_if(actor_trigger_times, actor_trigger_times + move_triggers_rules::timer_slots, [](const auto& t) { return t.actor != 0; });
			ReleaseSRWLockExclusive(&actor_trigger_lock);
			logger::write("MoveTriggers trace: %s actor=%08lX generation=%08lX epoch=%lu occupancy=%zu thread=%lu", event, address_of(actor), dword_at(actor, 0x1c), actor_trigger_epoch, static_cast<std::size_t>(occupancy), GetCurrentThreadId());
		}

		void retire_times_only()
		{
			if (!actor_trigger_times) return;
			AcquireSRWLockExclusive(&actor_trigger_lock);
			move_triggers_rules::retire_times(std::span(actor_trigger_times, move_triggers_rules::timer_slots));
			++actor_trigger_epoch;
			ReleaseSRWLockExclusive(&actor_trigger_lock);
			if (actor_trigger_trace) logger::write("MoveTriggers trace: arena retired epoch=%lu occupancy=0 thread=%lu", actor_trigger_epoch, GetCurrentThreadId());
		}

		void __cdecl retire_actor_times()
		{
			original_retire_times(); // includes destruction callbacks; their timers remain available until it returns
			retire_times_only();
		}

		// The MoveTriggers raise as a contribution to the startup transaction. With `teardown_owned_elsewhere` the
		// actor-arena teardown at 0x4613e0 is not wrapped here (the entity-capacity AllocatorShutdown replaces that
		// function and calls retire_move_trigger_times() after the teardown, plan section 0 P1).
		bool contribute_move_triggers(patch_transaction::transaction& transaction, const int capacity, const bool teardown_owned_elsewhere)
		{
			using namespace move_triggers_rules;
			constexpr const char* stays = "moves stay at 19 triggers";
			if (!base) return false;
			const DWORD mismatch = move_triggers_rules::mismatch(bytes_match, fight_moves::installed());
			if (mismatch)
			{
				logger::write("limits: MoveTriggers: unexpected code at 0x%08lX - %s", mismatch, stays);
				return false;
			}
			DWORD built = 0;
			if (!read_dword(pool_pointer, built) || built)
			{
				logger::write("limits: MoveTriggers: move pool already exists - %s", stays);
				return false;
			}
			const DWORD slots = fight_moves::descriptor().count;
			const std::size_t bytes = slots * static_cast<std::size_t>(capacity) * sizeof(DWORD);
			auto* arrays = static_cast<DWORD*>(VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			auto* timers = static_cast<actor_times*>(VirtualAlloc(nullptr, timer_slots * sizeof(actor_times), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (!arrays || !timers || !code)
			{
				logger::write("limits: MoveTriggers: cannot allocate side arrays/thunks (error %lu) - %s", GetLastError(), stays);
				if (arrays) VirtualFree(arrays, 0, MEM_RELEASE);
				if (timers) VirtualFree(timers, 0, MEM_RELEASE);
				if (code) VirtualFree(code, 0, MEM_RELEASE);
				return false;
			}
			using namespace move_triggers_runtime;
			const scheduler_hooks hooks{address_of(reinterpret_cast<const void*>(&scheduled_times)), address_of(reinterpret_cast<const void*>(&source_times)),
			                            address_of(reinterpret_cast<const void*>(&capture_times)), address_of(reinterpret_cast<const void*>(&clear_times)),
			                            address_of(reinterpret_cast<const void*>(&initialize_times)), address_of(reinterpret_cast<const void*>(&tick_times)),
			                            address_of(reinterpret_cast<const void*>(&start_move)), teardown_owned_elsewhere ? 0 : address_of(reinterpret_cast<const void*>(&retire_actor_times))};
			const auto plan = make_plan(capacity, address_of(reinterpret_cast<const void*>(&construct_move)), address_of(code), &hooks);
			std::memcpy(code, plan.code.data(), plan.code.size());
			DWORD protection = 0;
			if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &protection))
			{
				logger::write("limits: MoveTriggers: cannot protect thunks (error %lu) - %s", GetLastError(), stays);
				VirtualFree(arrays, 0, MEM_RELEASE);
				VirtualFree(timers, 0, MEM_RELEASE);
				VirtualFree(code, 0, MEM_RELEASE);
				return false;
			}
			FlushInstructionCache(GetCurrentProcess(), code, plan.code.size());
			move_trigger_cap = capacity;
			move_trigger_arrays = arrays;
			actor_trigger_times = timers;
			original_initialize_times = reinterpret_cast<initialize_times_t>(plan.original_initialize);
			original_tick_times = reinterpret_cast<tick_times_t>(plan.original_tick);
			original_start_move = reinterpret_cast<start_move_t>(plan.original_start);
			move_triggers_runtime::original_initialize = original_initialize_times;
			move_triggers_runtime::original_tick = original_tick_times;
			move_triggers_runtime::original_start = original_start_move;
			move_triggers_runtime::cap = capacity;
			move_triggers_runtime::lookup = &actor_times_for;
			move_triggers_runtime::notify = &trace_actor_times;
			actor_trigger_trace = ini::flag(L"Test", L"MoveTriggerTrace", false);
			original_retire_times = reinterpret_cast<void(__cdecl*)()>(plan.original_retire);
			patch_transaction::contribution c;
			c.owner = "limits.MoveTriggers";
			for (const auto& a : accesses) c.guards.push_back({a.va, patch_transaction::bytes_of(a.retail), "MoveTriggers handle access"});
			for (const auto& original : guards) { const auto g = descriptor_guard(original, fight_moves::installed()); c.guards.push_back({g.va, patch_transaction::bytes_of(g.hex), g.what ? g.what : "MoveTriggers guard"}); }
			c.guards.push_back({pool_pointer, {0, 0, 0, 0}, "MoveTriggers pool must still be null"});
			for (const auto& w : plan.writes)
			{
				patch_transaction::write out{w.va, {}, "MoveTriggers"};
				for (unsigned i = 0; i < w.size; ++i) out.bytes.push_back(static_cast<std::uint8_t>(w.value >> (8 * i)));
				c.writes.push_back(std::move(out));
			}
			c.publish = [capacity, bytes, slots, teardown_owned_elsewhere]
			{
				logger::write("limits: MoveTriggers raised from 19 to %d; %lu native move slots, %zu side-array bytes; all 10 handle accesses redirected; actor timing initialization, pause, dispatch and clear expanded with native arithmetic; native count, tags and event ownership preserved%s",
				              capacity, slots, bytes, teardown_owned_elsewhere ? "; timer retirement runs from the entity-capacity arena teardown" : "");
			};
			// Uncertified rollback: the redirected accesses may still point into the side arrays and thunks, so nothing is
			// released and the published state stays unset (startup is refused by the transaction's owner).
			c.retain = [stays]
			{
				logger::write("limits: MoveTriggers: the startup transaction could not be rolled back - %s; side arrays and thunks retained", stays);
			};
			c.abandon = [arrays, timers, code, stays]
			{
				logger::write("limits: MoveTriggers: the startup transaction did not commit - %s", stays);
				move_trigger_cap = stock_capacity;
				move_trigger_arrays = nullptr;
				actor_trigger_times = nullptr;
				original_initialize_times = nullptr;
				original_tick_times = nullptr;
				original_start_move = nullptr;
				original_retire_times = nullptr;
				move_triggers_runtime::cap = stock_capacity;
				VirtualFree(arrays, 0, MEM_RELEASE);
				VirtualFree(timers, 0, MEM_RELEASE);
				VirtualFree(code, 0, MEM_RELEASE);
			};
			// Every guard this contribution brings is checked here, read-only: a MoveTriggers-only mismatch keeps moves at
			// 19 triggers instead of refusing the whole startup transaction (and with it the LC seam).
			std::string why;
			if (!patch_transaction::verify_expectations(c, why))
			{
				logger::write("limits: MoveTriggers: unexpected code (%s) - %s", why.c_str(), stays);
				c.abandon();
				return false;
			}
			return transaction.add(std::move(c));
		}

		// E12 (address space): whether XMen2.exe runs large-address-aware (the flag the Legends Classic build sets on
		// its own copy of the exe; Windows reads it at process start, so there is no runtime switch), the highest user
		// address Windows reports and the highest 64 KB block it will actually reserve top-down (released at once).
		// Read only: nothing is written, so the log line is the same with every key off.
		void log_address_space(const HMODULE game)
		{
			const char* laa = "unknown";
			__try
			{
				const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const std::uint8_t*>(game) + dos->e_lfanew);
				if (dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE)
					laa = (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) ? "on" : "off";
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				laa = "unknown";
			}
			SYSTEM_INFO system{};
			GetSystemInfo(&system);
			void* top = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);
			const DWORD reservable = top ? address_of(top) + 0xffff : 0;
			if (top) VirtualFree(top, 0, MEM_RELEASE);
			logger::write("limits: address space - LAA %s (XMen2.exe header); highest user address 0x%08lX; highest reservable address 0x%08lX", laa,
			              address_of(system.lpMaximumApplicationAddress), reservable);
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

	bool contribute(patch_transaction::transaction& transaction, const HMODULE game, const bool teardown_owned_elsewhere)
	{
		// FightMoves publishes its verified layout before side arrays are sized.
		fight_moves::install(game);
		base = reinterpret_cast<std::uintptr_t>(game) == image_base ? reinterpret_cast<std::uint8_t*>(game) : nullptr;
		const auto move_triggers = ini_value(L"MoveTriggers");
		const int triggers = move_triggers_rules::capacity(move_triggers);
		if (move_triggers && triggers == move_triggers_rules::stock_capacity && parse_count(*move_triggers) != move_triggers_rules::stock_capacity)
			logger::write("limits: MoveTriggers must be 19..127 - moves stay at 19 triggers");
		if (triggers <= move_triggers_rules::stock_capacity) return false;
		return contribute_move_triggers(transaction, triggers, teardown_owned_elsewhere);
	}

	void retire_move_trigger_times()
	{
		retire_times_only();
	}

	// ModelPools, then the rows that read its published layout (W3-9 particle scratch, W3-4 effect templates, W3-7
	// primitives per effect), then the independent W3-5/W3-6 effect pools. Each reads its own key and is one
	// transaction; absent keys write nothing. Integration (w3/integrated): one order for both install paths.
	void install_effect_rows(const HMODULE game)
	{
		model_pools::install(game);
		particle_scratch::install(game);
		effect_templates::install(game);
		primitives_per_effect::install(game);
		effect_pools::install(game);
	}

	void install(const HMODULE game)
	{
		power_pools::install(game);
		base = reinterpret_cast<std::uintptr_t>(game) == image_base ? reinterpret_cast<std::uint8_t*>(game) : nullptr;
		if (game) log_address_space(game);
		const auto w2_items = item_capacity::installed_capacity();
		if (w2_items) item_cap = wanted_caps[2] = static_cast<int>(w2_items);
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
			motions_readable = !first_mismatch(motion_counter_guards(motion_pool::counter_guard_hex()));
			igb_readable = !first_mismatch(igb_counter_guards);
			if (igb_readable) igb_live_va = igb_live_retail;
			items_readable = w2_items || !items_mismatch();
			if (!styles_mismatch() && !first_mismatch(event_sites)) {
				published_manager=manager_layout(stock_fight_styles,stock_style_events);
			}
			if (!curves_mismatch()) curve_live_va = curve_object_retail + curve_layout_for(stock_effect_curves).live;
		}

		const auto actor_slots = ini_value(L"ActorSlots");
		const auto resource_names = ini_value(L"ResourceNames");
		const auto item_enhancements = ini_value(L"ItemEnhancements");
		const auto fight_styles = ini_value(L"FightStyles");
		const auto style_events = ini_value(L"StyleEvents");
		const auto effect_curves = ini_value(L"EffectCurves");
		const auto igb_records = ini_value(L"IGBCache");
		const auto move_triggers = ini_value(L"MoveTriggers"); // contributed to the startup transaction earlier (contribute())
		const auto fight_moves = ini_value(L"FightMoves"); // already installed before MoveTriggers prepared
		if (!actor_slots && !resource_names && !item_enhancements && !fight_styles && !style_events && !effect_curves && !move_triggers && !igb_records && !fight_moves)
		{
			install_effect_rows(game);
			if(w2_items)logger::write("limits: W2 owns the 4096-record item enhancement pool");
			else logger::write("limits: the game's own caps - 40 actor slots, 450 resource names, 375 item enhancements (no [Limits] in xml2-fix.ini)");
			return;
		}
		const auto chosen = decide(actor_slots ? std::optional<std::string_view>(*actor_slots) : std::nullopt,
		                           resource_names ? std::optional<std::string_view>(*resource_names) : std::nullopt,
		                           item_enhancements && !w2_items ? std::optional<std::string_view>(*item_enhancements) : std::nullopt,
		                           fight_styles ? std::optional<std::string_view>(*fight_styles) : std::nullopt,
		                           effect_curves ? std::optional<std::string_view>(*effect_curves) : std::nullopt,
		                           igb_records ? std::optional<std::string_view>(*igb_records) : std::nullopt);
		igb_requested = chosen.igb_records > stock_igb_records;
		wanted_caps = {chosen.resource_names, chosen.actor_slots, chosen.item_enhancements,
		               chosen.fight_styles, chosen.effect_curves, move_triggers_rules::capacity(move_triggers)};
		if (w2_items) wanted_caps[2] = static_cast<int>(w2_items);
		for (const auto& note : chosen.notes)
		{
			logger::write("limits: %s", note.c_str());
		}

		// The name table first: the actor table needs it.
		const bool names_raised = chosen.resource_names > stock_resource_names && raise_names(chosen.resource_names);

		// The IGB cache after the name table (its records take names there too) and on the same terms.
		if (chosen.igb_records > stock_igb_records)
		{
			if (names_raised && name_cap >= names_needed(chosen.actor_slots, chosen.igb_records))
			{
				raise_igb(chosen.igb_records);
			}
			else
			{
				logger::write("limits: the IGB cache stays at 200 records - %d records need a resource name table of at least %d names, and it wasn't raised", chosen.igb_records,
				              names_needed(chosen.actor_slots, chosen.igb_records));
			}
		}

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
		// DESIGN revision 2 section 7: actor cache, model pools, W3-9, W3-4/W3-7, effect pools, then styles.
		install_effect_rows(game);
		const auto manager_request=style_manager_plan::choose(
			fight_styles?std::optional<std::string_view>(*fight_styles):std::nullopt,
			style_events?std::optional<std::string_view>(*style_events):std::nullopt);
		wanted_events=manager_request.events;
		if (manager_request.requested) {
			manager_verified=manager_request.valid && raise_styles(manager_request.styles,manager_request.events);
			if (!manager_verified) logger::write("limits: FightStyles and StyleEvents unverified; shared manager unchanged (FightStyles 20..127, StyleEvents 1065..8192; stock 19/1064)");
		}
		if (chosen.effect_curves > stock_effect_curves) raise_curves(chosen.effect_curves);
		// W2 owns the item enhancement pool when its layout is installed (item_capacity); the stock raise stays out.
		if (!w2_items && chosen.item_enhancements > stock_item_enhancements && base) raise_items(chosen.item_enhancements);
	}

	void register_resource_agreement()
	{
		power_pools::register_agreement();
		// Not the game (XMen2.exe isn't at 0x400000): nothing was raised and nothing can be patched, like every other
		// module that returns before registering there - a "requested but not installed" entry would make the
		// agreement demand its network exclusion hook in a process that has none and refuse that process's startup
		// (found by xml2_test's pipe child at the beta 8 integration).
		if (!base)
			return;
		limits_agreement_rules::register_rules(wanted_caps,
			{name_cap, actor_cap, item_cap, style_cap, curve_cap, move_trigger_cap},
			[](auto id, auto revision, auto values, bool verified) {
				gameplay_agreement::register_rule(id, revision, values, verified);
			}, manager_verified);
		limits_agreement_rules::register_events(wanted_events,event_cap,manager_verified,
			[](auto id, auto revision, auto values, bool verified) {
				gameplay_agreement::register_rule(id,static_cast<std::uint16_t>(revision),values,verified);
			});
	}

	bool igb_cache_requested()
	{
		return igb_requested;
	}

	bool igb_cache_installed()
	{
		return igb_installed;
	}

	std::array<std::uint32_t, 4> igb_cache_values()
	{
		return {igb_installed ? 1u : 0u, static_cast<std::uint32_t>(igb_cap), static_cast<std::uint32_t>(name_cap), 0u};
	}

	unsigned actor_capacity() { return static_cast<unsigned>(actor_cap); }

    pool_diagnostics diagnostic_sources()
    {
        // The published style manager (W3-1/W3-2): retail when stock, the raised layout after its transaction.
        const bool styles=published_manager.has_value();
        const auto l=styles?*published_manager:manager_layout(stock_fight_styles,stock_style_events);
        return {
            {styles?style_manager_pointer:0,l.count,static_cast<DWORD>(l.capacity),0,true,false},
            {styles?style_manager_pointer:0,l.auxiliary+l.events.live,static_cast<DWORD>(l.events.capacity),0,true,false},
            {curve_live_va,0,static_cast<DWORD>(curve_cap),curve_init_guard,false,false},
            {styles?style_manager_pointer:0,l.styles,l.bitmap,style_record_size,0xc60,static_cast<DWORD>(l.capacity)}
        };
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
				motions = read(*pool + motion_pool::layout().live);
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
		std::optional<DWORD> styles, events;
		if (published_manager)
		{
			if (const auto manager = read(style_manager_pointer); manager && *manager) {
				styles = read(*manager + published_manager->count);
				events = read(*manager + published_manager->auxiliary + published_manager->events.live);
			}
		}
		const auto residency=resident_stats::status();
		const bool observing_refusals=published_manager && (published_manager->capacity>32 || published_manager->events.capacity!=stock_style_events);
        return (residency.empty()?std::string{}:residency+"; ")+"actors " + counter(read(actor_live_va), actor_cap) + "; names " + counter(names, name_cap) + "; motions " + counter(motions, static_cast<int>(motion_pool::layout().capacity)) + "; igb " +
		       counter(read(igb_live_va), igb_cap) + "; items " + counter(items, item_cap) + "; styles " + counter(styles, style_cap) + "; events " + counter(events,event_cap) + "; style_refused " + (observing_refusals?std::to_string(style_refusals):"-") + "; event_refused " + (observing_refusals?std::to_string(event_refusals):"-") + "; curves " +
		       counter(read(curve_live_va), curve_cap) + "; " + fight_moves::status() + "; move_triggers " + std::to_string(move_trigger_cap) + "; " + effect_templates::status() + "; " + primitives_per_effect::status() + "; " + effect_pools::status() + "; " + script_pools::status() + power_pools::status();
	}
}
