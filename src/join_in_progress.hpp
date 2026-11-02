#pragma once
#include <Windows.h>

// [Online] JoinInProgress=1 (requested-only; gameplay rule 26 JoinInProgress, docs/join-in-progress.md; needs
// [Online] Transport=gns): with LateJoin=1 the host admits a waiting newcomer after Start Game and the next zone
// load sends one live campaign to the cohort. LateJoin=0 (default) refuses after START before taking a seat. Absent:
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
	// Game/UI thread, before posting a host session; no INI or frozen agreement mutation.
	bool late_join_configurable();
	bool set_late_join_enabled(bool enabled);
	bool late_join_enabled(); // installed rule 26 AND [Online] LateJoin=1; default is lobby-only
	// A script is about to change the party (seatParty): the heroes the players hold right now, for a solo segment
	// that may start at the next zone load. Lockstep: the script runs on the same frame on every PC.
	void note_party_change();
	// p060fix/AR, game thread only: a newcomer lost between its admission and the shared start. The host's admission
	// (seats adding/streaming/starting, the announced boundary mask, newcomers already decided lost); the restart's
	// window on every role (from its 0x26 until its own restart load was seen: no removal inside it).
	bool admission_newcomer(int index);
	unsigned char admission_remaining(int lost);
	void newcomer_lost_decided(int index);
	bool restart_begun();
	bool restart_started();
	bool restart_removal_held();
}
