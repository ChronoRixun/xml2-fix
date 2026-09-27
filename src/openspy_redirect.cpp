#include "openspy_redirect.hpp"
#include "iat_hook.hpp"
#include "log.hpp"

#include <WinSock2.h>

#include <string>

namespace openspy_redirect
{
	namespace
	{
		constexpr WORD gethostbyname_ordinal = 52;
		constexpr std::string_view gamespy_suffix = ".gamespy.com";

		using gethostbyname_t = hostent*(WSAAPI*)(const char*);
		gethostbyname_t real_gethostbyname = nullptr;
		std::string target_domain;

		bool ends_with_gamespy(const std::string& name)
		{
			return name.size() > gamespy_suffix.size() &&
			       _stricmp(name.c_str() + name.size() - gamespy_suffix.size(), gamespy_suffix.data()) == 0;
		}

		hostent* WSAAPI redirected_gethostbyname(const char* name)
		{
			if (!name)
			{
				return real_gethostbyname(name);
			}

			std::string host(name);
			if (!ends_with_gamespy(host))
			{
				return real_gethostbyname(name);
			}

			host.replace(host.size() - gamespy_suffix.size() + 1, std::string::npos, target_domain);
			auto* result = real_gethostbyname(host.c_str());
			logger::write_once("host:" + host, "online: %s -> %s (%s)", name, host.c_str(), result ? "resolved" : "lookup failed");
			return result;
		}
	}

	void install(const HMODULE module, const char* domain)
	{
		target_domain = domain;
		// Runs while the game is still loading, before any lookups.
		real_gethostbyname = reinterpret_cast<gethostbyname_t>(
			iat_hook::hook(module, "WS2_32.dll", "gethostbyname", gethostbyname_ordinal, reinterpret_cast<void*>(&redirected_gethostbyname)));
		if (real_gethostbyname)
		{
			logger::write("online: GameSpy servers redirected to %s", domain);
		}
		else
		{
			logger::write("online: the game doesn't look up hosts through gethostbyname - no redirect");
		}
	}
}
