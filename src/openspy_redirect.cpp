#include "openspy_redirect.hpp"
#include "iat_hook.hpp"
#include "local_ip.hpp"
#include "log.hpp"

#include <WinSock2.h>

#include <string>
#include <vector>

namespace openspy_redirect
{
	namespace
	{
		constexpr WORD gethostbyname_ordinal = 52;

		using gethostbyname_t = hostent*(WSAAPI*)(const char*);
		gethostbyname_t real_gethostbyname = nullptr;
		online_rules::plan active;
		local_ip_rules::choice local;

		// The answer for this PC's own name, in place: its address list reordered as [Online] LocalIP
		// says (the pointers in Winsock's per-thread hostent move; the addresses stay where they are).
		// Each list is logged once, before and after.
		void arrange(const std::string& name, hostent* found)
		{
			if (!found || found->h_addrtype != AF_INET || found->h_length != 4 || !found->h_addr_list)
			{
				return;
			}
			std::vector<local_ip_rules::address> addresses;
			for (auto** entry = found->h_addr_list; *entry; ++entry)
			{
				addresses.push_back(reinterpret_cast<const in_addr*>(*entry)->s_addr);
			}

			const auto default_route = local.how == local_ip_rules::choice::mode::first ? std::nullopt : local_ip::default_route_address();
			const auto decision = local_ip_rules::decide(local, addresses, default_route);
			const auto before = local_ip_rules::list_text(addresses);
			if (decision.front && *decision.front > 0 && *decision.front < addresses.size())
			{
				local_ip_rules::move_to_front(found->h_addr_list, *decision.front);
				local_ip_rules::move_to_front(addresses.data(), *decision.front);
			}
			const auto key = "localip:" + before + "|" + decision.why;
			logger::write_once(key, "online: this PC's addresses (%s): %s", name.c_str(), before.c_str());
			logger::write_once(key + "|after", "online: LocalIP: %s: %s", decision.why.c_str(), local_ip_rules::list_text(addresses).c_str());
		}

		hostent* WSAAPI redirected_gethostbyname(const char* name)
		{
			const auto target = name ? online_rules::redirect(name, active) : std::nullopt;
			if (!target)
			{
				if (!name || !local_ip_rules::is_own_host(name, local_ip::own_host_names()))
				{
					return real_gethostbyname(name);
				}
				// The game passes the "localhost" answer's h_name, which lives in the hostent this lookup
				// reuses: keep a copy for the log.
				const std::string own(name);
				auto* found = real_gethostbyname(name);
				arrange(own, found);
				return found;
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

	void install(const HMODULE module, const online_rules::plan& chosen, const local_ip_rules::choice& local_choice)
	{
		if (!chosen.problem.empty())
		{
			logger::write("online: %s - ignored", chosen.problem.c_str());
		}
		if (!local_choice.problem.empty())
		{
			logger::write("online: %s - ignored, auto", local_choice.problem.c_str());
		}
		const bool redirecting = chosen.how != online_rules::plan::mode::off;
		if (!redirecting)
		{
			logger::write("online: redirect turned off in xml2-fix.ini");
		}

		if (!redirecting && local_choice.how == local_ip_rules::choice::mode::first)
		{
			logger::write("online: LocalIP=first - this PC's addresses in Windows' order, as the game has them");
			return;
		}

		active = chosen;
		local = local_choice;
		// Runs while the game is still loading, before any lookups. With LocalIP=first the hook only
		// logs the game's own address list.
		real_gethostbyname = reinterpret_cast<gethostbyname_t>(
			iat_hook::hook(module, "WS2_32.dll", "gethostbyname", gethostbyname_ordinal, reinterpret_cast<void*>(&redirected_gethostbyname)));
		if (!real_gethostbyname)
		{
			logger::write("online: the game doesn't look up hosts through gethostbyname - no redirect, LocalIP as Windows lists it");
			return;
		}
		if (chosen.how == online_rules::plan::mode::server)
		{
			logger::write("online: every GameSpy and OpenSpy host resolves to %s ([Online] Server)", chosen.target.c_str());
		}
		else if (redirecting)
		{
			logger::write("online: GameSpy servers redirected to %s", chosen.target.c_str());
		}

		switch (local.how)
		{
		case local_ip_rules::choice::mode::automatic:
			logger::write("online: LocalIP=auto - the address Windows reaches the internet from goes first when the game looks up this PC's name");
			break;
		case local_ip_rules::choice::mode::first:
			logger::write("online: LocalIP=first - this PC's addresses in Windows' order, as the game has them");
			break;
		case local_ip_rules::choice::mode::address:
			logger::write("online: LocalIP=%s - that address first when the game looks up this PC's name", local_ip_rules::text(local.wanted).c_str());
			break;
		}
	}
}
