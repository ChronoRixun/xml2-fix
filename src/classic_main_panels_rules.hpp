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
}
