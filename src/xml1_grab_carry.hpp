#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

// Candidate rule 45 revision 3: ground grab, pickup/recovery, flying-charge
// collision, flying carry, fix7 admission and XML1 flight upkeep. Requires the
// published AnimEnums layout and the XML1 object LiftRule.
// OFF leaves the executable and agreement unchanged. [Game] Xml1FlyingCarry=0 keeps everything but the flying carry
// (its five handlers and the descent admission).
namespace xml1_grab_carry
{
void install(HMODULE game);
std::string status();
bool requested();
bool installed();
bool verified();
// The rule's agreement mask: 15 with the flying carry, 23 with [Game] Xml1FlyingCarry=0.
std::uint32_t feature_mask();
// false only for [Game] Xml1FlyingCarry=0 (default on).
bool flying_carry();
using telekinesis_pair_test=bool(__cdecl*)(std::uint32_t,std::uint32_t);
// Installed rule46 publishes its generation-bearing TK ownership predicate.
void set_telekinesis_pair_test(telekinesis_pair_test predicate);
}
