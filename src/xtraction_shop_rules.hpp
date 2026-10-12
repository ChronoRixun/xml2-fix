#pragma once

#include "limits_rules.hpp"
#include <array>

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
	};

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
		{forge, 2005, "shopMenu('equip')", 1, 1, nullptr}, // its campaign rule is the builder's ftalk signal (#95)
	}};

	// Whether `o` is added: the hook is on, the point offers it, and its campaign character (if any) is in.
	constexpr bool offered(const bool enabled, const int services, const option& o, const bool in_campaign)
	{
		return enabled && (services & o.service) != 0 && (o.campaign == nullptr || in_campaign);
	}
}
