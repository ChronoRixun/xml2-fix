#pragma once

#include "script_functions_rules.hpp"

#include <Windows.h>

#include <span>

// [Limits] ScriptFunctions: XMen2.exe keeps its script function names in a tree of 320 (the game's 289,
// its 19 builtins, and the fix's own functions behind them, forced_teams.hpp), and a registration past
// the 320th is dropped without a word - a script calling that function then loses the statement when it
// compiles. The raise keeps the tree as it is and puts every name past it in a table of the DLL's
// (1024 names in all by default, 320 = the game's own limit, at most 4096): two code sites of the game's,
// installed as one checked transaction before the game's own code runs; a refusal leaves the game's limit
// with one log line. script_functions_rules.hpp has the details.
//
// [Test] ScriptFunctionProbes=N (1..64, test builds only) registers xml2fixProbe1..N, n(i), behind the
// fix's own functions; each call writes its number to xml2-fix.log - a probe past the 320th proves the
// raise in game.
namespace script_functions
{
	// Reads [Limits] ScriptFunctions and installs the raise. From DllMain, before forced_teams::install
	// (whose table carries the probes) and before XMen2.exe's own code runs.
	void install(HMODULE game);

	// The total the game can register now: 320 when the raise isn't installed.
	int capacity();

	// The probe entries [Test] ScriptFunctionProbes asked for (empty without it); the registration's table
	// appends them after the fix's own functions.
	std::span<const script_functions_rules::func_entry> probes();
}
