#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace classic_main_panels_rules
{
	struct rectangle { std::int16_t x, y, width, height; };
	using rows = std::array<rectangle, 8>;
	inline bool local_offsets(const rectangle& before, const std::array<std::int16_t,2>& local,
	                          const rectangle& after, std::array<std::int16_t,2>& result)
	{
		const std::array<int,2> planned{int(local[0])+after.x-before.x, int(local[1])+after.y-before.y};
		for (int value : planned)
			if (value<std::numeric_limits<std::int16_t>::min() || value>std::numeric_limits<std::int16_t>::max()) return false;
		result={std::int16_t(planned[0]),std::int16_t(planned[1])};
		return true;
	}
	// The native hit-test's RECT is [x,x+width-1) by [y-height-1,y).
	// Split overlapping panel seams at their midpoint, producing disjoint rows.
	inline bool regions(const rows& panels, rows& result)
	{
		std::array<int, 8> lower{}, upper{};
		for (std::size_t i = 0; i < panels.size(); ++i)
		{
			const auto& p = panels[i];
			if (p.width < 2 || p.height < 2 || p.width > 1024 || p.height > 128) return false;
			lower[i] = int(p.y) - p.height - 1; upper[i] = p.y;
			if (i && (p.y >= panels[i-1].y || p.x != panels[0].x || p.width != panels[0].width)) return false;
		}
		for (std::size_t i = 1; i < panels.size(); ++i)
			if (lower[i-1] < upper[i])
			{
				const int seam = (lower[i-1] + upper[i]) / 2;
				lower[i-1] = seam; upper[i] = seam;
			}
		rows planned{};
		for (std::size_t i = 0; i < panels.size(); ++i)
		{
			if (upper[i] - lower[i] < 3) return false;
			planned[i] = {panels[i].x, std::int16_t(upper[i]), panels[i].width,
			              std::int16_t(upper[i] - lower[i] - 1)};
		}
		result = planned;
		return true;
	}

	// The native hit test's half-open RECT for a row (PtInRect: right and bottom excluded).
	inline bool contains(const rectangle& r, const int x, const int y)
	{
		return x >= r.x && x < r.x + r.width - 1 && y >= r.y - r.height - 1 && y < r.y;
	}

	// MAIN_MENU's mouse handler (vt+0x80, 0x5c9320) hit-tests its rows with [y - h - 1, y); when the
	// row under the point is already focused and the message isn't the click's release, it does
	// nothing itself and falls through to the generic menu handler (0x5ae0a0), which hit-tests every
	// item with that item's own test - for the row labels 0x5c6490, whose rectangle is moved up by half
	// the label's local y offset (+0x7a / 2: 10 view units with the panel layout). That rectangle
	// reaches into the row above, so the generic handler focuses the row below while the point is
	// still in the focused row: every move in the lower third of a row flips the focus between the two,
	// and a press there can move the focus away before the release, which then only refocuses.
	// Returns whether the point belongs to the focused row (`focused`: its index, -1 for none) for a
	// message the generic handler would take over (a move or a left press): the hook then answers it
	// without the fallthrough. Everything else goes to the native handler unchanged.
	inline bool focused_row_keeps(const rows& planned, const int focused, const int x, const int y, const unsigned message)
	{
		if (message != 0x200 && message != 0x201) return false; // WM_MOUSEMOVE, WM_LBUTTONDOWN
		if (focused < 0 || focused >= static_cast<int>(planned.size())) return false;
		return contains(planned[static_cast<std::size_t>(focused)], x, y);
	}
}
