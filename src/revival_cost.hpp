#pragma once
#include <Windows.h>

// [Game] RevivalCost=xml1: the first game's revival price (revival_cost_rules.hpp). Without the key, retail.
namespace revival_cost
{
	void install(HMODULE game);
	bool requested();
	bool installed();
	bool verified(); // not requested, or installed
}
