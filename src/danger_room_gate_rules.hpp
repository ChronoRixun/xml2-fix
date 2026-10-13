#pragma once

// [Game] DangerRoomGate=<item>: the first game's main-menu Danger Room gate, for a mod whose main menu
// gives its Danger Room item a usecmd (the X-Men Legends 1 port: button3) instead of XML2's
// label_option06 and its story-level-6 popup.
//
// The first game (default.xbe): the main menu's update (0x17e93c-0x17e975) asks the profile (getter
// 0x8c980, vtable 0x3cf9c4) whether the character 'phoenix' (campaign registry vt+0x3c) has its profile
// bit (vt+0x34 = 0x8c3b0, a 256-bit map at profile +0x150) and, when it hasn't, disables button3
// (0x162960(name, 0)): the item is greyed and focus skips it. The bit is set, never cleared, by vt+0x28
// (0x8c380) from exactly two places: the campaign registry's add (vt+0x2c = 0x552a0, the first time a
// character enters a campaign, at 0x552de) and the load, for every character in the save's list
// (0x56fd0-0x57005, at 0x56fe9). Phoenix enters the campaign on Jean's opening mansion tour
// (setInCampaign in conversation 1_2_1_1). It is not XML2's story level 6.
//
// XMen2.exe keeps the same per-profile character map: the unlock manager / profile (getter 0x48fed0,
// vtable 0x689994) vt+0x28 = 0x48f740 sets bit i of +0x1c0, vt+0x38 = 0x48f770 tests it; the registry's
// add (vt+0x2c = 0x449c00) sets it (0x449c40-0x449c4a). The port's setInCampaign is unlockCharacter
// (0x49f520 -> 0x449c00), so Phoenix's profile bit is set at the first game's moment. XMen2.exe's load
// (registry vt+0xa4 = 0x44b970, its loop at 0x44ba30) restores the registry bits but not the profile's; the
// first game's load sets both. The hook restores that for the gate character only: after the registry's load,
// a save with Phoenix in its campaign sets Phoenix's profile bit (the only bit the gate reads; the team
// menu's other profile bits are left to XMen2.exe's own rules) - for local loads only: in a network game the loaded
// campaign is the shared session (a joiner receives the host's save), not this profile's.
//
// The hook: MAIN_MENU's first update step (vt+0x10, the slot 0x69f144 = 0x5c9260, the step that
// disables e3 items through the same item setter 0x5ade70) runs, then the item is disabled when the
// bit is clear. Mouse (item +0x54 bit 8, 0x5c9320) and key/pad focus skip a disabled item natively.

#include "limits_rules.hpp"

#include <array>
#include <string_view>

namespace danger_room_gate_rules
{
	constexpr DWORD first_step_slot = 0x69f144; // MAIN_MENU vt+0x10
	constexpr DWORD first_step = 0x5c9260;
	constexpr DWORD main_menu_vtable = 0x69f134;
	constexpr DWORD item_enable = 0x5ade70;      // __thiscall (menu, const char* name, int enabled), ret 8
	constexpr DWORD profile_getter = 0x48fed0;
	constexpr DWORD profile_vtable = 0x689994;
	constexpr DWORD profile_bit_slot = 0x38;    // bool (int index): 0x48f770
	constexpr DWORD registry_getter = 0x44b8f0;
	constexpr DWORD registry_vtable = 0x68544c;
	constexpr DWORD registry_index_slot = 0x3c; // short (const char*): 0x44acc0
	constexpr DWORD registry_load_slot = 0x6854f0;   // registry vt+0xa4 = its save-data load 0x44b970 (thiscall, ret 4)
	constexpr DWORD registry_load = 0x44b970;
	constexpr DWORD registry_campaign_slot = 0x84;  // bool (short index): 0x44b800, the save's own bit
	constexpr DWORD profile_set_slot = 0x28;        // void (int index): 0x48f740
	constexpr const char* gate_character = "phoenix";
	constexpr DWORD net_getter = 0x60c7e0;   // __cdecl: the net session; byte +0x64 = in a network game (0x60b420)
	constexpr DWORD net_online_offset = 0x64;
	constexpr std::size_t name_max = 31;

	inline constexpr std::array<limits_rules::guard, 11> guards{{
		{net_getter, "8a0d80e4a300b80100000084c875258b", "the net session's getter (0x60c7e0)"},
		{0x60b420, "8a4164c3", "0x60b420: the net session's online byte (+0x64)"},
		{registry_load_slot, "70b94400", "registry vt+0xa4 = its load 0x44b970 (0x6854f0)"},
		{0x6854d0, "00b84400", "registry vt+0x84 = the campaign-bit predicate 0x44b800 (0x6854d0)"},
		{0x6899bc, "40f74800", "profile vt+0x28 = the character-bit setter 0x48f740 (0x6899bc)"},
		{first_step_slot, "60925c00", "MAIN_MENU vt+0x10 = its first update step 0x5c9260 (0x69f144)"},
		{item_enable, "8b44240485c0568bf1744e8038007449", "the item setter 0x5ade70 (name, enabled): a null or empty name does nothing"},
		{profile_getter, "64a1000000008a0d88c972006aff68", "the profile's getter (0x48fed0)"},
		{0x6899cc, "70f74800", "profile vt+0x38 = the character-bit test 0x48f770 (0x6899cc)"},
		{0x48f770, "8b54240485d2568bf17c2a81fa290100007d22", "0x48f770 tests bit index (< 0x129) of the map at +0x1c0"},
		{0x685488, "c0ac4400", "registry vt+0x3c = the name lookup 0x44acc0 (0x685488)"},
	}};

	// After a load: mirror the save's bit into the profile when it is set and the profile's is not.
	constexpr bool mirror(const bool save_bit, const bool profile_bit) { return save_bit && !profile_bit; }

	// Disable the item when the character's profile bit is clear; a failed read keeps the item as it is.
	constexpr bool disable(const bool read, const bool bit) { return read && !bit; }

	constexpr bool valid_item(const std::string_view name)
	{
		if (name.empty() || name.size() > name_max) return false;
		for (const char c : name)
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
		return true;
	}
}
