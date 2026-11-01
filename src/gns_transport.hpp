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
	// AQ3, game thread only: admission decision at the synchronous area boundary. Existing guests wait for the
	// host's reliable same-frame control before loading; servicing this wait receives/queues game packets but never
	// dispatches native game handlers or steps the world. The initial load and cohort restart do not use this wait.
	void announce_join_boundary(std::uint16_t frame, std::uint8_t newcomers);
	bool poll_join_boundary(std::uint16_t frame, std::uint8_t& newcomers);
	void service_join_boundary();
	void clear_join_boundaries();
	// p060fix/AR, game thread only: a newcomer lost between its admission and the cohort restart. The host alone
	// decides (reliable link kind 11, before the native 0x2b and the restart's 0x26) and removes it at once; every
	// other PC records the admission's newcomers (boundary mask, mesh newcomers), never removes one by its own
	// timeout before its restart, and carries out the host's decision at once or at its restart entry at the latest.
	void note_admission_newcomers(std::uint8_t mask);
	void newcomers_restart_entry(const char* where, bool session_ended = false);
	bool newcomer_decisions_carried(const char* where); // a non-host PC: every host decision carried out (else its restart waits)
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
		std::uint32_t connection = 0; // authenticated room tunnel; its game link may still be unbound
		bool ring = false;            // its lockstep input ring was readable
		std::uint16_t next = 0;       // the ring's next expected frame
		char name[32]{};              // the native participant name (record +0x40, <= 31 bytes)
	};
	struct party_facts
	{
		bool session = false, hosting = false, loading = false, campaign_running = false;
		unsigned count = 0;
		party_participant participants[4];
	};
	bool party_snapshot(party_facts& out);
	// The host removes a remote participant in session (docs/online-party-menu.md): the agreed-departure path, never a pre-game leave.
	bool remove_member(unsigned native_index);
	// Rule 64 (online_removal_core.hpp): a joiner whose room host's removal notice was accepted hands the session the retail
	// removal notice 0x2b for its own index (reason 0x10ce, flag 1 - what the host's native Kick sends) through its bound link
	// to the host, so the native removal and the 0.6 Kick end path run unchanged. False: no running guest session at that
	// index or no bound live link to the host (the caller retries).
	bool deliver_removal(unsigned own_index);
}
