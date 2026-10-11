#pragma once
#include <Windows.h>
namespace gameplay_agreement
{
	// Called once after gameplay hooks settle, before any game code runs.
	bool install(HMODULE game, bool installed_rule);
	// Native registered callbacks, before either handler admits a participant.
	int receive_request(void* event);
	int receive_reply(void* event);
	int receive_info(void* event);
	int receive_ack(void* event);
}
