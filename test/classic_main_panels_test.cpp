#include "classic_main_panels_rules.hpp"
#include <cassert>
#include <cstdio>
using namespace classic_main_panels_rules;
int main()
{
	for (int offset : {0, 48})
	{
		rows panels{}, out{};
		for (int i = 0; i < 8; ++i) panels[i] = {std::int16_t(100 + offset), std::int16_t(280 - 28*i), 260, 32};
		assert(regions(panels, out));
		for (int i = 0; i < 8; ++i)
		{
			assert(out[i].x == panels[i].x && out[i].width == panels[i].width);
			assert(out[i].y <= panels[i].y && out[i].y-out[i].height-1 >= panels[i].y-panels[i].height-1);
			if (i) assert(out[i].y <= out[i-1].y-out[i-1].height-1);
		}
		assert(out[7].y <= out[6].y-out[6].height-1); // Online/Quit never overlap
		const auto before = out; panels[4].width=0; assert(!regions(panels,out));
		assert(out[0].x==before[0].x && out[7].height==before[7].height); // no partial result
		panels[4]=panels[3]; assert(!regions(panels,out));
	}
	const rectangle before{100,250,100,17}, after{20,258,260,30};
	const std::array<std::int16_t,2> local{0,16}; std::array<std::int16_t,2> moved{};
	assert(local_offsets(before,local,after,moved));
	assert(before.x-local[0]==after.x-moved[0] && before.y-local[1]==after.y-moved[1]); // component origin unchanged
	assert(100+moved[0]+after.width/2==150); // same centered label, despite wider hit region
	const auto stable=moved; assert(local_offsets(after,moved,after,moved) && stable==moved);
	const auto saved=moved;
	assert(!local_offsets({-32768,0,100,17},{32767,0},{32767,0,100,17},moved) && moved==saved);
	std::puts("classic main panels: full widths, seam splitting, layouts and malformed rows PASS");
}
