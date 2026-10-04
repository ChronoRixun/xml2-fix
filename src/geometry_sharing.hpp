#pragma once
#include <Windows.h>
#include <string>

namespace geometry_sharing
{
 void install(HMODULE game);
 // Aggregate diagnostics only, exposed through the existing harness status.
 std::string status();
}
