#pragma once

// The decisions behind the display-settings patches, kept apart from the installer so the
// tests can check them: which bytes of XMen2.exe prove the sites, and what replaces them.
// Every address is the retail build's, read from its disassembly (2026-10-12).
//
// Two pre-existing engine behaviours break the player's display settings:
//
// 1. The boot FSAA switch (display init FUN_005fac10). The Advanced Options panel's FSAA
//    cycle offers OFF/2x/4x/6x/8x and stores the selected option INDEX (0..4) in the global
//    0xa68c98 and the registry (Settings\Display\FSAA); the boot switch maps indices 0..3 to
//    0/2/4/6 samples through a 4-entry jump table at 0x5fb240 and sends anything above 3 to
//    the default case (0x5fad18), which asks for 0 samples, zeroes the global and persists
//    the settings - so an 8x selection turns itself off on the next boot (issue #84).
//    The patch accepts index 4: cmp eax,3 becomes cmp eax,4, and the jump table's operand
//    (the imm32 at 0x5facd1 of jmp [eax*4+0x5fb240]) points at a private 5-entry table whose
//    5th case asks for 8 samples. The switch can't know whether the adapter does 8x: the
//    adapter's FSAA support bytes (0xa09e84..87, CheckDeviceMultiSampleType at A8R8G8B8
//    fullscreen, FUN_005f5dd0) are only filled AFTER display init - FUN_005f7050, called at
//    0x40181e once the display object's init (vtable slot 0 = 0x5fb270 -> 0x5fac10, called at
//    0x401810) has returned - so at the switch they are still zero on every boot. The device
//    hook (display.cpp, rewrite_present) clamps the request to the highest level the adapter
//    offers at the device's real format and shell, which is why the patch is only installed
//    where that hook is.
//
// 2. The failure-path settings wipe. When the display device can't be created (the window/
//    device open call returns false, or the visual context reports an error), the game
//    persists Resolution="800x600" (FUN_0061bef0) and FSAA=0 (FUN_0061be60) before quitting,
//    at 0x5faf56/0x5faf5b and again at the second failure exit 0x5fb19b/0x5fb1a0. A failure
//    the display rewrite can fix on the next boot destroys the player's settings first. The
//    patch routes the first call of each pair to a logging stub and NOPs the second, so the
//    game still quits on a fatal device failure - with its own "unable to initialise graphics"
//    message box ([0xa09f94] set at 0x5fb320, shown at 0x40357e), which the patch leaves alone -
//    but leaves the registry untouched. Only installed where the fix's device hook has already
//    tried everything the wipe would have achieved (no multisampling, and in Mode=fullscreen the
//    desktop mode): in the game's own mode the wipe stays its crash recovery.

#include "xml1_hook_transaction.hpp"

#include <cstdint>
#include <vector>

namespace display_settings_rules
{
	constexpr std::uint32_t image_base = 0x400000;

	// The boot FSAA switch.
	constexpr std::uint32_t fsaa_switch_cmp = 0x5facc1;        // cmp eax,3 -> cmp eax,4 (the FSAA index bound)
	constexpr std::uint32_t fsaa_table_operand = 0x5facd1;     // the indexed jump's imm32 (0x5fb240) -> the private table
	constexpr std::uint32_t stock_fsaa_table = 0x5fb240;       // 4 dwords: the cases for indices 0..3
	constexpr std::uint32_t fsaa_case_off = 0x5facd5;          // case 0: 0 samples
	constexpr std::uint32_t fsaa_case_2x = 0x5face5;           // case 1: 2 samples
	constexpr std::uint32_t fsaa_case_4x = 0x5facf6;           // case 2: 4 samples
	constexpr std::uint32_t fsaa_case_6x = 0x5fad07;           // case 3: 6 samples
	constexpr std::uint32_t fsaa_default_case = 0x5fad18;      // > 3: 0 samples, clear the global, persist
	constexpr std::uint32_t fsaa_switch_join = 0x5fad31;       // where every case lands after the engine call
	constexpr std::uint32_t engine_param_cell = 0x67f570;      // the engine-parameter call the cases make
	constexpr std::uint32_t multi_sample_type_name = 0x6a3d08; // "multiSampleType"

	// The failure-path settings wipe.
	constexpr std::uint32_t wipe_resolution_call_a = 0x5faf56; // call FUN_0061bef0 (Resolution := "800x600")
	constexpr std::uint32_t wipe_settings_call_a = 0x5faf5b;   // call FUN_0061be60 (FSAA := 0)
	constexpr std::uint32_t wipe_resolution_call_b = 0x5fb19b; // the second failure exit, the same pair
	constexpr std::uint32_t wipe_settings_call_b = 0x5fb1a0;

	struct guard
	{
		std::uint32_t va;
		const char* hex;
		const char* what;
	};

	// The bytes that must be in place before anything is written, checked read-only before the
	// transaction (the mold controller's pattern). They describe the code as the retail build has
	// it and may cover the patch sites: the transaction verifies each write's own `before` again
	// and reads back only the writes. (Passed to the transaction as before==after writes, a guard
	// over a patch site reads the patched bytes back, fails the readback and rolls the whole set
	// back - the 19b2e76 bug.) Every write's `before` must agree with the guards it overlaps.
	inline constexpr guard guards[]{
	    {0x5facbc, "a1988ca60083f8038b4938c644247802774aff248540b25f00",
	     "the boot FSAA switch (mov eax,[0xa68c98]; cmp eax,3; ...; ja default; jmp [eax*4+0x5fb240])"},
	    {stock_fsaa_table, "d5ac5f00e5ac5f00f6ac5f0007ad5f00", "the jump table's four cases (Off/2x/4x/6x)"},
	    {fsaa_case_off, "5568083d6a006a04ff1570f56700eb4c", "the Off case (0 samples; jmp join)"},
	    {fsaa_case_2x, "6a0268083d6a006a04ff1570f56700eb3b", "the 2x case"},
	    {fsaa_case_4x, "6a0468083d6a006a04ff1570f56700eb2a", "the 4x case"},
	    {fsaa_case_6x, "6a0668083d6a006a04ff1570f56700eb19", "the 6x case"},
	    {fsaa_default_case, "5568083d6a006a04ff1570f56700892d988ca600e82f110200", "the default case (> 3: 0 samples, clear the global, persist Off)"},
	    {fsaa_switch_join, "8d5424186a0f52", "the join every case lands on (lea edx,[esp+18]; push 0fh; push edx)"},
	    {0x5faf4f, "c6052d3a6f0001e8950f0200e8000f02008bb42480000000", "the first failure exit (quit flag; call FUN_0061bef0; call FUN_0061be60)"},
	    {0x5fb194, "c6052d3a6f0001e8500d0200e8bb0c02008bb42480000000", "the second failure exit (the same pair)"},
	};

	// The 5th jump-table entry's case, built at the address `stub` (the table's 5th dword): the
	// stock cases' exact shape with 8 samples - push 8 / push "multiSampleType" / push 4 /
	// call [engine_param_cell] / jmp join. Every stock case pushes exactly three dwords before the
	// call (the Off and default cases push ebp, which is 0 here, as their count) and the code at
	// the join reads its locals at [esp+n], so this case keeps the same three pushes and no more.
	// ecx (the engine object, loaded at 0x5facc4) and eax are left as the stock cases leave them.
	inline std::vector<std::uint8_t> fsaa_case_8x_bytes(const std::uint32_t param_cell, const std::uint32_t name, const std::uint32_t join, const std::uint32_t stub)
	{
		std::vector<std::uint8_t> code;
		const auto dword = [&code](const std::uint32_t value)
		{
			for (unsigned i = 0; i < 4; ++i) code.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
		};
		code.push_back(0x6a); code.push_back(0x08);         // push 8
		code.push_back(0x68); dword(name);                  // push "multiSampleType"
		code.push_back(0x6a); code.push_back(0x04);         // push 4
		code.push_back(0xff); code.push_back(0x15); dword(param_cell); // call [engine_param_cell]
		code.push_back(0xe9); dword(join - (stub + static_cast<std::uint32_t>(code.size()) + 4)); // jmp join (rel32 from the next instruction)
		return code;
	}

	// The private 5-entry jump table: the stock cases, plus the new one at `stub`.
	inline std::vector<std::uint8_t> fsaa_table_bytes(const std::uint32_t stub)
	{
		std::vector<std::uint8_t> table;
		const auto dword = [&table](const std::uint32_t value)
		{
			for (unsigned i = 0; i < 4; ++i) table.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
		};
		dword(fsaa_case_off);
		dword(fsaa_case_2x);
		dword(fsaa_case_4x);
		dword(fsaa_case_6x);
		dword(stub);
		return table;
	}

	// The switch's writes: the widened bound and the repointed table. `table` is where the
	// private table lives (allocated by the installer).
	inline std::vector<xml1_hook_transaction::patch> fsaa_switch_writes(const std::uint32_t table)
	{
		return {
		    {fsaa_switch_cmp, {0x83, 0xf8, 0x03}, {0x83, 0xf8, 0x04}},
		    {fsaa_table_operand, {0x40, 0xb2, 0x5f, 0x00}, xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(table)))},
		};
	}

	// The wipe guard's writes: each failure exit's first call becomes a call to `log_stub`
	// (which leaves the settings alone and says so), the second is NOPed.
	inline std::vector<xml1_hook_transaction::patch> wipe_guard_writes(const std::uint32_t log_stub)
	{
		const auto reroute = [log_stub](const std::uint32_t site, std::initializer_list<std::uint8_t> original)
		{
			const auto rel = static_cast<std::int32_t>(static_cast<std::int64_t>(log_stub) - (static_cast<std::int64_t>(site) + 5));
			xml1_hook_transaction::patch patch{site, original, {0xe8}};
			for (unsigned i = 0; i < 4; ++i) patch.after.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(rel) >> (8 * i)));
			return patch;
		};
		const auto nop = [](const std::uint32_t site, std::initializer_list<std::uint8_t> original)
		{
			return xml1_hook_transaction::patch{site, original, std::vector<std::uint8_t>(original.size(), 0x90)};
		};
		return {
		    reroute(wipe_resolution_call_a, {0xe8, 0x95, 0x0f, 0x02, 0x00}),
		    nop(wipe_settings_call_a, {0xe8, 0x00, 0x0f, 0x02, 0x00}),
		    reroute(wipe_resolution_call_b, {0xe8, 0x50, 0x0d, 0x02, 0x00}),
		    nop(wipe_settings_call_b, {0xe8, 0xbb, 0x0c, 0x02, 0x00}),
		};
	}
}
