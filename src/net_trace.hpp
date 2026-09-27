#pragma once

#include <Windows.h>

// Optional diagnostics for online play ([Debug] LogNetwork=1 in xml2-fix.ini): logs the
// game's connections and datagrams - where they go, their size and any readable text (GameSpy
// queries are plain "\key\value" text; server list replies are encrypted, so only sizes help).

namespace net_trace
{
	void install(HMODULE module);
}
