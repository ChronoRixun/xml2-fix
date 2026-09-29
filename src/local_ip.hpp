#pragma once

#include "local_ip_rules.hpp"

#include <optional>
#include <string>
#include <vector>

// What [Online] LocalIP=auto asks Windows (local_ip_rules.hpp has the decisions). Also built into
// xml2_test, which checks the fix's answers against these.

namespace local_ip
{
	// The IPv4 address Windows would send from to reach the internet: the source address of its best
	// route to a public address (nothing is sent). Nothing without such a route.
	std::optional<local_ip_rules::address> default_route_address();

	// This PC's name as a lookup may spell it: gethostname's (what XMen2.exe's GameSpy code resolves,
	// and what Windows answers "localhost" with), the DNS host name and the fully qualified one.
	std::vector<std::string> own_host_names();
}
