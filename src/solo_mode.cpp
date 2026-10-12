#include "solo_mode.hpp"
#include "solo_mode_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// The six AI sites (XMen2.exe, unpacked retail; default.xbe counterparts in brackets). Each test is the original's
// "the follower or the one it follows is solo"; the branch it takes is the one XMen2.exe already has.
//   0x51ee14 hero idle handler (CAIHeroIdleHandler vt+0, 0x51ea70) [0xf3410 at 0xf373e/0xf374d]: formation follow.
//            The PC tests only the leader's +0x3d8 bit 2 (the original's bit 3) before stopping the follower
//            (vt+0x154, the original's vt+0x14c); solo now takes that branch too.
//   0x51ebd3 same function [0xf3523/0xf3536]: the AI's bridge power for the leader (Iceman / Storm). Solo skips the
//            block, as the original does.
//   0x50b8c9 follow movement (0x50b7c0, called from 0x516f60) [0x104d90 at 0x104ea0/0x104eb0]: stop and end.
//   0x50c29e follow movement (0x50c210, called from CActorAIDecision vt+0x10 0x517370) [0x105340 at
//            0x1053d1/0x1053e1]: no test on the PC at all; solo stops the follower (vt+0x154) and returns 0.
//   0x51dc10 hero idle handler vt+8 [0xf2610]: returns 0 at once for a solo character.
//   0x526960 the follow-me command (AI manager vt+0x1c0 0x525330) [0xfb010, from 0xf9a50]: ignored for a solo
//            character.
// Regroup predicate (default.xbe 0x34bb0): the hero's own +0x3d8 bit 2 refuses; every living, seated peer of the
// hero's controller group must be reachable through the hero's navigator (+0x3e8; reset vt+4, query
// vt+0(position, 80, 200) == 2). Not ported: the attached-entity test on +0xc0 and the manual exit's on-screen
// test (their PC counterparts are unproven; docs/solo-mode.md).
namespace solo_mode
{
	namespace
	{
		using u32 = std::uint32_t;
		using namespace solo_mode_rules;

		template <class T> T at(const void* p, u32 off) { T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p) + off, sizeof v); return v; }
		template <class T> T fn(u32 a) { return reinterpret_cast<T>(static_cast<std::uintptr_t>(a)); }

		using resolve_t = void*(__fastcall*)(const u32*, void*);
		using lookup_t = u32*(__cdecl*)(u32*, const char*);
		using clock_t = float(__cdecl*)();
		using voice_t = void(__fastcall*)(void*, void*, int);
		using reset_t = void(__fastcall*)(void*, void*);
		using query_t = int(__fastcall*)(void*, void*, const float*, float, int, int);
		using getter_t = void*(__cdecl*)();
		using visible_t = bool(__fastcall*)(void*, void*, const float*, float, float*);

		constexpr u32 resolve_handle = 0x4654b0, lookup_name = 0x4a1700, game_clock = 0x45fe60, voice = 0x420db0;
		constexpr u32 camera_getter = 0x4457e0, camera_vtable = 0x684cc4, camera_visible_slot = 0x178; // XBE 0x51100, vt+0x15c
		constexpr u32 path_vtable = 0x689b78;          // CPath: vt+0 query 0x496280, vt+4 reset 0x492560
		constexpr u32 character_class_id = 0x70b840;
		constexpr u32 ai_flags = 0x3d8, controller = 0x3d4, navigator = 0x3e8, health = 0x27c, origin = 0x20;

		party state;
		bool was_requested = false, sites_installed = false;
		float last_tick = -1.0f;

		bool is_character(void* e)
		{
			__try
			{
				if (!e) return false;
				const auto* concrete = at<void*(__fastcall*)(void*, void*)>(at<const void*>(e, 0), 0)(e, nullptr);
				const u32 bit = at<u32>(reinterpret_cast<const void*>(character_class_id), 0) + 0x24;
				return (at<u32>(concrete, 0x14 + 4 * (bit >> 5)) & (1u << (bit & 31))) != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		u32 handle_of(void* e)
		{
			__try { return e ? at<u32>(e, 0x1c) : 0; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
		}

		void* resolve(u32 h)
		{
			__try
			{
				if (!h) return nullptr;
				void* e = fn<resolve_t>(resolve_handle)(&h, nullptr);
				return e && at<u32>(e, 0x1c) == h ? e : nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
		}

		void* find(const char* name)
		{
			__try
			{
				u32 h = 0;
				return fn<resolve_t>(resolve_handle)(fn<lookup_t>(lookup_name)(&h, name), nullptr);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
		}

		float now()
		{
			__try { return fn<clock_t>(game_clock)(); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return -1.0f; }
		}

		void say(void* character, int id)
		{
			__try { if (character) fn<voice_t>(voice)(character, nullptr, id); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}

		member describe(void* e)
		{
			member m;
			__try
			{
				if (!e || !is_character(e)) return m;
				m.handle = at<u32>(e, 0x1c);
				m.character = true;
				m.seated = (at<u32>(e, 4) & (1u << 20)) != 0 && at<int>(e, controller) != -1;
				m.human = (at<unsigned char>(e, ai_flags) & 1) == 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { m = member{}; }
			return m;
		}

		// The party as the game seats it: the four _HEROn_ names (online too: every player's hero is seated).
		std::array<void*, 4> party_entities()
		{
			std::array<void*, 4> out{};
			for (int i = 0; i < 4; ++i)
			{
				char name[] = "_HERO1_";
				name[5] = static_cast<char>('1' + i);
				out[i] = find(name);
			}
			return out;
		}

		toggle_result toggle(bool on)
		{
			const auto entities = party_entities();
			std::array<member, 4> members{};
			for (int i = 0; i < 4; ++i)
			{
				members[i] = describe(entities[i]);
			}
			return state.toggle(on, members, members.size());
		}

		const char* text(toggle_result r)
		{
			switch (r)
			{
			case toggle_result::entered: return "the party is solo";
			case toggle_result::left: return "the party regrouped";
			case toggle_result::unchanged: return "no change";
			case toggle_result::refused_players: return "refused: more than one player";
			case toggle_result::too_few: return "refused: no other party member";
			}
			return "?";
		}

		// default.xbe 0x34bb0(manual, all=1): the automatic regroup asks with manual 0, the held button with 1 (each peer
		// must also be on screen: the camera's test 0x4ae90 with margin 150 - PC CCamera vt+0x178, 0x4403a0).
		bool regroup_allowed(void* hero, bool manual)
		{
			__try
			{
				if (!hero || (at<unsigned char>(hero, ai_flags) & 4)) return false;
				void* nav = at<void*>(hero, navigator);
				const int group = at<int>(hero, controller);
				for (void* peer : party_entities())
				{
					if (!peer || peer == hero || !is_character(peer) || !(at<float>(peer, health) > 0.0f) || at<int>(peer, controller) != group)
					{
						continue;
					}
					if (!nav || at<u32>(nav, 0) != path_vtable) return false;
					const float position[3]{at<float>(peer, origin), at<float>(peer, origin + 4), at<float>(peer, origin + 8)};
					if (manual)
					{
						void* camera = fn<getter_t>(camera_getter)();
						if (!camera || at<u32>(camera, 0) != camera_vtable) return false;
						if (!at<visible_t>(at<const void*>(camera, 0), camera_visible_slot)(camera, nullptr, position, regroup_view_margin, nullptr)) return false;
					}
					at<reset_t>(at<const void*>(nav, 0), 4)(nav, nullptr);
					// XMen2.exe's query takes a fourth argument: 1 skips the far-path (buoy) lookup the PC added
					// (0x496556), leaving default.xbe's near search (0x90c50).
					if (at<query_t>(at<const void*>(nav, 0), 0)(nav, nullptr, position, regroup_radius, regroup_steps, 1) != regroup_reached)
					{
						return false;
					}
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		bool any_member_alive()
		{
			for (void* e : party_entities())
			{
				if (e && state.contains(handle_of(e))) return true;
			}
			return false;
		}

		// default.xbe 0x73420 (the game's world update): the automatic regroup. Run from the AI sites, once per game
		// clock value; the schedule itself is the original's.
		void tick()
		{
			const float t = now();
			if (t < 0 || t == last_tick) return;
			last_tick = t;
			if (!any_member_alive())
			{
				// A zone or save load made new bodies: the original's constructor clears the bit (0x33cd0).
				state.clear();
				logger::write("solo mode: cleared - no solo party member left (zone or save load)");
				return;
			}
			u32 h = 0;
			void* hero = find("_ACTIVE_HERO_");
			h = handle_of(hero);
			if (!state.regroup_due(state.contains(h), t)) return;
			if (regroup_allowed(hero, false))
			{
				const auto r = toggle(false);
				say(hero, voice_end);
				logger::write("solo mode: automatic regroup at %.2f s - %s", t, text(r));
			}
		}

		int __cdecl solo_pair(void* self, void* leader)
		{
			if (!state.active()) return 0;
			tick();
			return either(state, handle_of(self), handle_of(leader)) ? 1 : 0;
		}

		int __cdecl solo_one(void* self)
		{
			if (!state.active()) return 0;
			tick();
			return state.contains(handle_of(self)) ? 1 : 0;
		}

		// default.xbe 0x36011 (the controller update 0x359f0): the hero's own selection button held 0.5 s. Solo: the
		// manual regroup test, then leave with the voice line; not solo: the toggle (its guards) and the voice line.
		void __cdecl solo_input(void* hero)
		{
			if (!sites_installed || !is_character(hero)) return;
			const float t = now();
			if (state.contains(handle_of(hero)))
			{
				if (!regroup_allowed(hero, true))
				{
					logger::write("solo mode: held hero button at %.2f s - a party member can't reach you or is off screen, still solo", t);
					return;
				}
				const auto r = toggle(false);
				say(hero, voice_end);
				logger::write("solo mode: held hero button at %.2f s - %s", t, text(r));
				return;
			}
			const auto r = toggle(true);
			say(hero, voice_begin);
			last_tick = -1.0f;
			logger::write("solo mode: held hero button at %.2f s - %s", t, text(r));
		}

		// ---- The sites. Each stub repeats the bytes it replaced, then asks solo_pair / solo_one. -------------------
		int verdict = 0;
		u32 h1_hold = 0x51ee20, h1_follow = 0x51ee36;
		u32 h2_skip = 0x51edcb, h2_block = 0x51ebe0;
		u32 h3_hold = 0x50b8d2, h3_follow = 0x50b8e9;
		u32 h4_absent = 0x50c553, h4_follow = 0x50c2a6, h4_return0 = 0x50c5d1;
		u32 h5_native = 0x51dc17;
		u32 h6_native = 0x526967;
		u32 h7_timer = 0x425937, h7_next = 0x42599e;

		__declspec(naked) void h1_stub()
		{
			__asm {
				mov al, byte ptr [ebp + 0x3d8]
				mov bl, 4
				test bl, al
				jnz hold
				pushfd
				pushad
				push ebp
				push edi
				call solo_pair
				add esp, 8
				mov dword ptr [verdict], eax
				popad
				popfd
				cmp dword ptr [verdict], 0
				jnz hold
				jmp dword ptr [h1_follow]
			hold:
				jmp dword ptr [h1_hold]
			}
		}

		__declspec(naked) void h2_stub()
		{
			__asm {
				mov al, byte ptr ds:[0x70b708]
				test al, al
				jnz skip
				pushfd
				pushad
				mov eax, dword ptr [esi + 4]
				push ebp
				push dword ptr [eax + 0x10]
				call solo_pair
				add esp, 8
				mov dword ptr [verdict], eax
				popad
				popfd
				cmp dword ptr [verdict], 0
				jnz skip
				jmp dword ptr [h2_block]
			skip:
				jmp dword ptr [h2_skip]
			}
		}

		__declspec(naked) void h3_stub()
		{
			__asm {
				test byte ptr [edi + 0x3d8], 4
				jnz hold
				pushfd
				pushad
				push edi
				push dword ptr [esi + 0x10]
				call solo_pair
				add esp, 8
				mov dword ptr [verdict], eax
				popad
				popfd
				cmp dword ptr [verdict], 0
				jnz hold
				jmp dword ptr [h3_follow]
			hold:
				jmp dword ptr [h3_hold]
			}
		}

		__declspec(naked) void h4_stub()
		{
			__asm {
				test edi, edi
				jz absent
				pushfd
				pushad
				push edi
				push dword ptr [esi + 0x10]
				call solo_pair
				add esp, 8
				mov dword ptr [verdict], eax
				popad
				popfd
				cmp dword ptr [verdict], 0
				jnz solo
				jmp dword ptr [h4_follow]
			solo:
				mov ecx, dword ptr [esi + 0x10]
				mov eax, dword ptr [ecx]
				call dword ptr [eax + 0x154]
				jmp dword ptr [h4_return0]
			absent:
				jmp dword ptr [h4_absent]
			}
		}

		__declspec(naked) void h5_stub()
		{
			__asm {
				pushfd
				pushad
				mov eax, dword ptr [ecx + 4]
				xor edx, edx
				test eax, eax
				jz ask
				mov edx, dword ptr [eax + 0x10]
			ask:
				push edx
				call solo_one
				add esp, 4
				mov dword ptr [verdict], eax
				popad
				popfd
				cmp dword ptr [verdict], 0
				jnz solo
				sub esp, 0x30
				push ebx
				push ebp
				push esi
				push edi
				jmp dword ptr [h5_native]
			solo:
				xor eax, eax
				ret 0xc
			}
		}

		__declspec(naked) void h6_stub()
		{
			__asm {
				pushfd
				pushad
				push dword ptr [esp + 0x28] // the command's character: past pushad (0x20), pushfd (4), the return
				call solo_one
				add esp, 4
				mov dword ptr [verdict], eax
				popad
				popfd
				cmp dword ptr [verdict], 0
				jnz solo
				sub esp, 0x38
				push ebx
				push ebp
				push esi
				push edi
				jmp dword ptr [h6_native]
			solo:
				ret 8
			}
		}

		// 0x425982 (the controller update 0x425500): the held selection button is the hero's own. The PC only re-arms its
		// 0.5 s timer there (0x425937); the original toggled solo at the same point.
		__declspec(naked) void h7_stub()
		{
			__asm {
				cmp ebx, dword ptr [esp + ebp * 4 + 0x38]
				jnz other
				pushfd
				pushad
				push ebx
				call solo_input
				add esp, 4
				popad
				popfd
				jmp dword ptr [h7_timer]
			other:
				jmp dword ptr [h7_next]
			}
		}

		struct site
		{
			u32 va;
			const char* original; // the replaced bytes (hex)
			const void* stub;
			const char* what;
		};

		std::vector<unsigned char> bytes(const char* hex)
		{
			std::vector<unsigned char> out;
			for (std::size_t i = 0; hex[i] && hex[i + 1]; i += 2)
			{
				auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
				out.push_back(static_cast<unsigned char>(v(hex[i]) * 16 + v(hex[i + 1])));
			}
			return out;
		}

		bool retail(const limits_rules::guard& g)
		{
			__try { return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va), g.hex); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		xml1_hook_transaction::patch jump(const site& s)
		{
			xml1_hook_transaction::patch p{s.va, bytes(s.original), {}};
			p.after.assign(p.before.size(), 0x90);
			p.after[0] = 0xe9;
			const u32 delta = static_cast<u32>(reinterpret_cast<std::uintptr_t>(s.stub)) - (s.va + 5);
			std::memcpy(p.after.data() + 1, &delta, 4);
			return p;
		}
	}

	bool requested() { return was_requested; }
	bool installed() { return sites_installed; }

	registration capability()
	{
		if (!was_requested) return {{0, 0, 0, 0}, true};          // verified off
		if (!sites_installed) return {{0, 0, 0, 0}, false};        // requested, failed: poisons online agreement
		return {{1, 1, 0, 0}, true};                               // installed, decision rules revision 1
	}

	void script_request(void* entity, bool on, const char* name)
	{
		if (!sites_installed)
		{
			logger::write("solo mode: %s(%s) - not installed, nothing done", on ? "enter" : "exit", name ? name : "");
			return;
		}
		if (!is_character(entity))
		{
			logger::write("solo mode: %s(%s) - not a character, nothing done", on ? "enter" : "exit", name ? name : "");
			return;
		}
		const bool solo = state.contains(handle_of(entity));
		// default.xbe 0x9eef0: a character not solo yet - the toggle, then its voice line (even when the toggle
		// refused). 0x9ef70 mirrors it for a solo character.
		if (on == solo)
		{
			logger::write("solo mode: %s(%s) - already %s", on ? "enter" : "exit", name ? name : "", on ? "solo" : "with the party");
			return;
		}
		const auto r = toggle(on);
		say(entity, on ? voice_begin : voice_end);
		last_tick = -1.0f;
		logger::write("solo mode: %s(%s) at %.2f s - %s", on ? "enter" : "exit", name ? name : "", now(), text(r));
	}

	void install(HMODULE game)
	{
		was_requested = ini::flag(L"Game", L"SoloMode", false);
		sites_installed = false;
		if (!was_requested) return;
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("solo mode: XMen2.exe isn't at 0x400000 - not installed, the native AI follows as before");
			return;
		}
		constexpr std::array<limits_rules::guard, 20> guards{{
			{0x51ee0e, "0f89160300008a85d8030000b30484c374168b078bcfff905401000032c05f5e5d5b83c428c20c00", "hero idle handler: formation follow"},
			{0x51ebc6, "f680b4020000400f85f8010000a008b7700084c00f85eb010000", "hero idle handler: bridge power"},
			{0x50b8c3, "0f8470040000f687d80300000474178b4e108b01ff905401000032c05f5e5d5b83c424c20400", "follow movement"},
			{0x50c298, "0f845001000085ff0f84ad0200008b47", "follow movement (formation slot)"},
			{0x50c553, "b0015f5e5d5b83c438c20400", "follow movement: return 1"},
			{0x50c5d1, "5f5e5d32c05b83c438c20400", "follow movement: return 0"},
			{0x51dc10, "83ec3053555657518bf18b4c24488bc4", "hero idle handler: assist step"},
			{0x526960, "83ec38535556578bd9", "follow-me command"},
			{0x420db0, "83ec08568bf18b8644040000", "character voice line"},
			{0x4654b0, "568bf1e8682a06008b16518bcc89118b", "entity handle resolve"},
			{0x4a1700, "83ec1c568b74242856e82e0c1d0083c4", "script name lookup"},
			{0x45fe60, "8b0d348471008b01ffa060010000", "game clock"},
			{0x6949e4, "70ea5100", "hero idle handler vtable"},
			{0x689b78, "8062490060254900", "CPath vtable (query, reset)"},
			{0x496280, "83ec3c535657", "CPath query"},
			{0x492560, "33c0c781f403000005000000", "CPath reset"},
			{0x425978, "38b9700033c03bc8750a3b5cac387516ebad33c0898638b97000898690b97000", "controller update: held hero button"},
			{0x4457e0, "64a1000000008a0dac7671006aff", "camera getter"},
			{0x684e3c, "a0034400", "camera vt+0x178 (on-screen test)"},
			{0x4403a0, "8b44240483ec0c81c10c020000", "camera on-screen test"}}};
		for (const auto& g : guards)
		{
			if (!retail(g))
			{
				logger::write("solo mode: 0x%08X isn't the retail code (%s) - not installed, the native AI follows as before", g.va, g.what);
				return;
			}
		}
		const std::array<site, 7> sites{{
			{0x51ee14, "8a85d8030000b30484c37416", reinterpret_cast<const void*>(&h1_stub), "formation follow"},
			{0x51ebd3, "a008b7700084c00f85eb010000", reinterpret_cast<const void*>(&h2_stub), "bridge power"},
			{0x50b8c9, "f687d8030000047417", reinterpret_cast<const void*>(&h3_stub), "follow movement"},
			{0x50c29e, "85ff0f84ad020000", reinterpret_cast<const void*>(&h4_stub), "follow movement (formation slot)"},
			{0x51dc10, "83ec3053555657", reinterpret_cast<const void*>(&h5_stub), "assist step"},
			{0x526960, "83ec3853555657", reinterpret_cast<const void*>(&h6_stub), "follow-me command"},
			{0x425982, "3b5cac387516ebad", reinterpret_cast<const void*>(&h7_stub), "held hero button"}}};
		std::vector<xml1_hook_transaction::patch> writes;
		for (const auto& s : sites) writes.push_back(jump(s));
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			sites_installed = true;
			logger::write("solo mode: installed (6 AI sites and the held hero button; setSoloMode, automatic regroup 1.5 s / 1.0 s)");
		}
		else if (safe)
		{
			logger::write("solo mode: the AI sites couldn't be written - not installed, the native AI follows as before");
		}
		else
		{
			game_version::refuse_startup();
			logger::write("solo mode: rollback/protection verification failed; process startup refused");
		}
	}
}
