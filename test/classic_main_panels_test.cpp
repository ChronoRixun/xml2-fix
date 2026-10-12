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
	{
		// The live wide layout of candidate 0.2.0i (planned regions, label local y offsets +0x7a).
		const rows live{{{167,250,268,30},{167,219,268,27},{167,191,268,27},{167,163,268,27},{167,135,268,27},{167,107,268,27},{167,79,268,27},{167,51,268,29}}};
		const int local_y[8]{24,20,20,20,20,20,20,20};
		// The generic handler's label test (0x5c6490): the same rectangle moved up by local_y / 2.
		const auto generic=[&](int i,int x,int y){ const auto& r=live[i]; const int s=local_y[i]/2;
			return x>=r.x && x<r.x+r.width-1 && y>=r.y-r.height-1+s && y<r.y+s; };
		int flips=0, kept=0;
		for (int focused=0; focused<8; ++focused)
			for (int y=0; y<384; ++y)
				for (int x : {168, 300, 433})
				{
					if (!contains(live[focused],x,y)) continue;
					// Without the hook: the generic handler would focus the first other label containing the point.
					bool other=false; for (int i=0;i<8;++i) if (i!=focused && generic(i,x,y)) other=true;
					flips+=other;
					// With it: every move or press inside the focused row is answered for that row.
					kept+=focused_row_keeps(live,focused,x,y,0x200) && focused_row_keeps(live,focused,x,y,0x201);
					assert(!focused_row_keeps(live,focused,x,y,0x202)); // the release still accepts natively
				}
		assert(flips>0); // the maintainer-visible flicker in each row's lower third exists without the hook
		int inside=0; for (int f=0;f<8;++f) for (int y=0;y<384;++y) for (int x:{168,300,433}) inside+=contains(live[f],x,y);
		assert(kept==inside);
		assert(!focused_row_keeps(live,-1,300,235,0x200) && !focused_row_keeps(live,0,300,218,0x200)); // no focus / another row: native
		assert(focused_row_keeps(live,7,300,21,0x200) && !focused_row_keeps(live,7,300,51,0x200)); // Quit's own region only
		std::printf("classic main panels: %d of %d focused-row points would have been refocused by the generic handler\n",flips,inside);
	}
	std::puts("classic main panels: full widths, seam splitting, layouts, malformed rows and focused-row mouse PASS");
}
