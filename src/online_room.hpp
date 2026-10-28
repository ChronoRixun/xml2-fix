#pragma once
#include <Windows.h>
#include <cstdint>
#include "online_browser_core.hpp"

#if XML2_FIX_GNS
#include <steam/steamnetworkingsockets.h>
#endif

// [Online] Host=broker / JoinCode=XXXXXXXX (docs/online-rooms.md): joining over the internet. The PC talks to the
// project broker (net030-broker SPEC.md v2 over TLS: identity, certificate, TURN credentials, rooms, signaling), opens a
// GameNetworkingSockets P2P connection (ICE, TURN relay as fallback) to each room member, and carries the game socket's
// datagrams to and from a member through it, so the stock lobby, handshake and admission run unchanged against the
// member's virtual address 10.77.x.y. After admission the GNS transport (gns_transport, rule 25) carries the game's
// messages on the same connection. Neither key set: nothing is hooked or written.
namespace online_menus { struct lobby_fixture; }
namespace online_room
{
	void install(HMODULE game);
	bool requested();
	// Process exit: online-status.json says `ended` (loader-lock safe).
	void shutdown();
	// Game/UI thread only. Network/key I/O runs on one worker; one outstanding operation.
	// Browsing/authentication never creates a room. Native admission is separate.
	bool available();
	const online_browser_core::client_view& view();
	void ui_tick();
	bool list();
	bool host(const std::string& name, bool public_room);
	bool host_native(const std::string& name, bool public_room, int max_players, bool danger_room);
	bool host_posted(online_browser_core::ticket generation);
	// Game-thread admission target, only for the current authenticated room and proven host tunnel.
	bool admission_target(online_browser_core::ticket generation, std::uint32_t& address);
	bool resolve(const std::string& code);
	bool retry();
	void cancel();
	void leave();
	// A native session end: leave after the transport tick that flushes the quit notices retail just queued.
	void leave_after_flush();
	std::string version();
	bool take_preset(online_browser_core::launch_preset& out);
	bool refresh_due();
	// Round 15 (presentation): milliseconds until the shared limiter/backoff lets a new request start (0 = now), and whether this
	// joiner's connection to the host of its room has ended since it connected (latched until the room is left).
	std::uint32_t retry_in_ms();
	bool host_lost();
	// For the code that learns the host removed THIS PC on purpose (a kick): the connection ending after it is not a host loss.
	void suppress_host_lost();
	// Rule 64 OnlineRemovalNotice (online_removal_core.hpp): this joiner accepted its room host's notice that the host removed it
	// in session (latched until the room is left; the Hub then shows "The host removed you from the game.").
	bool removed_by_host();
#if XML2_FIX_GNS
	// The identity GNS starts with (str:legends:<hex16> of this install's key); false when rooms are off or the key
	// could not be loaded (then the PC stays without rooms, logged).
	bool identity(SteamNetworkingIdentity& out);
	// On the game's thread, from the GNS transport's tick once GNS runs: broker events, certificate, connections.
	void tick();
	// The GNS status callback, first: true when it was a room connection's event this module consumed.
	bool on_status(SteamNetConnectionStatusChangedCallback_t* info);
	// The connected P2P connection to the member at this virtual address (host byte order), or invalid.
	HSteamNetConnection connection_for(std::uint32_t virtual_address);
	bool owns(HSteamNetConnection connection);
	// The non-datagram messages that arrived on a room connection (the GNS transport's), oldest first; the caller
	// releases them. Before the first true, only hello/hello_ack are retained.
	void set_transport_bound(HSteamNetConnection connection, bool bound);
	int fetch(HSteamNetConnection connection, SteamNetworkingMessage_t** out, int max);
	std::uint32_t own_virtual(); // host byte order, 0 before the room roster is resolved
	// Online Party (docs/online-party.md section 8): the lobby cards' observed facts and accepted claims for the menus
	// (game/UI thread; false outside a room). Never admission: native rows and readiness stay the menu's native reads.
	bool party(online_menus::lobby_fixture& out);
	// The measured RTT (ms) to the host of the room with this code when this PC is in it as a joiner, else -1.
	int room_rtt(const std::string& code);
	// The host starts rule 64's removal notice to the member on this room connection (native index 1-3) and sends the first
	// one now; false when this PC does not host a ready room, the connection is not a connected member, or one is pending.
	bool removal_start(HSteamNetConnection connection, unsigned native_index);
#endif
}
