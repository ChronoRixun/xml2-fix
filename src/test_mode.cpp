#include "test_mode.hpp"
#include "test_mode_rules.hpp"
#include "test_input.hpp"
#include "test_state_rules.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "mold_query.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace test_mode
{
	namespace
	{
		using u32 = std::uint32_t;
		using vec = std::array<float, 3>;
		using predicate = bool(__fastcall*)(void*, void*);
		template<class T> T fn(u32 address) { return reinterpret_cast<T>(address); }
		template<class T> T at(const void* p, u32 offset) {
			T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p) + offset, sizeof(v)); return v;
		}
		xml1_hook_transaction::memory memory;
		bool enabled = false;
		bool notice_installed = false;
		std::atomic<bool> invulnerable{false};
		std::atomic<unsigned> kills{0}, moves{0};
		std::array<unsigned char, 3> keys = test_mode_rules::default_keys;
		test_mode_rules::edges edges;
		predicate original_invulnerable = nullptr;
		using popup_text = void(__fastcall*)(void*, void*, const char*);
		void __fastcall testing_popup_text(void* self, void*, const char* text)
		{
			if (game_version::take_test_mode_refusal()) {
				text = game_version::test_mode_reason();
				logger::write("test mode: native network-error dialog explains [TestMode]");
			}
			fn<popup_text>(0x5e9e90)(self, nullptr, text);
		}

		void* resolve(u32 handle) { return fn<void*(__fastcall*)(const u32*, void*)>(0x4654b0)(&handle, nullptr); }
		bool __fastcall protected_actor(void* actor, void*)
		{
			// No actor flag or deadline is changed: saves and scripted protection keep
			// their original values, including when this option is switched off.
			if (invulnerable.load() && at<u32>(actor, 0) == 0x682ccc &&
			    test_mode_rules::party_member(at<u32>(actor, 4), at<int>(actor, 0x3d4))) return true;
			return original_invulnerable(actor, nullptr);
		}
		bool matches(const limits_rules::guard& g)
		{
			std::vector<unsigned char> bytes(g.hex.size() / 2);
			return memory.read(g.va, bytes.data(), bytes.size()) && limits_rules::matches(bytes.data(), g.hex);
		}
		bool context_ready()
		{
			// Both input and gameplay run on the game's thread. Reject online, loads,
			// cutscenes, menus and script locks before following any entity handles.
			test_state_rules::reader<xml1_hook_transaction::memory> r(memory);
			const auto& d = entity_capacity::installed();
			r.table = {d.world_object, d.world_core.entities, d.world_core.live_bits, d.world_core.handles, d.world};
			const auto s = r.read(GetTickCount64());
			const auto online = r.get<unsigned char>(0xa53478);
			bool living_party = false;
			for (const auto& actor : s.party) if (actor.binding == "party_member" && actor.alive() == true) living_party = true;
			return living_party && online == 0 && s.mode == "in-zone" && s.loading == false &&
			       s.menu_open == false && s.popup == false && s.conversation == false && s.script_controls_locked == false;
		}
		void notice(const char* text)
		{
			logger::write("test mode: %s", text);
			using getter = void*(__cdecl*)();
			using message = int(__fastcall*)(void*, void*, const char*, float, float, float, float, float, int);
			__try {
				void* hud = fn<getter>(0x59ee20)();
				if (hud && at<u32>(at<void*>(hud, 0), 0x30) == 0x59cdb0)
					fn<message>(0x59cdb0)(hud, nullptr, text, 5.f, 1.f, 1.f, 1.f, 1.f, 3);
			} __except (EXCEPTION_EXECUTE_HANDLER) {}
		}
		std::vector<u32> entities()
		{
			std::vector<u32> result;
			const auto& d = entity_capacity::installed();
			test_state_rules::reader<xml1_hook_transaction::memory> r(memory);
			for (u32 i = 0; i < d.world; ++i) {
				const auto bits = r.get<u32>(d.world_object + d.world_core.live_bits + 4 * (i / 32));
				if (!bits || !(*bits & (1u << (i % 32)))) continue;
				const auto h = r.get<u32>(d.world_object + d.world_core.handles + 4 * i);
				if (h && *h) result.push_back(*h);
			}
			return result;
		}
		void kill_actor(void* actor, u32 attacker)
		{
			// The hit built by killEntity's 0x499740, with a party attacker instead
			// of zero so the native onDeath path credits XP and kill counters.
			using attack = void*(__fastcall*)(void*, void*, u32, float, short, u32, u32, u32, unsigned char, const float*, const float*, const float*);
			using take_hit = void(__fastcall*)(void*, void*, void*, u32, u32, u32);
			std::array<unsigned char, 0x6c> hit{};
			const vec zero{};
			fn<attack>(0x429860)(hit.data(), nullptr, attacker, 0.f, 0, 1, 0x80, 0, 1, zero.data(), zero.data(), zero.data());
			hit[0x35] = (hit[0x35] & 0x8f) | 8;
			const u32 direct = 0x10000000; const float amount = 1000000.f;
			std::memcpy(hit.data()+0x10, &direct, 4); std::memcpy(hit.data()+4, &amount, 4);
			at<take_hit>(at<const void*>(actor, 0), 0xb0)(actor, nullptr, hit.data(), 0, 1, 1);
		}
		void kill_enemies()
		{
			unsigned killed = 0, skipped = 0;
			u32 attacker = 0;
			fn<u32*(__cdecl*)(u32*, const char*)>(0x4a1700)(&attacker, "_ACTIVE_HERO_");
			void* hero = resolve(attacker);
			if (!hero || at<u32>(hero, 0) != 0x682ccc ||
			    !test_mode_rules::party_member(at<u32>(hero, 4), at<int>(hero, 0x3d4))) {
				notice("[TestMode] No active party hero for kill credit."); return;
			}
			const auto candidates = entities(); // never chase newly spawned reinforcements
			for (u32 h : candidates) {
				if (!context_ready()) break; // a death script can lock input or change zones
				void* actor = resolve(h);
				if (!actor || at<u32>(actor, 0) != 0x682ccc) continue;
				const u32 flags = at<u32>(actor, 4);
				if (!(flags & 0xc0000000u) || !(at<float>(actor, 0x27c) > 0)) continue;
				test_state_rules::reader<xml1_hook_transaction::memory> reader(memory);
				const bool scripted = test_mode_rules::scripted_event(reader, at<u32>(actor, 0x29c)) ||
				                      test_mode_rules::scripted_event(reader, at<u32>(actor, 0x2a0)) ||
				                      test_mode_rules::scripted_event(reader, at<u32>(actor, 0x2ac));
				if (!test_mode_rules::killable(flags, at<float>(actor, 0x27c), original_invulnerable(actor, nullptr),
				                              at<unsigned char>(actor, 0x30c), at<unsigned char>(actor, 0x30d), scripted)) {
					logger::write("test mode: kept hostile %u (flags %08X life %02X boss %02X scripted %d)", h, flags,
					              at<unsigned char>(actor, 0x30c), at<unsigned char>(actor, 0x30d), int(scripted));
					++skipped; continue;
				}
				// killEntity's native vt+8 implementation. It constructs a direct hit
				// and enters the actor's damage/death path, including death targets.
				// Never write health, remove an entity, or override a pain/undying gate.
				kill_actor(actor, attacker);
				actor = resolve(h); // callbacks can retire the object
				if (!actor || !(at<float>(actor, 0x27c) > 0)) ++killed;
			}
			kills += killed;
			char text[144]; std::snprintf(text, sizeof(text), "[TestMode] Defeated %u enemies; left %u protected or scripted actors.", killed, skipped);
			notice(text);
		}
		struct query
		{
			void* value = nullptr;
			~query() { if (value) fn<void(__fastcall*)(void**, void*)>(0x4ab720)(&value, nullptr); }
		};
		bool floor_at(const vec& origin)
		{
			if (!mold_query::pools_ready()) return false;
			query q; u32 none = 0;
			fn<void(__fastcall*)(void**, void*, const u32*)>(0x4ab670)(&q.value, nullptr, &none);
			if (!q.value) return false;
			const auto vt = at<const void*>(q.value, 0);
			using property = void(__fastcall*)(void*, void*, int, int);
			using record = void(__fastcall*)(void*, void*, int, const void*);
			using segment = void(__fastcall*)(void*, void*, const float*, const float*, const float*);
			using trace = u32*(__fastcall*)(void*, void*, u32*);
			at<property>(vt, 0x40)(q.value, nullptr, 1, 1); // subtract nocollide
			at<record>(vt, 0x4c)(q.value, nullptr, 1, reinterpret_cast<void*>(0x70b83c)); // subtract actors
			vec from{origin[0], origin[1], origin[2] + 8.f}, to{origin[0], origin[1], origin[2] - 64.f};
			const vec extent{12.f, 12.f, 1.f};
			at<segment>(vt, 0x74)(q.value, nullptr, from.data(), to.data(), extent.data());
			u32 contact = 0; at<trace>(vt, 0x20)(q.value, nullptr, &contact);
			if (!contact) return false;
			const float normal_z = at<float>(reinterpret_cast<void*>(contact), 0x28);
			const float floor_z = at<float>(reinterpret_cast<void*>(contact), 0x38);
			return std::isfinite(normal_z) && std::isfinite(floor_z) && normal_z >= .65f &&
			       floor_z <= origin[2] + 8.f && floor_z >= origin[2] - 64.f;
		}
		void return_to_start()
		{
			// Authored default player starts first. The engine places the four living
			// heroes at these offsets; check every destination against collision first.
			constexpr vec offsets[]{{40,0,0}, {0,40,0}, {-40,0,0}, {0,-40,0}};
			const auto candidates = entities();
			for (int defaults = 1; defaults >= 0; --defaults) for (u32 h : candidates) {
				void* start = resolve(h);
				if (!start || at<u32>(start, 0) != 0x689e5c ||
				    ((at<unsigned char>(start, 0xda) & 2) != 0) != (defaults != 0)) continue;
				const auto origin = at<vec>(start, 0x20);
				bool safe = true;
				for (const auto& offset : offsets) {
					vec point{origin[0]+offset[0], origin[1]+offset[1], origin[2]};
					if (!floor_at(point)) { safe = false; break; }
				}
				if (!safe) continue;
				char script[64]; std::snprintf(script, sizeof(script), "moveHeroesToEnt('%d')", static_cast<std::int32_t>(h));
				if (!test_input::queue_script_on_game_thread(script)) { notice("[TestMode] Return refused: console queue busy."); return; }
				++moves;
				logger::write("test mode: return to player start %u at %.1f/%.1f/%.1f; four collision floor checks passed", h, origin[0], origin[1], origin[2]);
				notice("[TestMode] Returning party to the level start."); return;
			}
			notice("[TestMode] Return refused: no player start with verified floor for the party.");
		}
	}

	bool hotkeys_enabled() { return enabled; }
	void input_lost() { edges.disarm(); }
	void keyboard(const unsigned char* state)
	{
		if (!enabled) return;
		const auto pressed = edges.read(state, keys);
		if (!pressed[0] && !pressed[1] && !pressed[2]) return;
		if (!context_ready()) { logger::write("test mode: hotkeys ignored outside normal play"); return; }
		// One action per press even when several configured keys arrive together.
		if (pressed[1]) { invulnerable = !invulnerable.load(); notice(invulnerable ? "[TestMode] Party invulnerability ON." : "[TestMode] Party invulnerability OFF."); }
		else if (pressed[0]) kill_enemies();
		else if (pressed[2]) return_to_start();
	}
	std::string status()
	{
		return std::string("test mode hotkeys ") + (enabled ? "on" : "off") + "; party invulnerability " +
		       (invulnerable ? "on" : "off") + "; test kills " + std::to_string(kills.load()) + "; test returns " + std::to_string(moves.load());
	}
	void install(HMODULE game)
	{
		const bool forge = ini::flag(L"TestMode", L"ForgeEverywhere", false);
		const bool hotkeys = ini::flag(L"TestMode", L"Hotkeys", false);
		if (!forge && !hotkeys) return;
		if (!game_version::test_mode_active()) {
			logger::write("test mode: settings changed after network setup; restart to enable [TestMode]"); return;
		}
		logger::write("test mode: [TestMode] ForgeEverywhere=%d Hotkeys=%d; online host/join disabled", int(forge), int(hotkeys));
		if (reinterpret_cast<std::uintptr_t>(game) != 0x400000) { logger::write("test mode: unsupported image; testing adapters not installed"); return; }
		if (!notice_installed) {
			if (!matches({0x6a3344, "909e5e00", "native popup text slot"}) ||
			    !matches({0x5e9e90, "81ec04040000a1f8386f00", "native popup text setter"})) {
				logger::write("test mode: popup guard mismatch; testing adapters not installed"); return;
			}
			const xml1_hook_transaction::patch change{0x6a3344, xml1_hook_transaction::pointer_bytes(reinterpret_cast<void*>(0x5e9e90)),
			    xml1_hook_transaction::pointer_bytes(reinterpret_cast<void*>(&testing_popup_text))};
			bool safe = true;
			if (!xml1_hook_transaction::apply(memory, {change}, &safe)) {
				if (!safe) game_version::refuse_startup();
				logger::write("test mode: popup transaction failed; original code %s", safe ? "retained" : "not verified as restored"); return;
			}
			notice_installed = true;
		}
		if (!hotkeys) return;
		const wchar_t* names[]{L"KillEnemiesKey", L"InvulnerabilityKey", L"ReturnToStartKey"};
		for (std::size_t i = 0; i < keys.size(); ++i) if (auto text = ini::text(L"TestMode", names[i])) {
			keys[i] = test_mode_rules::function_key(*text);
			if (!keys[i]) { logger::write("test mode: invalid function key; hotkeys not installed"); return; }
		}
		if (keys[0] == keys[1] || keys[0] == keys[2] || keys[1] == keys[2]) {
			logger::write("test mode: duplicate function keys; hotkeys not installed"); return;
		}
		constexpr limits_rules::guard guards[]{
			{0x496a20, "568bf181be94020000000080bf", "invulnerability predicate prologue"},
			{0x4654b0, "568bf1e8682a0600", "generation handle resolver"},
			{0x499740, "83ec6ca154fa6700565050506a01", "killEntity native damage path"},
			{0x429860, "8b44241c8b5424145650", "native attack constructor"},
			{0x4a2560, "8b4c240483ec64556a00", "moveHeroesToEnt"},
			{0x67aaa0, "6830206800680c9e680068dc000000b9a43f7500", "player start class"},
			{0x498dd7, "68a46a68008d8e9c020000", "pain event field"},
			{0x498d9f, "68ac6a68008d8eac020000", "extreme death event field"},
			{0x498dbb, "689c4068008d8ea0020000", "death event field"},
			{0x467730, "a19c98720085c0753c", "event pool singleton"},
			{0x4676bc, "898e2427000089be282700008d964c220000", "event pool mask and generation table"},
			{0x467333, "8b710885f67507", "event script token"},
			{0x498ab5, "8a8e0c0300003bc30f95c0c0e004", "undying field"},
			{0x4a88e0, "8a860d0300008acbc0e106", "boss undying field"},
			{0x610d20, "8a8120040000c3", "online flag"},
			{0x5d8640, "8b819060080085c07404", "current menu accessor"},
			{0x4a36f0, "8b4c240483ec08566a00", "health accessor"},
			{0x4a1ac0, "8b4c240483ec10566a00", "position accessor"},
			{0x474d43, "814e0400001000", "setEnable uses property 20"},
			{0x59ee20, "64a1000000008a0d94618a00", "HUD getter"},
			{0x59cdb0, "56578bf983bf707908000a", "HUD notice"}
		};
		for (const auto& g : guards) if (!matches(g)) { logger::write("test mode: guard mismatch at %08lX; hotkeys not installed", g.va); return; }
		if (!mold_query::supported()) { logger::write("test mode: query layout not verified; hotkeys not installed"); return; }
		// Relocate the complete, position-independent 13-byte prologue. The last
		// instruction is CMP; a JMP preserves its flags for the native JNE.
		constexpr u32 site = 0x496a20, length = 13;
		xml1_hook_transaction::patch patch{site, std::vector<unsigned char>(length), std::vector<unsigned char>(length, 0x90)};
		if (!memory.read(site, patch.before.data(), length)) return;
		auto* bridge = static_cast<unsigned char*>(VirtualAlloc(nullptr, length+5, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
		if (!bridge) { logger::write("test mode: cannot allocate predicate bridge; hotkeys not installed"); return; }
		std::memcpy(bridge, patch.before.data(), length); bridge[length] = 0xe9;
		u32 delta = site + length - (reinterpret_cast<u32>(bridge) + length + 5);
		std::memcpy(bridge+length+1, &delta, 4);
		DWORD old = 0;
		if (!VirtualProtect(bridge, length+5, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), bridge, length+5)) {
			VirtualFree(bridge, 0, MEM_RELEASE); logger::write("test mode: cannot seal predicate bridge; hotkeys not installed"); return;
		}
		original_invulnerable = reinterpret_cast<predicate>(bridge);
		patch.after[0] = 0xe9; delta = reinterpret_cast<u32>(&protected_actor) - site - 5;
		std::memcpy(patch.after.data()+1, &delta, 4);
		bool safe = true;
		if (!xml1_hook_transaction::apply(memory, {patch}, &safe)) {
			if (safe) { VirtualFree(bridge, 0, MEM_RELEASE); original_invulnerable = nullptr; }
			else game_version::refuse_startup();
			logger::write("test mode: predicate transaction failed; original code %s", safe ? "retained" : "not verified as restored"); return;
		}
		edges.disarm(); enabled = true;
		logger::write("test mode: hotkeys installed (DirectInput %02X/%02X/%02X: kill/invulnerability/return)", keys[0], keys[1], keys[2]);
	}
}
