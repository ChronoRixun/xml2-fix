#pragma once
#include <Windows.h>
#include <string>

// Candidate rule 45 revision 2: ground grab, pickup/recovery lifecycle, and
// flying-charge collision. Carry/upkeep and flight admission remain deferred.
// OFF leaves the executable and agreement unchanged.
namespace xml1_grab_carry
{
void install(HMODULE game);
std::string status();
bool requested();
bool installed();
bool verified();
}
