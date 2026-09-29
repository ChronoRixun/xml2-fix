#pragma once

// [Online]: where the game's GameSpy lookups go, kept apart from the gethostbyname hook
// (openspy_redirect.cpp) so xml2_test can check it without the game.
//
// What XMen2.exe looks up (read from the retail exe, image base 0x400000). It has one resolver,
// WS2_32 gethostbyname, tried after inet_addr fails on the name, and five GameSpy names:
//   xmenlegpc.available.gamespy.com  UDP 27900  availability check    (push 0x6a6474 at 0x641b9d)
//   xmenlegpc.master.gamespy.com     UDP 27900  a hosted game's QR2    (push 0x6a6288 at 0x63d3ab)
//   xmenlegpc.ms<N>.gamespy.com      TCP 28910  server + group lists   (push 0x6a6458 at 0x63f665)
//   natneg1 / natneg2.gamespy.com    UDP 27901  NAT negotiation        (pointers at 0x6efec8, 0x6efecc)
// The server list's socket (connect at 0x63f714) is the only TCP connection the exe makes, and it
// has no chat server name: its Peer SDK code never reaches peerchat.gamespy.com.
//
//   Domain=openspy.net   the default: *.gamespy.com -> the same host under that domain, the
//                        OpenSpy way (xmenlegpc.master.gamespy.com -> xmenlegpc.master.openspy.net).
//                        "off": no redirect. Not set (or empty): openspy.net.
//   Server=127.0.0.1     every GameSpy or OpenSpy name (*.gamespy.com, *.openspy.net) resolves to
//                        that IPv4 address: a server of your own that has no DNS names, such as a
//                        private OpenSpy stack on this PC. Wins over Domain. A value that isn't an
//                        address is ignored (and logged), leaving Domain in charge.
// Both values are read by the fix's one ini rule (ini_rules.hpp): a ';' starts a comment, and an
// empty value is the same as none.

#include "ini_rules.hpp" // value_text

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace online_rules
{
	constexpr std::string_view default_domain = "openspy.net";
	constexpr std::string_view gamespy_domain = "gamespy.com";

	inline bool same_text(const std::string_view a, const std::string_view b)
	{
		return a.size() == b.size() &&
		       std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
	}

	// `host` is a name under `domain` ("<something>.domain", any case), not the bare domain.
	inline bool under(const std::string_view host, const std::string_view domain)
	{
		return host.size() > domain.size() + 1 && host[host.size() - domain.size() - 1] == '.' && same_text(host.substr(host.size() - domain.size()), domain);
	}

	// What Domain renames: GameSpy's names.
	inline bool is_gamespy_host(const std::string_view host) { return under(host, gamespy_domain); }

	// What Server catches: GameSpy's names and OpenSpy's (so an already renamed name lands there too).
	inline bool is_server_host(const std::string_view host) { return is_gamespy_host(host) || under(host, default_domain); }

	// A dotted-quad IPv4 address: four decimal numbers 0-255 of one to three digits, nothing else.
	// Returns it without leading zeros ("127.000.0.1" -> "127.0.0.1").
	inline std::optional<std::string> parse_ipv4(const std::string_view text)
	{
		std::string address;
		std::size_t position = 0;
		for (int part = 0; part < 4; ++part)
		{
			if (part > 0)
			{
				if (position >= text.size() || text[position] != '.') return std::nullopt;
				++position;
				address += '.';
			}
			const auto start = position;
			int value = 0;
			while (position < text.size() && position - start < 3 && text[position] >= '0' && text[position] <= '9')
			{
				value = value * 10 + (text[position] - '0');
				++position;
			}
			if (position == start || value > 255) return std::nullopt;
			address += std::to_string(value);
		}
		if (position != text.size()) return std::nullopt;
		return address;
	}

	struct plan
	{
		enum class mode
		{
			off,    // lookups left alone
			domain, // *.gamespy.com -> *.<target>
			server, // GameSpy and OpenSpy names -> the address in target
		};
		mode how = mode::off;
		std::string target;  // the domain, or the server's address
		std::string problem; // why a Server value was ignored, when it was
	};

	// `domain` and `server` as the ini has them, "" when a key isn't set: no Domain is openspy.net.
	inline plan choose(const std::string_view domain, const std::string_view server)
	{
		plan chosen;
		if (const auto server_text = ini_rules::value_text(server); !server_text.empty())
		{
			if (const auto address = parse_ipv4(server_text))
			{
				chosen.how = plan::mode::server;
				chosen.target = *address;
				return chosen;
			}
			chosen.problem = "[Online] Server=" + std::string(server_text) + " isn't an IPv4 address (a.b.c.d)";
		}
		const auto domain_text = ini_rules::value_text(domain);
		if (!same_text(domain_text, "off"))
		{
			chosen.how = plan::mode::domain;
			chosen.target = std::string(domain_text.empty() ? default_domain : domain_text);
		}
		return chosen;
	}

	// The name to resolve in place of `host` under `chosen`, or nothing when the lookup stays as it is.
	inline std::optional<std::string> redirect(const std::string_view host, const plan& chosen)
	{
		switch (chosen.how)
		{
		case plan::mode::server:
			if (is_server_host(host)) return chosen.target;
			break;
		case plan::mode::domain:
			if (is_gamespy_host(host)) return std::string(host.substr(0, host.size() - gamespy_domain.size())) + chosen.target;
			break;
		case plan::mode::off:
			break;
		}
		return std::nullopt;
	}
}
