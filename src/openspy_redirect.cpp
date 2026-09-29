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

		using gethostbyname_t = hostent*(WSAAPI*)(const char*);
		gethostbyname_t real_gethostbyname = nullptr;
		online_rules::plan active;

		hostent* WSAAPI redirected_gethostbyname(const char* name)
		{
			const auto target = name ? online_rules::redirect(name, active) : std::nullopt;
			if (!target)
			{
				return real_gethostbyname(name);
			}

			// Server mode hands Winsock the dotted address, which it answers without a lookup, in its
			// own per-thread hostent - the same memory a real answer lives in.
			auto* result = real_gethostbyname(target->c_str());
			if (active.how == online_rules::plan::mode::server)
			{
				logger::write_once(std::string("host:") + name, "online: %s -> %s ([Online] Server)", name, target->c_str());
			}
			else
			{
				logger::write_once("host:" + *target, "online: %s -> %s (%s)", name, target->c_str(), result ? "resolved" : "lookup failed");
			}
			return result;
		}
	}

	void install(const HMODULE module, const online_rules::plan& chosen)
	{
		if (!chosen.problem.empty())
		{
			logger::write("online: %s - ignored", chosen.problem.c_str());
		}
		if (chosen.how == online_rules::plan::mode::off)
		{
			logger::write("online: redirect turned off in xml2-fix.ini");
			return;
		}

		active = chosen;
		// Runs while the game is still loading, before any lookups.
		real_gethostbyname = reinterpret_cast<gethostbyname_t>(
			iat_hook::hook(module, "WS2_32.dll", "gethostbyname", gethostbyname_ordinal, reinterpret_cast<void*>(&redirected_gethostbyname)));
		if (!real_gethostbyname)
		{
			logger::write("online: the game doesn't look up hosts through gethostbyname - no redirect");
		}
		else if (chosen.how == online_rules::plan::mode::server)
		{
			logger::write("online: every GameSpy and OpenSpy host resolves to %s ([Online] Server)", chosen.target.c_str());
		}
		else
		{
			logger::write("online: GameSpy servers redirected to %s", chosen.target.c_str());
		}
	}
}
