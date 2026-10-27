#pragma once
#include <Windows.h>

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
}
