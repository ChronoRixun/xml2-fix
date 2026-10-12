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
//    5th case asks for 8 samples - or 6, the most the stock switch knows, when the adapter's
//    8x support byte (set by CheckDeviceMultiSampleType on A8R8G8B8 fullscreen, FUN_005f5dd0)
//    is clear, so an unsupported 8x still gets a device.
//
// 2. The failure-path settings wipe. When the display device can't be created (the window/
//    device open call returns false, or the visual context reports an error), the game
//    persists Resolution="800x600" (FUN_0061bef0) and FSAA=0 (FUN_0061be60) before quitting,
//    at 0x5faf56/0x5faf5b and again at the second failure exit 0x5fb19b/0x5fb1a0. A failure
//    the display rewrite can fix on the next boot destroys the player's settings first. The
//    patch routes the first call of each pair to a logging stub and NOPs the second, so the
//    game still quits on a fatal device failure but leaves the registry untouched.

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
	constexpr std::uint32_t support_byte_8x = 0xa09e87;        // 1 when the adapter offers 8x fullscreen

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

	// The bytes that must be in place before anything is written: the switch's own instructions,
	// its jump table, the cases the table names, and both failure exits with their wipe calls.
	inline constexpr guard guards[]{
	    {0x5facbc, "a1988ca60083f8038b4938", "the boot FSAA switch (mov eax,[0xa68c98]; cmp eax,3)"},
	    {0x5facce, "ff248540b25f00", "the switch's indexed jump (jmp [eax*4+0x5fb240])"},
	    {stock_fsaa_table, "d5ac5f00e5ac5f00f6ac5f0007ad5f00", "the jump table's four cases (Off/2x/4x/6x)"},
	    {fsaa_case_off, "5568083d6a006a04ff1570f56700", "the Off case (0 samples)"},
	    {fsaa_case_2x, "6a0268083d6a006a04ff", "the 2x case"},
	    {fsaa_case_4x, "6a0468083d6a006a04ff", "the 4x case"},
	    {fsaa_case_6x, "6a0668083d6a006a04ff", "the 6x case"},
	    {fsaa_default_case, "5568083d6a006a04ff1570f56700", "the default case (> 3: 0 samples, persist Off)"},
	    {0x5faf4f, "c6052d3a6f0001e8950f0200e8000f0200", "the first failure exit (quit flag; call FUN_0061bef0; call FUN_0061be60)"},
	    {0x5fb194, "c6052d3a6f0001e8500d0200e8bb0c0200", "the second failure exit (the same pair)"},
	};

	// The 5th jump-table entry's case, built at the address `stub` (the table's 5th dword):
	// 8 samples when the adapter's 8x support byte is set, else 6 (the highest level the stock
	// cases know), then the engine-parameter call and the join every case lands on.
	//     mov al, [support_byte] / test al,al / jnz +3 / push 6 / jmp +5 / push 8 /
	//     push "multiSampleType" / push 4 / call [engine_param_cell] / jmp join
	inline std::vector<std::uint8_t> fsaa_case_8x_bytes(const std::uint32_t support_byte, const std::uint32_t param_cell, const std::uint32_t name,
	                                                    const std::uint32_t join, const std::uint32_t stub)
	{
		std::vector<std::uint8_t> code;
		const auto dword = [&code](const std::uint32_t value)
		{
			for (unsigned i = 0; i < 4; ++i) code.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
		};
		code.push_back(0xa0); dword(support_byte);          // mov al, [support_byte]
		code.push_back(0x84); code.push_back(0xc0);         // test al, al
		code.push_back(0x75); code.push_back(0x03);         // jnz +3 (to push 8)
		code.push_back(0x6a); code.push_back(0x06);         // push 6
		code.push_back(0xeb); code.push_back(0x05);         // jmp +5 (over the push 8)
		code.push_back(0x6a); code.push_back(0x08);         // push 8
		code.push_back(0x68); dword(name);                  // push "multiSampleType"
		code.push_back(0x6a); code.push_back(0x04);         // push 4
		code.push_back(0xff); code.push_back(0x15); dword(param_cell); // call [engine_param_cell]
		code.push_back(0xe9); dword(join - (stub + code.size() + 4)); // jmp join (rel32 from the next instruction)
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
