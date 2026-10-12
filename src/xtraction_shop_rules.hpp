#pragma once

#include "limits_rules.hpp"
#include <array>
#include <cstdint>

// Issue #95 / #63: the first game's Xtraction services appended before the native dialog's finalize call.
// The builder passes the point's services as a bitmask (the second argument of extractionPointServices);
// this never changes Save, Change Team, or the world map.
//
// The first game (default.xbe) builds its points' menus in extractionPoint (0x9f110) and extractionPointLite
// (0x9f4a0): the title, Change Team (lite: its first flag), Save (2002, saveloadProcess(4)) and Load (2006,
// saveloadProcess(3)) always, then
//   - Danger Room (2003, openmenu('danger_room')) when 'storm' is in the campaign (lite: and its second flag),
//   - Healer (2004, openmenu('item_shop')) when 'healer' is in the campaign (lite: and its third flag),
//   - Forge (2005, openmenu('equip_shop')) when 'forge' is in the campaign (lite: and its fourth flag).
// "In the campaign" is the campaign registry's character bit 1 (vt+0x3c name -> index, vt+0x88 index ->
// bit, 0x56df0), the bit the first activation of a hero-team actor sets (0xb22d0) and the save restores.
// XMen2.exe keeps the same bit (registry 0x44b8f0, vt+0x3c 0x44acc0 name -> index, vt+0x84 0x44b800
// index -> bit 1 of registry+0x9b40+0x1c*index), set by the same first-activation rule (0x4bc990) and
// restored by the load (0x44ba30). The builder decides the point's own flags; the DLL reads the campaign.
namespace xtraction_shop_rules
{
	constexpr DWORD finalize_call = 0x4a6d33;
	constexpr DWORD dialog_getter = 0x5eb300;
	constexpr DWORD text_getter = 0x4bdb30;
	constexpr DWORD text_vtable = 0x68e990;
	constexpr int label_id = 2005;
	constexpr DWORD registry_getter = 0x44b8f0;
	constexpr DWORD registry_vtable = 0x68544c;
	constexpr DWORD registry_index_slot = 0x3c;    // short (const char* name), 0 = none (0x44acc0)
	constexpr DWORD registry_campaign_slot = 0x84; // bool (short index): in the campaign (0x44b800)
	inline constexpr std::array<limits_rules::guard, 6> guards{{
		{0x4a6d04, "e8276e01008bf0e8f04514006a006a006a006a008d5424288bf88b1f8b065268d20700008bceff5008508bcfff531ce8c84514008b108bc8ff5268",
		 "native Save addOption(text, script, script2, closes, resumes, disabled), then dialog finalize"},
		{text_getter, "8a0d98337700b80100000084c80f858f", "text singleton getter"},
		{dialog_getter, "a1ec138b0085c0750ae882ffffffa1ec", "dialog singleton getter"},
		{registry_getter, "64a1000000006aff68b132670050a10c77710064892500000000", "the registry's getter (0x44b8f0)"},
		{0x685488, "c0ac4400", "registry vt+0x3c = the name lookup 0x44acc0 (0x685488)"},
		{0x6854d0, "00b84400", "registry vt+0x84 = the campaign-bit predicate 0x44b800 (0x6854d0)"},
	}};
	constexpr bool should_add(bool enabled, bool permission) { return enabled && permission; }

	// The second argument of extractionPointServices. 0 and 1 keep their 1.4.0 meaning (Forge only).
	enum service : int
	{
		forge = 1,       // the point offers Forge and its contact is made (the builder's ftalk signal)
		healer = 2,      // the point offers the Healer (full points; lite points' third flag)
		load = 4,        // Load (every first-game point)
		danger_room = 8, // the point offers the Danger Room (full points; lite points' second flag)
		forge_campaign = 16, // Forge also needs 'forge' in the campaign (the first game's rule; without it 1.4.0's)
		no_change_team = 32, // a lite point whose first flag is false: no Change Team (default.xbe 0x9f695)
	};
	// Everything this DLL understands (the shared gameplay agreement advertises it, rule 10).
	constexpr int capability = forge | healer | load | danger_room | forge_campaign | no_change_team;

	struct option
	{
		int service;
		int label;              // the build's string id (the first game's)
		const char* script;     // run when chosen
		int closes, resumes;    // addOption's 4th / 5th arguments, as the first game passes them
		const char* campaign;   // the character that must be in the campaign, or null
	};

	// In the first game's order: Load, Danger Room, Healer, Forge (Save and Change Team are native, before them).
	inline constexpr std::array<option, 4> options{{
		{load, 2006, "saveloadProcess(3)", 0, 0, nullptr},
		{danger_room, 2003, "dangerRoomMenu()", 1, 1, "storm"},
		{healer, 2004, "shopMenu('item')", 1, 1, "healer"},
		{forge, 2005, "shopMenu('equip')", 1, 1, "forge"}, // 'forge' checked only with forge_campaign (1.4.0 builds: ftalk)
	}};

	// Whether `o` is added: the hook is on, the point offers it, and its campaign character (if any) is in.
	constexpr bool needs_campaign(const int services, const option& o)
	{
		return o.campaign != nullptr && (o.service != forge || (services & forge_campaign) != 0);
	}
	constexpr bool offered(const bool enabled, const int services, const option& o, const bool in_campaign)
	{
		return enabled && (services & o.service) != 0 && (!needs_campaign(services, o) || in_campaign);
	}

	// Online with more than one player, XMen2.exe's own Load is not available (the pause menu skips it; CSaveLoad's
	// own test 0x4aebb6-0x4aebde: net session 0x60c7e0 -> +0x64 set, players 0x612be0 -> +0x278 > 1). The Load option
	// is still added, greyed (addOption's 6th argument), so every peer's option list keeps the same indices.
	constexpr DWORD net_getter = 0x60c7e0;       // __cdecl: the net session (0xa3e168)
	constexpr DWORD net_online_offset = 0x64;     // byte: in a network game (0x60b420)
	constexpr DWORD players_getter = 0x612be0;   // __cdecl: the player manager (0xa53058)
	constexpr DWORD players_count_offset = 0x278; // int: connected players (0x6110b0)
	constexpr bool load_disabled(const bool online, const int players) { return online && players > 1; }
	inline constexpr std::array<limits_rules::guard, 5> online_guards{{
		{0x4aebb6, "e825dc15008bc8e85ec8150084c07451", "CSaveLoad: net session getter, its +0x64 byte test"},
		{0x4aebcf, "e80c4016008bc8e8d5241600", "CSaveLoad: player manager getter, its count"},
		{0x4a6cf0, "68b0d468008d4c24", "0x4a6cf0: the Save add begins (push 'saveloadProcess(4)')"},
		{0x60b420, "8a4164c3", "0x60b420: the net session's online byte (+0x64)"},
		{0x6110b0, "8b8178020000c3", "0x6110b0: the player manager's count (+0x278)"},
	}};

	// The XML1 Xtract skip (xtract_rules.hpp, jmp rel8 at 0x4a6c7a) lands on Change Team (0x2d) or, for a lite point
	// without its first flag, on the Save add (0x4a6cf0, 0x74): Change Team's block only formats its line, adds the
	// option and reloads esi / edi / ebx, like the Xtract block, and the stack is balanced at 0x4a6cf0.
	constexpr std::uint8_t to_change_team = 0x2d, to_save = 0x74;
	constexpr DWORD save_add = 0x4a6cf0;
}
