#pragma once

// The team-combo popup without one of its two heads (owner's crash report 2026-10-23, beta 1: XMen2.exe+0x1a6848).
// Kept apart from the patch so the tests can check every retail byte it relies on.
//
// What XMen2.exe does. When two heroes land a combo (0x450a40..0x450cf3: the combo XP and the greatest-combo count),
// the HUD's vt+0xb4 (slot 0x69dd58 -> thunk 0x5996f0 `add ecx,0x87c30; jmp 0x5a6980`) shows the combo popup, a HUD
// widget at HUD+0x87c30 (vtable 0x69dbac). Its show (0x5a6980(heroA, heroB, type, amount, text)) frees the previous
// heads (0x5a64a0), then builds one head model per hero through 0x5d59d0 (a CModel handle from 0x575c90, then the
// head lookup 0x5f4ec0) into widget+0x1c / +0x20 and makes the widget visible (vt+0x10(1), 0x5a6b2d) - whether or not
// both heads were built. 0x575c90 creates nothing when any model component pool it needs is full (its can-allocate
// check 0x5756e0; SPEC-runtime-limits 2.4), and a hero without stats (+0x35c) gets no head either. The popup's draw
// (vt+0xc = 0x5a6500) then calls through both heads without a null test: `mov ecx,[esi+0x1c]; mov eax,[ecx]` at
// 0x5a67fe and `mov ecx,[esi+0x20]; mov edx,[ecx]` at 0x5a6845. The owner's dump: widget+0x1c a head, +0x20 null,
// the CModelIGB pool (aggregate 0x7bf6d0 + 0xe5d4) at its retail capacity 400/400 - the second head could not be made.
//
// The guard: the HUD slot 0x69dd58 points at the fix's own show, which runs the game's and then, when the popup is
// visible (+0x18) with either head missing, hides it through the widget's own vt+0x10(0) (0x5a6450: detaches the head
// that exists and clears +0x18) - the same call the popup's own timeout makes (0x5a6439). The combo itself (its XP,
// the count) was already credited by the caller; only the picture of it is not shown. Display only: no peer sees a
// difference in game state, so it needs no gameplay-agreement record.

#include "limits_rules.hpp" // guard, matches, image_base

#include <array>
#include <cstdint>

namespace combo_heads_rules
{
	using limits_rules::guard;
	using address = std::uint32_t;

	constexpr address hud_show_slot = 0x69dd58; // HUD vtable 0x69dca4 + 0xb4
	constexpr address retail_show = 0x5996f0;   // add ecx,0x87c30; jmp 0x5a6980
	constexpr std::uint32_t widget_offset = 0x87c30;
	constexpr std::uint32_t visible_offset = 0x18;
	constexpr std::uint32_t head_a_offset = 0x1c;
	constexpr std::uint32_t head_b_offset = 0x20;
	constexpr std::uint32_t hide_slot_offset = 0x10; // widget vt+0x10 = 0x5a6450(bool shown)
	constexpr address igb_pool_live = 0x7bf6d0 + 0xe5d4;    // CModelIGB live count (retail capacity 400)
	constexpr address shaderfx_pool_live = 0x7bf6d0 + 0x80d0; // CModelShaderFx live count (retail capacity 432)

	inline constexpr std::array<guard, 9> guards{{
		{hud_show_slot, "f0965900", "the HUD vtable's combo popup show (0x69dd58 -> 0x5996f0)"},
		{retail_show, "81c1307c0800e985d20000", "the show thunk: add ecx,0x87c30; jmp 0x5a6980 (0x5996f0)"},
		{0x450cc5, "8bceff93b4000000", "the combo code's call of HUD vt+0xb4 (0x450cc5)"},
		{0x5a6980, "535556578b7c2414833f008bf10f849d", "the popup's show (0x5a6980)"},
		{0x5a6515, "8a461884c00f842d", "the popup's draw: its visible test (0x5a6515)"},
		{0x5a67fe, "8b4e1c8b01ff5008", "the popup's draw: the first head, no null test (0x5a67fe)"},
		{0x5a6845, "8b4e208b11ff5208", "the popup's draw: the second head, no null test (0x5a6845)"},
		{0x69dbbc, "50645a00", "the popup widget's vt+0x10 (0x69dbac + 0x10 -> 0x5a6450)"},
		{0x5a6450, "538a5c2408578bf9385f18743155568d", "the popup's show/hide (0x5a6450)"},
	}};

	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - limits_rules::image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	// Which heads a visible popup lacks: 0 none (or the popup is not visible), bit 0 the first, bit 1 the second.
	inline unsigned missing_heads(const std::uint8_t* widget)
	{
		if (!widget[visible_offset])
		{
			return 0;
		}
		std::uint32_t a = 0, b = 0;
		for (int i = 0; i < 4; ++i)
		{
			a |= std::uint32_t(widget[head_a_offset + i]) << (8 * i);
			b |= std::uint32_t(widget[head_b_offset + i]) << (8 * i);
		}
		return (a ? 0u : 1u) | (b ? 0u : 2u);
	}
}
