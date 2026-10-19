#pragma once
#include <Windows.h>
#include <string>

namespace world_impact
{
	void install(HMODULE game);
	bool requested();
	bool installed();
	bool verified();
	std::string status();
}
