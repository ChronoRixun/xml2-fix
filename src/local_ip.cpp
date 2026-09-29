#include "local_ip.hpp"

#include <WinSock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

namespace local_ip
{
	std::optional<local_ip_rules::address> default_route_address()
	{
		// A public address, only for the route lookup: 8.8.8.8, in network order.
		SOCKADDR_INET destination{};
		destination.Ipv4.sin_family = AF_INET;
		destination.Ipv4.sin_addr.s_addr = htonl(0x08080808);

		MIB_IPFORWARD_ROW2 route{};
		SOCKADDR_INET source{};
		if (GetBestRoute2(nullptr, 0, nullptr, &destination, 0, &route, &source) != NO_ERROR || source.si_family != AF_INET)
		{
			return std::nullopt;
		}
		return source.Ipv4.sin_addr.s_addr;
	}

	std::vector<std::string> own_host_names()
	{
		std::vector<std::string> names;
		char name[256]{};
		if (gethostname(name, sizeof(name)) == 0 && name[0])
		{
			names.emplace_back(name);
		}
		for (const auto format : {ComputerNameDnsHostname, ComputerNameDnsFullyQualified})
		{
			char buffer[256]{};
			DWORD size = sizeof(buffer);
			if (GetComputerNameExA(format, buffer, &size) && buffer[0])
			{
				names.emplace_back(buffer);
			}
		}
		return names;
	}
}
