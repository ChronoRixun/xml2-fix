#pragma once
#include <Windows.h>
#include <string>
namespace primitives_per_effect {
void install(HMODULE game);
std::string status();
// The admission bound in effect: 10 (retail) or the installed PrimitivesPerEffect value.
unsigned capacity();
}
