#include "xml1_cling.hpp"

#include "entity_capacity.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "mold_query_rules.hpp"
#include "xml1_cling_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace xml1_cling
{
	namespace
	{
		using namespace xml1_cling_rules;
		using u32 = std::uint32_t;
		std::atomic<int> state{-1}; // 1 installed; -1 the game's own; 0 asked for but nothing patched
		bool trace = false;

		template<class T> T at(const void* p, u32 off) { T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p) + off, sizeof v); return v; }
		template<class T> void put(void* p, u32 off, T v) { std::memcpy(static_cast<unsigned char*>(p) + off, &v, sizeof v); }
		template<class T> T fn(u32 va) { return reinterpret_cast<T>(static_cast<std::uintptr_t>(va)); }
		template<class T> T slot(const void* object, u32 offset) { return at<T>(at<const void*>(object, 0), offset); }

		using trace_t = bool(__fastcall*)(void* actor, void*, const float* start, const float* delta, int flag, float* shift, void** contact);
		using holder_bool_t = bool(__fastcall*)(void** contact, void*);
		using holder_ptr_t = void*(__fastcall*)(void** contact, void*);
		using cast_t = void*(__cdecl*)(void* entity);
		using facing_t = float*(__cdecl*)(float* out, const float* angles);
		using action_t = void*(__fastcall*)(void* actor, void*, int action);
		using may_start_t = bool(__fastcall*)(void* move, void*, void* actor);
		using intern_t = void*(__fastcall*)(u32* key, void*, const char* name);
		using named_t = void*(__fastcall*)(void* actor, void*, const u32* key);
		using start_t = void(__fastcall*)(void* actor, void*, void* move, int flag);
		using vector_t = void(__fastcall*)(void* actor, void*, const float* v);
		using ground_t = void(__fastcall*)(void* actor, void*, const u32* handle);
		using flags_t = void(__fastcall*)(void* entity, void*, const u32* flags);
		using registry_t = void*(__cdecl*)();
		using find_t = void*(__fastcall*)(void* registry, void*, int* id);
		using attribute_t = const char*(__fastcall*)(void* node, void*, const char* name);

		// The handler objects: one word each, holding its eight callbacks (as the game's own registrations).
		std::array<void*, 8> decide_slots{}, backflip_slots{};
		void* decide_handler = nullptr;
		void* backflip_handler = nullptr;
		bool lookup_installed = false;
		std::array<handler_resolver, 8> additional_lookups{};
		u32 lookup_resume = lookup_resume_va;

		vec read_vec(const void* p, u32 off) { return {at<float>(p, off), at<float>(p, off + 4), at<float>(p, off + 8)}; }
		vec facing(void* actor)
		{
			float out[3]{};
			const float* angles = reinterpret_cast<const float*>(static_cast<unsigned char*>(actor) + actor_angles);
			const float* d = fn<facing_t>(facing_va)(out, angles);
			return d ? vec{d[0], d[1], d[2]} : vec{};
		}
		bool grounded(void* actor)
		{
			return at<u32>(actor, actor_ground) != 0 || (at<u32>(actor, actor_flags) & grounded_bit) != 0;
		}
		// The talent's rank as the game keeps it (CStats+0x20: 8-byte entries, the id in the first word, the rank in
		// the low six bits of the third byte, the count at +0xbc), the name through the talent registry (0x4c05b0,
		// vt+0x14) and its data node's `name` (0x5649c0), as balance.cpp reads might.
		int talent_rank(void* actor, const char* wanted)
		{
			void* stats = at<void*>(actor, actor_stats);
			if (!stats) return 0;
			void* registry = fn<registry_t>(registry_va)();
			if (!registry) return 0;
			int count = at<int>(stats, stats_talent_count);
			if (count > stats_talent_capacity) count = stats_talent_capacity;
			for (int i = 0; i < count; ++i)
			{
				const u32 entry = stats_talents + 8u * static_cast<u32>(i);
				int id = at<std::uint16_t>(stats, entry);
				void* talent = slot<find_t>(registry, 0x14)(registry, nullptr, &id);
				if (!talent) continue;
				void* node = at<void*>(talent, 8);
				if (!node) continue;
				const char* name = fn<attribute_t>(attribute_va)(node, nullptr, "name");
				if (name && _stricmp(name, wanted) == 0) return at<std::uint8_t>(stats, entry + 2) & 0x3f;
			}
			return 0;
		}
		void consume(u32* input) { if (input) input[input_flags / 4] |= consumed; }
		bool take_chain(void* actor, int action, bool check)
		{
			void* move = fn<action_t>(action_va)(actor, nullptr, action);
			if (!move) return false;
			if (check && !slot<may_start_t>(move, move_may_start)(move, nullptr, actor)) return false;
			put<void*>(actor, actor_next_move, move);
			return true;
		}

		// ch_clingwalldecide entry (0xda3a0): its only write lands in an object the PC does not share, so nothing.
		void __fastcall decide_entry(void*, void*, void*, void*, void*) {}
		// ch_clingwalldecide update (0xd9740).
		bool __fastcall decide_update(void*, void*, void* actor, u32* input)
		{
			if (!actor || !input) return false;
			reading r;
			r.jump_held = (input[0] & (1u << jump_bit)) != 0; // 0xe6f40 / XML2 0x4fc020: input+0, bit 6
			r.facing = facing(actor);
			r.grounded = grounded(actor);
			vec shift{};
			if (r.jump_held && (r.acrobatics = talent_rank(actor, talent_name)) >= min_acrobatics)
			{
				const vec delta = probe_delta(r.facing);
				void* contact = nullptr;
				const float* start = reinterpret_cast<const float*>(static_cast<unsigned char*>(actor) + actor_position);
				r.contact = fn<trace_t>(trace_va)(actor, nullptr, start, &delta.x, 0, &shift.x, &contact);
				if (r.contact)
				{
					r.wall_kind = fn<holder_bool_t>(is_level_va)(&contact, nullptr);
					if (!r.wall_kind)
					{
						void* e = fn<holder_ptr_t>(contact_entity_va)(&contact, nullptr);
						r.wall_kind = e && fn<cast_t>(tileent_cast_va)(e);
					}
					if (const auto* n = static_cast<const float*>(fn<holder_ptr_t>(contact_normal_va)(&contact, nullptr)))
						r.normal = {n[0], n[1], n[2]};
				}
			}
			const auto c = decide(r);
			if (c == choice::cling)
			{
				const float angles[3]{0.f, 0.f, yaw_into(r.normal)};
				slot<vector_t>(actor, entity_set_angles)(actor, nullptr, angles);
				const vec p = read_vec(actor, actor_position);
				const float position[3]{p.x + shift.x, p.y + shift.y, p.z + shift.z};
				slot<vector_t>(actor, entity_set_position)(actor, nullptr, position);
				consume(input);
				// The original takes the chain without a check (an absent chain leaves the next move empty).
				put<void*>(actor, actor_next_move, fn<action_t>(action_va)(actor, nullptr, action_special));
				if (trace)
					logger::write("wall cling: actor %08X clings: normal %.3f/%.3f/%.3f yaw %.3f shift %.1f/%.1f/%.1f", at<u32>(actor, 0x1c),
					              r.normal.x, r.normal.y, r.normal.z, angles[2], shift.x, shift.y, shift.z);
				return true;
			}
			if (trace && r.contact)
				logger::write("wall cling: actor %08X no wall: kind %d normal %.3f/%.3f/%.3f facing %.3f/%.3f", at<u32>(actor, 0x1c),
				              r.wall_kind ? 1 : 0, r.normal.x, r.normal.y, r.normal.z, r.facing.x, r.facing.y);
			if (r.jump_held && take_chain(actor, action_move, true)) { consume(input); return true; }
			if (r.grounded)
			{
				consume(input);
				put<void*>(actor, actor_next_move, fn<action_t>(action_va)(actor, nullptr, action_special2));
				return true;
			}
			return false;
		}
		// ch_clingwallbackflip entry (0xd9590): launch back and up, leave the ground.
		void __fastcall backflip_entry(void*, void*, void* actor, void*, void*)
		{
			if (!actor) return;
			const vec v = backflip_velocity(facing(actor));
			fn<vector_t>(velocity_va)(actor, nullptr, &v.x);
			fn<ground_t>(ground_va)(actor, nullptr, reinterpret_cast<const u32*>(static_cast<std::uintptr_t>(null_handle_va)));
			u32 flags[4]{};
			std::memcpy(flags, static_cast<unsigned char*>(actor) + actor_flags, sizeof flags);
			flags[0] &= ~grounded_bit;
			fn<flags_t>(flags_va)(actor, nullptr, flags);
			if (trace) logger::write("wall cling: actor %08X backflip %.1f/%.1f/%.1f", at<u32>(actor, 0x1c), v.x, v.y, v.z);
		}
		// ch_clingwallbackflip update (0xd96e0): on landing, idle.
		bool __fastcall backflip_update(void*, void*, void* actor, u32*)
		{
			if (!actor || !grounded(actor)) return false;
			u32 key = 0;
			fn<intern_t>(intern_va)(&key, nullptr, "idle");
			void* idle = fn<named_t>(named_va)(actor, nullptr, &key);
			if (idle) fn<start_t>(start_va)(actor, nullptr, idle, 1);
			return true;
		}
		// The handlers live for the process; a teardown that asks them to delete themselves must not.
		void* __fastcall handler_destructor(void* self, void*, unsigned) { return self; }

		void* __cdecl lookup(const char* name)
		{
			if (!name) return nullptr;
			if (decide_handler && _stricmp(name, "ch_clingwalldecide") == 0) return &decide_handler;
			if (backflip_handler && _stricmp(name, "ch_clingwallbackflip") == 0) return &backflip_handler;
			for (const auto resolver : additional_lookups)
				if (resolver) if (void* handler = resolver(name)) return handler;
			return nullptr;
		}
		auto lookup_function = &lookup;
		// At 0x4fd860 (cdecl, the name at [esp+4]): ours, or the five displaced bytes and the game's own lookup.
		__declspec(naked) void lookup_hook()
		{
			__asm {
				push dword ptr [esp + 4]
				call dword ptr [lookup_function]
				add esp, 4
				test eax, eax
				jz native
				ret
			native:
				push esi
				mov esi, [esp + 8]
				jmp dword ptr [lookup_resume]
			}
		}

		bool live_matches(const guard& g)
		{
			__try { return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(g.va)), g.hex); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		// The accessors the capacity owner may bridge are checked on their own (stock or its jump, accessors_ok); the
		// table lists their stock bytes for the tests.
		const guard* first_live_mismatch()
		{
			for (const auto& g : guards)
			{
				if (g.va == contact_entity_va || g.va == is_level_va || g.va == contact_normal_va) continue;
				if (!live_matches(g)) return &g;
			}
			return nullptr;
		}
		// The entity-capacity owner bridges three of the contact accessors; accept stock bytes or its published jump.
		bool accessors_ok(xml1_hook_transaction::memory& memory)
		{
			for (const auto& e : std::array<mold_query_rules::entry, 3>{{
			         {contact_entity_va, "568bf1833e00", "contact entity"},
			         {is_level_va, "51568bf1833e00", "contact is the level"},
			         {contact_normal_va, "8b0185c07404", "contact normal"}}})
			{
				const auto target = entity_capacity::installed_target(e.va);
				std::array<unsigned char, 7> b{};
				if (!memory.read(e.va, b.data(), b.size()) || !mold_query_rules::entry_ok(target != 0, b.data(), e, target)) return false;
			}
			return true;
		}
	}

	bool register_lookup(const HMODULE game, handler_resolver resolver)
	{
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base) return false;
		// Each feature publishes one resolver at startup. Duplicate registration
		// is harmless; never replace another feature's resolver.
		auto destination = additional_lookups.end();
		if (resolver)
		{
			destination = std::find(additional_lookups.begin(), additional_lookups.end(), resolver);
			if (destination == additional_lookups.end())
				destination = std::find(additional_lookups.begin(), additional_lookups.end(), nullptr);
			if (destination == additional_lookups.end()) return false;
		}
		std::vector<unsigned char> jump(5);
		jump[0] = 0xe9;
		const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&lookup_hook) - (lookup_va + 5));
		std::memcpy(jump.data() + 1, &rel, 4);
		xml1_hook_transaction::memory memory;
		if (lookup_installed)
		{
			std::array<unsigned char, 5> actual{};
			if (!memory.read(lookup_va, actual.data(), actual.size()) || !std::equal(actual.begin(), actual.end(), jump.begin())) return false;
		}
		if (!lookup_installed)
		{
			if (!live_matches(guards[0])) return false;
			const std::vector<xml1_hook_transaction::patch> writes{{lookup_va, {retail_entry.begin(), retail_entry.end()}, jump}};
			bool safe = true;
			if (!xml1_hook_transaction::apply(memory, writes, &safe))
			{
				if (!safe) game_version::refuse_startup();
				return false;
			}
			lookup_installed = true;
		}
		if (resolver) *destination = resolver;
		return true;
	}

	void install(const HMODULE game)
	{
		state = -1;
		const auto value = ini::text(L"Game", L"Xml1WallCling").value_or("");
		const auto wanted = parse(value);
		if (wanted == setting::native) return;
		state = 0;
		trace = ini::flag(L"Test", L"ClingTrace", false);
		const char* keep = " - the acrobat moves keep the game's default handler";
		if (wanted == setting::invalid)
		{
			logger::write("wall cling: [Game] Xml1WallCling=%s is neither xml1 nor xml2%s", value.c_str(), keep);
			return;
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("wall cling: XMen2.exe isn't loaded at 0x400000 (not the game?)%s", keep);
			return;
		}
		if (const guard* g = first_live_mismatch())
		{
			logger::write("wall cling: 0x%08lX isn't the retail code (%s)%s", static_cast<unsigned long>(g->va), g->what, keep);
			return;
		}
		xml1_hook_transaction::memory memory;
		if (!accessors_ok(memory))
		{
			logger::write("wall cling: the contact accessors (0x4a9590/0x4a95c0/0x4a9600) are neither retail nor the entity-capacity bridges%s", keep);
			return;
		}
		std::memcpy(decide_slots.data(), reinterpret_cast<const void*>(static_cast<std::uintptr_t>(default_vtable_va)), sizeof decide_slots);
		backflip_slots = decide_slots;
		decide_slots[0] = backflip_slots[0] = reinterpret_cast<void*>(&handler_destructor);
		decide_slots[1] = reinterpret_cast<void*>(&decide_entry);
		decide_slots[6] = reinterpret_cast<void*>(&decide_update);
		backflip_slots[1] = reinterpret_cast<void*>(&backflip_entry);
		backflip_slots[6] = reinterpret_cast<void*>(&backflip_update);
		decide_handler = decide_slots.data();
		backflip_handler = backflip_slots.data();
		if (register_lookup(game, nullptr))
		{
			state = 1;
			logger::write("wall cling: xml1 ([Game] Xml1WallCling=xml1): ch_clingwalldecide and ch_clingwallbackflip answer the FightMove handler "
			              "lookup (0x4fd860); wall test %.0f units, |n.z| < %.1f, facing < %.1f; back-flip %.0f back, %.0f up",
			              probe_length, flat_limit, facing_limit, -launch_back, launch_up);
		}
		else
		{
			decide_handler = backflip_handler = nullptr;
			logger::write("wall cling: shared lookup patch refused%s", keep);
		}
	}

	std::string status()
	{
		const int s = state.load();
		return std::string("wall cling ") + (s == 1 ? "xml1" : s == -1 ? "game's own" : "unavailable");
	}
	bool requested() { return state.load() != -1; }
	bool installed() { return state.load() == 1; }
	bool verified() { return !requested() || installed(); }
}
