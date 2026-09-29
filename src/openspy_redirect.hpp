#pragma once

#include "online_rules.hpp"

#include <Windows.h>

// GameSpy's servers shut down in 2014. OpenSpy (openspy.net) runs compatible ones under the
// same host names with its own domain, e.g. xmenlegpc.master.gamespy.com ->
// xmenlegpc.master.openspy.net, so redirecting name lookups is all a GameSpy game needs.
// [Online] Server points every GameSpy name at one address instead (online_rules.hpp).

namespace openspy_redirect
{
	// Redirects `module`'s gethostbyname lookups as `chosen` says. Nothing for plan::mode::off.
	void install(HMODULE module, const online_rules::plan& chosen);
}
