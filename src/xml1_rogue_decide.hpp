#pragma once
#include <Windows.h>
#include <string>
namespace xml1_rogue_decide
{
void install(HMODULE game);
std::string status();
bool requested();
bool installed();
bool verified();
}
