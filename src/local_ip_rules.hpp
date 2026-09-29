#pragma once

// [Online] LocalIP: which of this PC's addresses the game takes for its own, kept apart from the
// gethostbyname hook (openspy_redirect.cpp) so xml2_test can check it without the game.
//
// What XMen2.exe does (read from the retail exe, image base 0x400000). It never asks Windows for
// its adapters; every local address comes from resolving the PC's own host name:
//   - The game's own address, the one the Play Online screen shows as LocalIP and the one its game
//     socket ("SVS Multiplay", UDP 5165) binds to: 0x615d30 calls gethostbyname("localhost")
//     (0x615d48), then gethostbyname on that answer's h_name, the PC's host name (0x615d62), and
//     keeps h_addr_list[0] (0x615d6b) in the network manager (0x60b210, +0x1a4). The Play Online
//     screen resolves it when it opens (0x5cae52 -> 0x615dc0), a session again when it starts
//     (0x613bc7). The screen only shows it (0x5caff0: inet_ntoa at 0x615db0 into data_networkfile,
//     under the "LocalIP" label); nothing in the exe picks another entry of the list.
//   - Its GameSpy code (getlocalhost, 0x641fe0: gethostname, then gethostbyname): QR2 keeps the
//     first five addresses for the heartbeat's localip0..4 (0x63d358), Peer and NAT negotiation take
//     the first private one (0x62e800, 0x63b620).
// Windows lists the addresses in adapter order, not by route, so on a PC with WSL, Hyper-V, Docker
// or VPN adapters the first one is often a virtual adapter's that can't reach the internet.
//
//   LocalIP=auto     the default: the address Windows sends from to reach the internet (its best
//                    route's to a public address) goes first; the others keep their order. Without
//                    such a route, or with its address not in the list, Windows' order stays.
//   LocalIP=first    Windows' order, as the game has it without the fix.
//   LocalIP=a.b.c.d  that address first, when it is one of this PC's; otherwise auto (logged).
// A ';' starts a comment (the profile API keeps those).

#include "limits_rules.hpp" // value_text
#include "online_rules.hpp" // parse_ipv4, same_text

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace local_ip_rules
{
	// An IPv4 address as in_addr holds it (network order: the first number in the lowest byte here).
	using address = std::uint32_t;

	inline std::optional<address> parse_address(const std::string_view text)
	{
		const auto dotted = online_rules::parse_ipv4(text);
		if (!dotted) return std::nullopt;
		address result = 0;
		std::size_t position = 0;
		for (int part = 0; part < 4; ++part)
		{
			std::uint32_t value = 0;
			while (position < dotted->size() && (*dotted)[position] != '.')
			{
				value = value * 10 + static_cast<std::uint32_t>((*dotted)[position] - '0');
				++position;
			}
			++position; // the dot
			result |= value << (8 * part);
		}
		return result;
	}

	inline std::string text(const address value)
	{
		return std::to_string(value & 0xff) + "." + std::to_string((value >> 8) & 0xff) + "." + std::to_string((value >> 16) & 0xff) + "." +
		       std::to_string(value >> 24);
	}

	inline std::string list_text(const std::span<const address> addresses)
	{
		std::string result;
		for (const auto value : addresses)
		{
			if (!result.empty()) result += ", ";
			result += text(value);
		}
		return result.empty() ? "(none)" : result;
	}

	struct choice
	{
		enum class mode
		{
			automatic, // the default route's address first
			first,     // Windows' order
			address,   // `wanted` first
		};
		mode how = mode::automatic;
		address wanted = 0;
		std::string problem; // why a value was ignored, when it was
	};

	// `value` as the ini has it: "auto" when the key is absent (the profile API's default).
	inline choice choose(const std::string_view value)
	{
		choice chosen;
		const auto setting = limits_rules::value_text(value);
		if (setting.empty() || online_rules::same_text(setting, "auto"))
		{
			return chosen;
		}
		if (online_rules::same_text(setting, "first"))
		{
			chosen.how = choice::mode::first;
			return chosen;
		}
		if (const auto wanted = parse_address(setting))
		{
			chosen.how = choice::mode::address;
			chosen.wanted = *wanted;
			return chosen;
		}
		chosen.problem = "[Online] LocalIP=" + std::string(setting) + " isn't auto, first or an IPv4 address (a.b.c.d)";
		return chosen;
	}

	inline std::string setting_text(const choice& chosen)
	{
		switch (chosen.how)
		{
		case choice::mode::first:
			return "first";
		case choice::mode::address:
			return text(chosen.wanted);
		case choice::mode::automatic:
			break;
		}
		return "auto";
	}

	// `name` spells this PC's name: one of `own_names` (gethostname's, the DNS host name, the fully
	// qualified one), any case, a trailing root dot allowed.
	inline bool is_own_host(std::string_view name, const std::span<const std::string> own_names)
	{
		if (!name.empty() && name.back() == '.') name.remove_suffix(1);
		return !name.empty() && std::ranges::any_of(own_names, [&](const std::string& own) { return !own.empty() && online_rules::same_text(name, own); });
	}

	struct decision
	{
		std::optional<std::size_t> front; // the entry to move to the front; nothing = the list stays
		std::string why;                  // for the log
	};

	// Which of `addresses` (the answer for this PC's name, in Windows' order) goes first under `chosen`,
	// with `default_route` the address Windows sends from to reach the internet (nothing without a route).
	inline decision decide(const choice& chosen, const std::span<const address> addresses, const std::optional<address> default_route)
	{
		const auto index_of = [&](const address value) -> std::optional<std::size_t>
		{
			const auto found = std::ranges::find(addresses, value);
			if (found == addresses.end()) return std::nullopt;
			return static_cast<std::size_t>(found - addresses.begin());
		};

		std::string prefix;
		switch (chosen.how)
		{
		case choice::mode::first:
			return {std::nullopt, "Windows' order ([Online] LocalIP=first)"};
		case choice::mode::address:
			if (const auto index = index_of(chosen.wanted))
			{
				return {index, text(chosen.wanted) + " first ([Online] LocalIP=" + text(chosen.wanted) + ")"};
			}
			prefix = "[Online] LocalIP=" + text(chosen.wanted) + " isn't one of this PC's addresses - auto: ";
			break;
		case choice::mode::automatic:
			break;
		}

		if (!default_route)
		{
			return {std::nullopt, prefix + "no route to the internet - Windows' order kept"};
		}
		if (const auto index = index_of(*default_route))
		{
			return {index, prefix + text(*default_route) + " first (the address Windows reaches the internet from" + (prefix.empty() ? ", [Online] LocalIP=auto)" : ")")};
		}
		return {std::nullopt, prefix + "the internet route's address " + text(*default_route) + " isn't in the list - Windows' order kept"};
	}

	// Moves list[index] to the front, the entries before it one place down: the rest keep their order.
	template <typename T>
	void move_to_front(T* list, const std::size_t index)
	{
		std::rotate(list, list + index, list + index + 1);
	}
}
