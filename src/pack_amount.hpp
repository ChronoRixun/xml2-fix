#pragma once

#include <Windows.h>

#include <string>

// [Game] PackAmount=xml1: health and energy packs restore a quarter of the maximum, as X-Men Legends 1's do, instead
// of XMen2.exe's third (pack_amount_rules.hpp has the research and every byte). No key, or xml2: the game's own.
namespace pack_amount
{
	// Reads [Game] PackAmount and patches the two single-amount fractions. From DllMain, before the exe's entry point;
	// the script functions first run when a pack is used in play.
	void install(HMODULE game);

	// "pack amount xml1", "pack amount game's own" or "pack amount unavailable".
	std::string status();

	// Asked for (key xml1), installed or not: the gameplay agreement registers it (rule 13).
	bool requested();
	bool installed();
}
