#pragma once
#include <Windows.h>
#include <string>

// Optional acceptance telemetry. It forwards native calls without changing records or return values.
namespace save_trace
{
	void install(HMODULE game);
	std::string status();
}
