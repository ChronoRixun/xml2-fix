#pragma once
#include <Windows.h>

namespace harm_loop_startup
{
	void install(HMODULE game);
	bool requested();
	bool installed();
}
