#pragma once
#include <Windows.h>

// [Online] JoinInProgress=1 (requested-only; gameplay rule 26 JoinInProgress, docs/join-in-progress.md; needs
// [Online] Transport=gns): after Start Game the host stays listed and admits a player while a seat is free; the
// newcomer stays outside the lockstep; the next zone load sends one live campaign to the whole cohort. Absent:
// nothing is written and Start Game is stock.
namespace join_in_progress
{
	void install(HMODULE game);
	// The host's bring-in between zone loads; the game's thread (the GNS receive pump).
	void tick();
	bool bringing_in(); // service the waiting/load scheduler states where the ordinary receive pump does not run
	bool can_service(); // control-message draining is safe only outside synchronous world construction
	bool pending_player(const void* record); // admission stays on the stock/tunnelled socket until native add
	bool requested();
	bool installed();
	// A script is about to change the party (seatParty): the heroes the players hold right now, for a solo segment
	// that may start at the next zone load. Lockstep: the script runs on the same frame on every PC.
	void note_party_change();
}
