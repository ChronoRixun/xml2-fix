#pragma once
#include <Windows.h>

// [Dev] NetFakeLoss (percent), NetFakeLag (ms), NetFakeJitter (ms, uniform 0..n added): the same send-side impairment
// for the stock UDP transport (the game's sendto) and the GNS transport (its fake-network config). Tests only.
namespace net_impair
{
	struct settings
	{
		float loss = 0;
		int lag = 0, jitter = 0;
		bool on() const { return loss > 0 || lag > 0 || jitter > 0; }
	};
	void install(HMODULE game);
	const settings& current();
}
