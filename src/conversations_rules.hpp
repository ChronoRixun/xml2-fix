#pragma once

// Conversations ([Game] AutoAdvance, ReplyVoices, ReplyCursor): three changes to XMen2.exe's
// conversation system for a mod whose conversations were written for X-Men Legends 1's engine (the
// X-Men Legends 1 port), kept apart from the patching (conversations.cpp) so xml2_test can check the
// decisions without the game: the ini keys, every retail byte the hooks rely on, what each hook
// writes, and the per-frame decision of the auto-advance written over a view of the frame.
//
// The research is in the xml1-port repository, research/heroes/conversation-speakers.md (sections 4,
// 8 and 9) and research/audit/xml1_world_gaps_2026-10-01.md (findings W1, W3 and 7); every address
// below was read again from the retail XMen2.exe for this code (image base 0x400000, no
// relocations) and is in `guards`.
//
// The conversation system (CS) is the singleton [0x717aac] (getter 0x4583f0, vtable 0x685e04). Its
// update, vt+0xc = 0x45d1a0, runs every frame from 0x46f16f:
//   1. nothing unless initialised (bit 4 of CS+0x21b24) and active (vt+0x20); the conversation ends
//      when its file is no longer loaded (0x45d21a);
//   2. a PENDING RESPONSE (CS+0x239a0 != 0, set by 0x458700 when the chosen reply has a voice):
//      0x45d242 asks the audio system (0x592480, vtable 0x69d1ac) whether the reply's voice
//      (CS+0x21b80) still plays (vt+0x60 = 0x5909a0), STOPS it if so (vt+0x74 = 0x590780) and
//      advances to the reply's line (0x45cde0), returning at 0x45d2a7 - so a voiced reply is cut one
//      frame after it starts (XML2 has 6 voiced replies, the port 239);
//   3. the ending flag (bit 3) ends it; 4. the current line (CS+0x4bc, looked up with 0x4573f0) is
//      drawn (its vt+0, 0x45beb0), which rebuilds the reply menu every frame (0x45b0a0: the visible
//      replies in CS+0x4c0.., their count in the short CS+0x21b28, the cursor in the short CS+0x21b26);
//   5. 0x455a80 / 0x41d8e0 (the activator);
//   6. ACCEPT: the menu manager (0x5d8920, vtable 0x6a236c) vt+0x138(4) = 0x5d4950 says whether an
//      active player pressed accept this frame (call at 0x45d33e); then only once the game's time
//      (game 0x46dce0 vt+0x160 = 0x469740, the float at game+0x3e8) is past CS+0x21b5c (the
//      conversation's start plus 1.0 s, [0x685c28]): the menu sound vt+0xe8(6), the line's voice
//      stopped if one plays (CS+0x21b80 != [0x69d05c] = -1: vt+0x74, bit 0 of CS+0x239a4 cleared, the
//      handle set to -1), and CS vt+0x18 = 0x45d5d0(cursor), which picks the cursor-th visible reply:
//      with a voice it starts it and sets it pending (step 2 next frame), else advances at once;
//   7. up / down move the cursor over the visible count.
// Nothing in the loop waits: a line sits until accept. X-Men Legends 1 flagged lines
// `runWithoutUser`, which XMen2.exe never reads (no such string); it does read `timeDelay` (the line
// parser 0x458820 at 0x458b68: atof into the line's +0x80, 0.5 when absent; the response parser
// 0x459503 the same) and then never uses it (its only other use is a copy into CS+0x239a8 at display,
// 0x45a260 / 0x45a30a / 0x4587ee, which nothing reads). The port's builder writes the flag INTO that
// number: a NEGATIVE timeDelay marks a line that advances by itself, and its magnitude is how long
// to show the line when it has no voice to wait for (the builder sizes it to the text). Retail data
// has no negative timeDelay (XML2: two lines at 3 and 2, two responses at 3 and 1).
//
// The three changes:
//   AutoAdvance (the call at 0x45d33e, vt+0x138(4), becomes a call into the fix): the fix asks the
//     game's own accept first and hands the answer back as it is; when accept isn't pressed and the
//     current line's timeDelay is negative, exactly ONE reply is visible, the accept lock-out has
//     passed and either the line's voice has played and ended (the fix saw it playing, then not), or,
//     with no voice to wait for, |timeDelay| seconds have passed, the fix does what step 6 does
//     without the menu sound - stops the line's voice if any and calls vt+0x18(0) - and answers "not
//     pressed", so the game's own step 6 does nothing more that frame. A menu (two or more replies)
//     is never picked by the fix.
//   ReplyVoices (the call at 0x45d242 becomes a jump into the fix): while the pending reply's voice
//     still plays and accept isn't pressed, the update goes on at step 3 (the line and its menu stay
//     drawn, the cursor keys work, the game's own accept finds nothing pressed) instead of stopping
//     the voice; once it has ended, or when accept is pressed (a skip), the game's own path runs as
//     before.
//   ReplyCursor (the store at 0x45b5fc): the menu builder sets the cursor to the visible count when
//     it is at or past it (0x45b5f1-0x45b5fc), which highlights nothing and makes accept pick nothing
//     until up or down is pressed - a menu re-entered by a tagJump whose previous pick is now hidden
//     (the port's hub menus hit it, XML2 retail's 35 conversations with the same pattern too). The
//     fix stores the last visible reply (count - 1, 0 when none) instead: the one after the previous
//     pick, which is what the pick's own bookkeeping meant (0x45d61d-0x45d636 remembers pick + 1,
//     wrapping to 0).
// Each is on unless its key is 0, each is written only when every byte it relies on is the retail
// build's, and on any other build xml2-fix.log says why.

#include "limits_rules.hpp" // guard, matches, image_base, rel32

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace conversations_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	// ---- The game's objects and offsets ---------------------------------------------------------------

	constexpr DWORD cs_pointer = 0x717aac;         // the conversation system singleton (0x4583f0 builds it)
	constexpr DWORD cs_vtable = 0x685e04;
	constexpr DWORD cs_pick_slot = 0x18;           // vt+0x18 = 0x45d5d0: pick the i-th visible reply (ret 4)
	constexpr DWORD cs_flags = 0x21b24;            // byte: bit 1 active, bit 3 ending, bit 4 initialised
	constexpr DWORD cs_line_id = 0x4bc;            // the current line's id
	constexpr DWORD cs_cursor = 0x21b26;           // short: the menu cursor
	constexpr DWORD cs_visible = 0x21b28;          // short: the visible replies
	constexpr DWORD cs_accept_from = 0x21b5c;      // float: the game's time from which accept counts
	constexpr DWORD cs_voice = 0x21b80;            // the line's (or the pending reply's) voice handle
	constexpr DWORD cs_pending = 0x239a0;          // the pending reply's id, 0 for none
	constexpr DWORD cs_voice_flags = 0x239a4;      // byte: bit 0 the line's voice is on
	constexpr DWORD node_lookup = 0x4573f0;        // __thiscall node* (int id): the line, or 0
	constexpr DWORD node_time_delay = 0x80;        // float: the line's timeDelay
	constexpr DWORD none_handle_va = 0x69d05c;     // the audio system's "no handle", -1
	constexpr std::uint32_t none_handle = 0xffffffff;
	constexpr DWORD audio_getter = 0x592480;       // __cdecl: the audio system
	constexpr DWORD audio_vtable = 0x69d1ac;
	constexpr DWORD audio_playing_slot = 0x60;     // bool (handle), ret 4: 0x5909a0
	constexpr DWORD audio_stop_slot = 0x74;        // void (handle), ret 4: 0x590780
	constexpr DWORD game_getter = 0x46dce0;        // __cdecl: the game
	constexpr DWORD game_vtable = 0x686e1c;
	constexpr DWORD game_time_slot = 0x160;        // float (): the game's time, 0x469740
	constexpr DWORD menus_getter = 0x5d8920;       // __cdecl: the menu manager
	constexpr DWORD menus_vtable = 0x6a236c;
	constexpr DWORD menus_accept_slot = 0x138;     // bool (int action), ret 4: 0x5d4950; action 4 = accept
	constexpr DWORD menus_name_slot = 0x214;       // const char* (): the current menu's name, "" for none
	constexpr int accept_action = 4;

	// ---- The sites ------------------------------------------------------------------------------------

	// AutoAdvance: `call dword ptr [edx+0x138]` (ff 92 38 01 00 00) at 0x45d33e -> call rel32 + nop.
	constexpr DWORD accept_call = 0x45d33e;
	constexpr std::array<std::uint8_t, 6> retail_accept_call{0xff, 0x92, 0x38, 0x01, 0x00, 0x00};
	// ReplyVoices: `call 0x592480` (e8 39 52 13 00) at 0x45d242 -> jmp rel32; the fix's stub either
	// makes that call itself and goes on at 0x45d247, or goes on at 0x45d2aa (step 3).
	constexpr DWORD pending_call = 0x45d242;
	constexpr std::array<std::uint8_t, 5> retail_pending_call{0xe8, 0x39, 0x52, 0x13, 0x00};
	constexpr DWORD pending_continue = 0x45d247;
	constexpr DWORD pending_wait_at = 0x45d2aa;
	// ReplyCursor: `mov word ptr [eax+0x21b26], di` (66 89 b8 26 1b 02 00) at 0x45b5fc -> jmp rel32 +
	// two nops; the stub stores max(edi - 1, 0) and goes on at 0x45b603.
	constexpr DWORD cursor_store = 0x45b5fc;
	constexpr std::array<std::uint8_t, 7> retail_cursor_store{0x66, 0x89, 0xb8, 0x26, 0x1b, 0x02, 0x00};
	constexpr DWORD cursor_continue = 0x45b603;

	inline std::array<std::uint8_t, 6> accept_call_bytes(const DWORD target)
	{
		const std::uint32_t rel = limits_rules::rel32(accept_call, target);
		std::array<std::uint8_t, 6> bytes{0xe8, 0, 0, 0, 0, 0x90};
		std::memcpy(bytes.data() + 1, &rel, sizeof(rel));
		return bytes;
	}

	inline std::array<std::uint8_t, 5> pending_jump_bytes(const DWORD target)
	{
		const std::uint32_t rel = limits_rules::rel32(pending_call, target);
		std::array<std::uint8_t, 5> bytes{0xe9, 0, 0, 0, 0};
		std::memcpy(bytes.data() + 1, &rel, sizeof(rel));
		return bytes;
	}

	inline std::array<std::uint8_t, 7> cursor_jump_bytes(const DWORD target)
	{
		const std::uint32_t rel = limits_rules::rel32(cursor_store, target);
		std::array<std::uint8_t, 7> bytes{0xe9, 0, 0, 0, 0, 0x90, 0x90};
		std::memcpy(bytes.data() + 1, &rel, sizeof(rel));
		return bytes;
	}

	// ---- The ini --------------------------------------------------------------------------------------

	struct switches
	{
		bool auto_advance = true;
		bool reply_voices = true;
		bool reply_cursor = true;
	};

	// A switch's value by the fix's one rule: 1/0, true/false, yes/no, on/off; not set or anything
	// else: on. `error` names a value that isn't a switch.
	inline bool parse_switch(const std::optional<std::string_view> raw, const char* key, std::string& error)
	{
		const auto text = ini_rules::value(raw);
		if (!text)
		{
			return true;
		}
		const auto value = ini_rules::flag(raw);
		if (!value)
		{
			error += std::string(error.empty() ? "" : "; ") + key + "=" + *text + " isn't 1 or 0 - taken as 1";
			return true;
		}
		return *value;
	}

	inline switches decide(const std::optional<std::string_view> auto_advance, const std::optional<std::string_view> reply_voices,
	                       const std::optional<std::string_view> reply_cursor, std::string& error)
	{
		switches chosen;
		chosen.auto_advance = parse_switch(auto_advance, "AutoAdvance", error);
		chosen.reply_voices = parse_switch(reply_voices, "ReplyVoices", error);
		chosen.reply_cursor = parse_switch(reply_cursor, "ReplyCursor", error);
		return chosen;
	}

	// ---- The auto-advance decision -----------------------------------------------------------------------

	// What the hook reads at the accept call, once a frame while a conversation is active.
	struct frame_view
	{
		int line_id = 0;        // CS+0x4bc
		float time_delay = 0.5f; // the line's +0x80; negative marks a line that advances by itself
		int visible = 0;        // CS+0x21b28
		float now = 0;          // the game's time (vt+0x160)
		float accept_from = 0;  // CS+0x21b5c
		bool voice = false;     // CS+0x21b80 is a handle (not -1)
		bool playing = false;   // the audio system says that handle plays
		bool menu_up = false;   // the menu manager has a current menu (the pause menu, a popup)
	};

	// What the hook keeps about the line it is on.
	struct line_state
	{
		int line_id = 0;
		float shown_at = 0;        // the game's time when the line was first seen
		bool seen_playing = false; // its voice was heard playing
		float ended_at = 0;        // the game's time when the voice was first found ended
		bool advanced = false;     // vt+0x18 was called for this line
		float advanced_at = 0;
	};

	enum class verdict : std::uint8_t
	{
		none,    // not the fix's business this frame (no mark, a menu, the game's own rules)
		wait,    // marked, waiting for the voice or the time
		advance, // pick the one reply now
	};

	constexpr float after_voice = 0.25f; // a breath after the voice, before the next line
	constexpr float retry_after = 2.0f;  // a pick that changed nothing (the next line's lookup failed) is tried again after this

	inline verdict auto_advance_step(line_state& s, const frame_view& v)
	{
		if (v.line_id != s.line_id)
		{
			s = line_state{};
			s.line_id = v.line_id;
			s.shown_at = v.now;
		}
		if (v.time_delay >= 0 || v.visible != 1 || v.menu_up)
		{
			return verdict::none;
		}
		if (s.advanced)
		{
			if (v.now - s.advanced_at < retry_after)
			{
				return verdict::wait;
			}
			s.advanced = false;
		}
		if (!(v.now > v.accept_from))
		{
			return verdict::wait; // the game's own accept lock-out: the first second
		}
		if (v.voice)
		{
			if (v.playing)
			{
				s.seen_playing = true;
				s.ended_at = 0;
				return verdict::wait;
			}
			if (s.seen_playing)
			{
				if (s.ended_at == 0)
				{
					s.ended_at = v.now;
				}
				if (v.now - s.ended_at < after_voice)
				{
					return verdict::wait;
				}
				s.advanced = true;
				s.advanced_at = v.now;
				return verdict::advance;
			}
			// a handle that was never heard playing (a voice the sound system couldn't start): the time
		}
		if (v.now - s.shown_at < -v.time_delay)
		{
			return verdict::wait;
		}
		s.advanced = true;
		s.advanced_at = v.now;
		return verdict::advance;
	}

	// The pending reply: wait (keep drawing, don't stop the voice) while it plays and accept isn't pressed.
	inline bool pending_wait(const bool voice, const bool playing, const bool accept_pressed)
	{
		return voice && playing && !accept_pressed;
	}

	// The cursor the menu builder stores when it is at or past the visible count.
	inline int clamped_cursor(const int visible)
	{
		return visible > 0 ? visible - 1 : 0;
	}

	// ---- Every byte of XMen2.exe the changes rely on, read from the retail build ------------------------

	inline constexpr std::array<guard, 25> guards{{
		// The conversation system: its singleton and getter, its vtable's update and pick.
		{0x4583f0, "a1ac7a710085c0750ae882ffffffa1ac7a7100c3", "the conversation system's getter (0x4583f0) and singleton [0x717aac]"},
		{0x685e10, "a0d14500", "conversation system vt+0xc = the update 0x45d1a0 (0x685e10)"},
		{0x685e1c, "d0d54500", "conversation system vt+0x18 = the pick 0x45d5d0 (0x685e1c)"},
		{0x45d1a0, "83ec14568bf1f686241b0200100f84110400008b86381b02005333db8d883c040000899800040000899804040000e8cd1f19008b863c1b02008d883c040000899800040000899804040000e8b01f19008b068bceff502084c00f84c4030000",
		 "the update's start: initialised, active (0x45d1a0-0x45d1ff)"},
		{0x45d23a,
		 "399ea03902007468e8395213008bf8391dac7a71007505e82ab1ffffa1ac7a71008b88801b02008b17518bcfff526084c07413e80e5213008b8e801b02008b10518bc8ff52748b86a03902008b155cd06900508bce8996801b0200e846fbffff8bcee86fb1ffff5f5b5e83c414c20400",
		 "the pending reply: its voice stopped if playing, then the advance (0x45d23a-0x45d2a9)"},
		{0x45d2aa, "f686241b0200080f8573ffffff899ebc390200899ee00400008b8ebc040000518bcee81fa1ffff3bc37511538bcee8d383ffff5f5b5e83c414c204008b106aff8bc8ff12ddd8",
		 "the ending flag, the current line's lookup and draw (0x45d2aa-0x45d2ef)"},
		{0x45d333,
		 "e8e8b517008b106a048bc8ff923801000084c07477e8930901008b108bc8ff9260010000d89e5c1b0200dfe0f6c441755be8b7b517008b106a068bc8ff92e80000008b86801b02003b055cd06900742de8f85013008b8e801b02008b10518bc8ff52748a86a43902008b155cd0690024fe8996801b02008886a43902000fbf8e261b02008b06518bceff5018",
		 "accept: the menu manager's vt+0x138(4), the lock-out, the menu sound, the line's voice stopped, the pick vt+0x18(cursor) (0x45d333-0x45d3be)"},
		{0x45d5d0,
		 "5355568bf18b86e00400008b4c241033db33ed3bc87d7385c07e6f578dbec00400008b0783f8ff74523b6c2414754b508bcee8f9b0ffff84c0753f8b86bc040000508bcee8d79dffff85c074230fbf8e261b0200418988840000000fbf96281b02003bca7c0ac78084000000000000008b07508bcee896f7ffff458b86e00400004383c7043bd87c995f8bcee8afadffff5e5d5bc20400",
		 "the pick (0x45d5d0): the i-th visible reply, its voice started and set pending or the advance at once"},
		{0x4573f0, "8b81b0040000568d71048b4e0451508bcee83af0ffff8bb486280300008b869c0500008b54240823c283f8287d3b399486fc04000075328bc85783e11fbf01000000d3e7c1f80585bc86f00400000f95c084c05f74138b869c05000023c28b84869c0300005ec2040033c05ec20400",
		 "the line lookup by id (0x4573f0), 0 for none"},
		{0x45b5e0, "a1ac7a71006689b8281b0200a1ac7a71000fbf88261b02003bcf7c076689b8261b02005fb0015e5d5b8be55dc3",
		 "the menu builder's end: the visible count stored, the cursor clamped to it (0x45b5e0-0x45b60c)"},
		// timeDelay: parsed into the line's +0x80 (0.5 when absent), copied at display, never read.
		{0x458b68, "686c5e68008bcfe84cbe100085c0740e50e892962100d95e7c83c404eb07c7467c0000003f", "the line parser reads timeDelay with atof into +0x80, 0.5 when absent (0x458b68-0x458b8c)"},
		{0x685e6c, "74696d6544656c617900", "the attribute name \"timeDelay\" (0x685e6c)"},
		{0x45a260, "8b858000000089442414", "the line's timeDelay is copied at display, into CS+0x239a8 (0x45a260)"},
		{0x45a30a, "d985800000008b15ac7a7100d99aa8390200", "and on the other voice path (0x45a30a)"},
		// The audio system: getter, vtable, playing, stop, no handle.
		{0x592480, "64a1000000008a0da0d87f006aff68ce81670050b80100000084c86489250000000075250905a0d87f00b9b8077f00c744240800000000e804d3ffff6860e36700",
		 "the audio system's getter (0x592480)"},
		{0x58f7da, "c706acd16900", "the audio system's vtable (0x69d1ac, stored at 0x58f7da)"},
		{0x69d20c, "a0095900", "audio vt+0x60 = is the handle playing (0x5909a0)"},
		{0x69d220, "80075900", "audio vt+0x74 = stop the handle (0x590780)"},
		{0x5909a0, "8b4424043b055cd06900750532c0c20400", "is-playing: false for the no-handle (0x5909a0)"},
		{0x69d05c, "ffffffff", "the no-handle, -1 (0x69d05c)"},
		// The game's time and the menu manager's accept.
		{0x46dce0, "64a1000000008a0d8ca072006aff681e38670050b80100000084c864892500000000752509058ca07200b960997200c744240800000000e834b1ffff68b0db6700e8f843200083c4048b0c24b86099720064890d0000000083c40cc3",
		 "the game's getter (0x46dce0)"},
		{0x468e6d, "c7061c6e6800", "the game's vtable (0x686e1c, stored at 0x468e6d)"},
		{0x686f7c, "40974600", "game vt+0x160 = the game's time 0x469740 (0x686f7c)"},
		{0x469740, "d981e8030000c3", "the game's time: the float at game+0x3e8 (0x469740)"},
		{0x5d8920, "a118ff8a0085c0750ae882ffffffa118ff8a00c3", "the menu manager's getter (0x5d8920)"},
	}};

	inline constexpr std::array<guard, 4> menu_guards{{
		{0x5d7f4b, "c7066c236a00", "the menu manager's vtable (0x6a236c, stored at 0x5d7f4b)"},
		{0x6a24a4, "50495d00", "menu manager vt+0x138 = accept pressed (0x5d4950)"},
		{0x5d4950, "8b018d542404528b54240852ff903401", "accept pressed: vt+0x134 with the action (0x5d4950)"},
		{0x6a2580, "40865d00", "menu manager vt+0x214 = the current menu's name (0x5d8640)"},
	}};

	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
			}
		}
		for (const auto& g : menu_guards)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	// The three sites' retail bytes, inside the guards above; checked again right before each write.
	inline bool accept_site_is_retail(const std::uint8_t* image)
	{
		return std::memcmp(image + (accept_call - image_base), retail_accept_call.data(), retail_accept_call.size()) == 0;
	}

	inline bool pending_site_is_retail(const std::uint8_t* image)
	{
		return std::memcmp(image + (pending_call - image_base), retail_pending_call.data(), retail_pending_call.size()) == 0;
	}

	inline bool cursor_site_is_retail(const std::uint8_t* image)
	{
		return std::memcmp(image + (cursor_store - image_base), retail_cursor_store.data(), retail_cursor_store.size()) == 0;
	}
}
