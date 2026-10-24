#pragma once
#include <Windows.h>
#include <cstdint>

#if XML2_FIX_GNS
#include <steam/steamnetworkingsockets.h>
#endif

// [Online] Host=broker / JoinCode=XXXXXXXX (docs/online-rooms.md): joining over the internet. The PC talks to the
// project broker (net030-broker SPEC.md v2 over TLS: identity, certificate, TURN credentials, rooms, signaling), opens a
// GameNetworkingSockets P2P connection (ICE, TURN relay as fallback) to each room member, and carries the game socket's
// datagrams to and from a member through it, so the stock lobby, handshake and admission run unchanged against the
// member's virtual address 10.77.x.y. After admission the GNS transport (gns_transport, rule 25) carries the game's
// messages on the same connection. Neither key set: nothing is hooked or written.
namespace online_room
{
	void install(HMODULE game);
	bool requested();
	// Process exit: online-status.json says `ended` (loader-lock safe).
	void shutdown();
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
#endif
}
