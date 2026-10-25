#pragma once

// [Game] ClassicObjectives (docs/classic-objectives.md): the PDA's Objectives page follows the first game.
// X-Men Legends lists the visible objectives of the current mission and the player reads the first unticked one;
// the PC PDA has seven selector slots (slot 7 is the secondary-objective page while one exists), maps slot n to the
// n-th top-level major objective even when that objective is hidden (an empty hole in the column), and opens on the
// last page viewed or the first slot. With the rule a slot is the n-th VISIBLE top-level major objective, counted
// from a window offset chosen when the page opens so that the current objective has a slot, and the page opens on it.
// Pure rules; tested in test/classic_objectives_test.cpp.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace classic_objectives_rules
{
	// XMen2.exe objective records (the mission object's vt+0x34 array, stride 0x1ac, count at array+0x7d70).
	constexpr std::uint32_t record_stride = 0x1ac;
	constexpr std::uint32_t record_count_offset = 0x7d70;
	constexpr std::uint32_t parent_offset = 0x20;   // parentname char[32]: empty for a top-level objective
	constexpr std::uint32_t flags_offset = 0x1aa;   // bit 6: major (the PDA's selector test, 0x5cce98)
	constexpr std::uint8_t major_bit = 0x40;
	constexpr int native_slots = 7;                 // icon01..icon07 (0x5cdd70's loop, 0x5cd900's draw)
	constexpr std::uint32_t remembered_focus = 0x8afe00; // char[3][64]: focused item name per PDA mode
	constexpr std::size_t remembered_focus_size = 0x40;
	constexpr std::uint32_t mode_offset = 0x18d8;   // PDA_MENU: current mode (1 = objectives)

	struct objective
	{
		bool major_top; // major and top-level: what a selector slot shows
		bool hidden;    // the native hidden/disabled state bit (0x488ad0)
		bool complete;  // the native completion bit (0x488ab0)
		bool secondary_top = false; // not major, top-level, visible: the secondary page (slot 7) exists
	};

	struct plan
	{
		int offset = 0;  // visible majors skipped before slot 1
		int focus = -1;  // 0-based slot to open on, -1: leave the native choice
	};

	// The slot count for majors: six. The page-text walk (0x5ce0f8) always shows the optional-objectives page for
	// slot 7 (cmp 7 before its major walk), so a seventh major's page can never be shown; the window keeps the
	// current objective among slots 1-6 (in game: a seventh major in slot 7 opened "Optional Objectives").
	template <class Range>
	int major_slots(const Range&)
	{
		return native_slots - 1;
	}

	// Current = the first visible major the player has not completed (XML1's first unticked line); when every one is
	// complete, the last one. The window starts at the current objective, or as late as still fills every slot.
	template <class Range>
	plan plan_for(const Range& objectives)
	{
		const int slots = major_slots(objectives);
		int visible = 0, current = -1;
		for (const auto& o : objectives)
		{
			if (!o.major_top || o.hidden) continue;
			if (current < 0 && !o.complete) current = visible;
			++visible;
		}
		plan p;
		if (!visible) return p;
		if (current < 0) current = visible - 1;
		const int latest_start = visible > slots ? visible - slots : 0;
		p.offset = current < latest_start ? current : latest_start;
		p.focus = current - p.offset;
		return p;
	}

	// Whether the record at `index` fills a slot under the window: a visible top-level major with at least
	// `offset` visible ones before it. The native walk then counts these records in its own order.
	template <class Range>
	bool fills_slot(const Range& objectives, std::size_t index, int offset)
	{
		std::size_t i = 0;
		int rank = 0;
		for (const auto& o : objectives)
		{
			const bool counted = o.major_top && !o.hidden;
			if (i == index) return counted && rank >= offset;
			if (counted) ++rank;
			++i;
		}
		return false;
	}

	// "icon%02d" for a 0-based slot (the PDA's selector item names, 0x6a1bb0).
	inline bool slot_name(int slot, char (&out)[remembered_focus_size])
	{
		if (slot < 0 || slot >= native_slots) return false;
		constexpr std::string_view prefix = "icon";
		for (std::size_t i = 0; i < sizeof out; ++i) out[i] = 0;
		for (std::size_t i = 0; i < prefix.size(); ++i) out[i] = prefix[i];
		out[4] = static_cast<char>('0' + (slot + 1) / 10);
		out[5] = static_cast<char>('0' + (slot + 1) % 10);
		return true;
	}

	struct guard { std::uint32_t va; std::string_view hex; const char* what; };
	constexpr std::uint32_t selector_test = 0x5cce98;   // 14 bytes: test [esi+0x18a],0x40 / je / cmp [esi],0 / jne
	constexpr std::uint32_t selector_counted = 0x5ccea6;
	constexpr std::uint32_t selector_skip = 0x5ccebd;
	constexpr std::uint32_t page_test = 0x5ce148;       // 9 bytes: test [esi],0x40 / je 0x5ce214
	constexpr std::uint32_t page_counted = 0x5ce165;
	constexpr std::uint32_t page_skip = 0x5ce214;
	constexpr std::uint32_t enter_call = 0x5ce484;      // call 0x5cdd70 (the objectives mode refresh)
	constexpr std::uint32_t enter_refresh = 0x5cdd70;
	constexpr std::uint32_t mission_getter = 0x48a0e0;
	constexpr std::uint32_t complete_lookup = 0x488ab0;
	constexpr std::uint32_t hidden_lookup = 0x488ad0;
	constexpr std::uint32_t results_guard = 0x4c87a0;   // Danger Room results page: native single-slot PDA
	constexpr guard guards[]{
		{selector_test, "f6868a01000040741c803e007517", "selector major/top-level test"},
		{0x5cce40, "81ec0c010000a1f8386f0053555657", "selector accessor"},
		{page_test, "f606400f84c3000000", "page major test"},
		{0x5ce151, "8a8e76feffff84c98d8696feffff0f85", "page top-level test"},
		{enter_call, "e8e7f8ffff", "objectives mode refresh call"},
		{enter_refresh, "81ecdc070000a1f8386f0056", "objectives mode refresh"},
		{mission_getter, "a108b1720085c075", "mission getter"},
		{complete_lookup, "0fb681a90100008a8018b17200c0e806", "completion lookup"},
		{hidden_lookup, "0fb681a90100008a8018b17200c0e807", "hidden lookup"},
		{results_guard, "8a0d2827780033c080f9ff0f95c0c3", "results-page guard"},
	};
}
