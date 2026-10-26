#pragma once
#include <Windows.h>

namespace input_worker
{
	// Retail controller enumeration shares one manager across all worker threads.
	void install(HMODULE game);
}
