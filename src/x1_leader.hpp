#pragma once
#include <Windows.h>
#include <cstdint>

namespace x1_leader
{
	// [Game] X1Leader: X-Men Legends 1's stats `leader` bit (issue #15); the decision itself lives
	// in x1_leader_rules.hpp so the test executable can check it without the game.
	bool installed();
	bool verified();
	void install(const HMODULE game);
	// Test seam: give the names install() would have loaded from Data\x1leader.txt (the production
	// path reads that file from the DLL's own directory; a call before install() replaces it).
	void give_for_testing(const char* sidecar_text);
}
