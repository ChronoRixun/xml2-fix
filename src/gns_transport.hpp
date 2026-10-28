#pragma once
#include <Windows.h>
#include <cstdint>

// [Online] Transport=gns (requested-only; gameplay rule 25 GnsTransport, docs/gns-transport.md): the game's messages
// between session participants over GameNetworkingSockets, whole and on the game's thread. Absent or any other value:
// nothing is written and the stock transport is byte-identical.
namespace gns_transport
{
	void install(HMODULE game);
	bool requested();
	bool installed();
	// Menu-thread pump after the room worker loaded the authenticated identity.
	void room_ui_tick();
	bool room_can_start();
	// Online Party (docs/online-party.md): the native session facts the lobby cards and member reports are derived
	// from, read on the game thread with guarded field reads only (no native calls, nothing written). Connections are
	// HSteamNetConnection values of BOUND room links (0: none / not a room link). False when Transport=gns is not
	// installed or no native session is set up.
	struct party_participant
	{
		int index = -1;               // session index (participant record +0x18; the host is 0)
		bool local = false;           // record flags bit 0: this PC
		bool ready = false;           // record flags bit 2 (0x614240)
		bool pending = false;         // join_in_progress::pending_player: outside the lockstep until the next area
		std::uint32_t connection = 0; // its bound room link
		bool ring = false;            // its lockstep input ring was readable
		std::uint16_t next = 0;       // the ring's next expected frame
		char name[32]{};              // the native participant name (record +0x40, <= 31 bytes)
	};
	struct party_facts
	{
		bool session = false, hosting = false, loading = false;
		unsigned count = 0;
		party_participant participants[4];
	};
	bool party_snapshot(party_facts& out);
}
