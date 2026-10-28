#pragma once

#include <Windows.h>

#include <string>

// E12: the address space of XMen2.exe (large-address-aware when the Legends Classic build set the flag on its copy).
// Read-only apart from two reservations that change no game behaviour (address_space_rules.hpp): the 64 KB 2 GB guard
// (flag on only) and the test pipe's high-address test. Refuses nothing: with the flag off it only logs.
//
//   test pipe:  status                 ... "; memory laa on; top 0xFFFEFFFF; above 2 GB 812 MB; ..."
//               memory [status]        the same words alone
//               memory high <MB>       reserve the free space below 2 GB except <MB> (flag on only; test builds)
//   crash:      xml2-fix.log gets "crash: exception 0xC0000005 at 0x... (module+0x...), address 0x...; laa on; ..."
//               from an unhandled-exception filter that then hands the crash on as before.
namespace address_space
{
	void install(HMODULE game);           // from DllMain, before the game's code runs: the guard and the crash line
	const char* laa();                    // "on" / "off" / "unknown" (XMen2.exe's file header)
	std::string status();                 // the status words above
	std::string pipe(const std::string& arguments);
}
