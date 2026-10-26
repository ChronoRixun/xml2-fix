#pragma once
#include <Windows.h>
#include <string>

// Candidate rule 45 revision 3: ground grab, pickup/recovery, flying-charge
// collision, flying carry, fix7 admission and XML1 flight upkeep. Requires the
// published AnimEnums layout and the XML1 object LiftRule.
// OFF leaves the executable and agreement unchanged.
namespace xml1_grab_carry
{
void install(HMODULE game);
std::string status();
bool requested();
bool installed();
bool verified();
}
