#pragma once

#include <Windows.h>

// GameSpy's servers shut down in 2014. OpenSpy (openspy.net) runs compatible ones under the
// same host names with its own domain, e.g. xmenlegpc.master.gamespy.com ->
// xmenlegpc.master.openspy.net, so redirecting name lookups is all a GameSpy game needs.

namespace openspy_redirect
{
	// Redirects `module`'s gethostbyname lookups of *.gamespy.com to *.<domain>.
	void install(HMODULE module, const char* domain);
}
