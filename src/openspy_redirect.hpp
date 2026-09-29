#pragma once

#include "local_ip_rules.hpp"
#include "online_rules.hpp"

#include <Windows.h>

// GameSpy's servers shut down in 2014. OpenSpy (openspy.net) runs compatible ones under the
// same host names with its own domain, e.g. xmenlegpc.master.gamespy.com ->
// xmenlegpc.master.openspy.net, so redirecting name lookups is all a GameSpy game needs.
// [Online] Server points every GameSpy name at one address instead (online_rules.hpp).
//
// The same hook puts the right one of this PC's addresses first when the game looks up its own
// name, which is how it picks its LocalIP and the addresses its heartbeats list ([Online] LocalIP,
// local_ip_rules.hpp).

namespace openspy_redirect
{
	// Redirects `module`'s gethostbyname lookups as `chosen` says and arranges the answers for this
	// PC's own name as `local` says. No hook when neither has anything to do.
	void install(HMODULE module, const online_rules::plan& chosen, const local_ip_rules::choice& local);
}
