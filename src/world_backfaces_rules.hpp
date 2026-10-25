#pragma once

// [Game] WorldBackfaces=xml1: an actor's movement sweep meets only the level's front faces, as in X-Men Legends 1
// (gameplay rule 30, docs/world-backfaces.md). Kept apart from the patch so the tests can check every retail byte
// it relies on.
//
// The movement sweep. An actor's movement update (0x432130) moves it by tracing its collision box with
// actor_movement_trace (0x42b6c0; default.xbe 0x3c720, the same code) -> 0x4ab7f0 (XBE 0xa2b90) -> the common trace
// helper 0x4a9650 (XBE 0xa0af0), which fills a pooled collider (vtable 0x68d6a4; XBE 0x3d29ec) and runs its sweep
// (collider vt+0x78 = 0x4aa250; XBE 0xa1860) through the world collision object (vt+0x10 = 0x44d360; XBE 0x588d0).
// That one call first tests the level's own entity (the zone's collision hull) and then every candidate entity; the
// hull goes to the hull narrow phase (0x56bb00; XBE 0x12f4a0) and its BVH leaf testers (0x5fd1e0, 0x5fe8f0, 0x5ffb50,
// 0x5ffdf0; XBE 0x194cf0, 0x1963e0, 0x197640, 0x1978e0).
//
// The face filter. Every leaf tester takes two flags in its context (+0x34 front, +0x35 back) and, per triangle,
// d = the triangle's normal dot the query direction (0x5fd88c..0x5fd8df, 0x5fec54..; XBE 0x19539c.., 0x196744..,
// byte-for-byte the same tests): back clear rejects d > 0, front clear rejects d <= 0. So (front 1, back 0) is a
// one-sided level - a wall seen from behind is not there - and (1, 1) is two-sided.
//
// What X-Men Legends 1 does. Its hull narrow phase always runs (1, 0): the context constructor 0x12f310 sets +0x34 = 1
// and +0x35 = 0, and 0x12f4a0 stores 0 at +0x35 again (0x12f50d). Its trace helper 0xa0af0 has no flag call at all,
// and its collider (vtable 0x3d29ec, 0x84 bytes of slots) has no flag setters.
//
// What XMen2.exe does. The collider grew two flags (+0x30 front, +0x31 back; reset to 1 / 0 at 0x4a9fe2, setters
// vt+0x84 = 0x61fde0 and vt+0x88 = 0x4aa320), carried as two more arguments to the world call (0x4aa2a2..0x4aa2a9)
// and on to the hull narrow phase (0x56bb58 / 0x56bb6a -> its context +0x34 / +0x35). The entity pass of 0x44d360
// forces (1, 0) again, so props and other entities keep the original one-sided rule; only the level's hull takes the
// collider's flags. And the trace helper 0x4a9650 calls vt+0x88(1) before every sweep (0x4a96c0 push 1, 0x4a96dc
// call): every trace through it meets the level's back faces too - 0x4ab7f0 (the actor movement trace 0x42b6c0 and
// the entity trace at 0x471cca) and 0x4ab890 (five more entity traces: 0x471cf1, 0x473bd9, 0x473d15, 0x48bdf4,
// 0x48beaa). Their twins in default.xbe (0xa2b90, 0xa2c30) go through 0xa0af0, which never asks.
//
// Why it matters. XML1's levels are authored for one-sided collision; the X-Men Legends 1 port loads them unchanged
// (haarp2_1.igb is byte-identical). Wherever an actor's 32 x 32 x 72 box (stats "size", default 0x4bce80 / XBE
// 0xb1742) straddles or approaches a wall from its back, the port stops or deflects it where the original walks
// through. HAARP's 520 staging pocket in haarp2_1 is boxed in by such walls (hull triangles of the y = 78, x = 318,
// y = 90 (x 356..360) and x = 360 planes face into the pocket): the officer's box meets the back of the y = 90 strip
// at (340.0, 91.3), where the port's officer is deflected (339.3, 91.6) and, below 60 fps, wedged at its free end
// (339.58, 91.87; three zero-displacement updates -> the stuck abort), while the original's officer walks straight
// through it ((340, 91) -> (342, 94) -> (345, 97)) and first slides on the front-facing wall at x ~ 365.9
// (its centre at x 349.1, the line both engines share).
//
// WorldBackfaces=xml1 makes the helper pass 0 instead of 1 in the first game's levels: every movement sweep of the
// level's hull runs (1, 0), the original's rule. Nothing else changes: traces that do not use this helper never set
// the flag, entities were already (1, 0), and the collider's reset is untouched.
//
// Revision 2 (owner decision 2026-10-25): only XML1 levels. The mod lists its first-game zones ([WorldBackfaces]
// ZoneCount + Z1..Zn, zone paths as the zone manager names them; the list is required); every other zone (the X-Men Legends II maps a port
// install still carries: tutorial, act1-5, briefings, Danger Room, versus) keeps XMen2.exe's two-sided sweep. The
// helper's `push 1; fstp [esp+0x10]` (0x4a96c0, 6 bytes) becomes a jump to a bridge that does the store first and
// then pushes the current zone's flag; the entity-capacity owner of the helper asks the same function
// (world_backfaces_state::current). The current zone is the zone manager's (static 0x72a578, vtable 0x68878c) path
// at +0x1e0 (0x40 bytes, its vt+0x5c getter 0x483f30), named when its load is asked for: every peer of a lockstep
// session loads the same zones, so every peer picks the same flag. Without a list nothing is patched (revision 1,
// the unscoped private candidate, applied it to every zone).

#include "limits_rules.hpp" // guard, matches, image_base
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace world_backfaces_rules
{
	using limits_rules::guard;
	using address = std::uint32_t;

	// 2: scoped to the listed zones (1.8.0-beta.7); 1 was the unscoped first candidate (070a, private), refused.
	constexpr std::uint16_t agreement_revision = 2;

	// The argument of the helper's collider vt+0x88 (back faces) call: 1 = XMen2.exe, 0 = X-Men Legends 1.
	constexpr address back_flag_operand = 0x4a96c1;
	constexpr std::uint8_t retail_back_flag = 0x01;
	constexpr std::uint8_t xml1_back_flag = 0x00;
	// push 1 (6a 01); fstp dword [esp+0x10] (d9 5c 24 10) -> jmp bridge; nop. The bridge returns to 0x4a96c6.
	constexpr address flag_site = 0x4a96c0, flag_resume = 0x4a96c6;
	inline constexpr std::array<std::uint8_t, 6> flag_site_retail{0x6a, 0x01, 0xd9, 0x5c, 0x24, 0x10};
	// The zone manager (discord_rules.hpp / forced_teams_rules.hpp read the same fields).
	constexpr address zones_static = 0x72a578, zones_vtable = 0x68878c;
	constexpr std::size_t zone_name_at = 0x1e0, zone_name_size = 0x40;

	inline constexpr std::array<guard, 8> guards{{
		// From 0x4a9657: [Limits] EntityCapacity (installed earlier, at startup) bridges the helper's first 7 bytes to its
		// DLL owner, which passes world_backfaces_state::trace_back_faces; the operand below stays the retail helper's.
		{0x4a9657, "85c00f84b40000008d4c2414e868befbff84c00f84a3000000568b74242c8b0e8b01578d54241c526a01ff50308b44242085c0741b8d4c2420e83bbefbff84c0740e8b0e8b018d542420526a01ff50308b0e8b016a016a01ff50408b7c24248b442428d907d8008b0e6a01d95c2410d94704d84004d95c2414d94708d84008d95c24188b11ff92880000008b0e8b018d54241c528d5424105257ff50788b0e8b018d54240852ff50208b088b44242c5f85c989085e0f95c083c410c3",
		 "the trace helper after its entry: exclusions, group filter, end point, back-face flag (0x4a96c0 push 1, 0x4a96dc vt+0x88), sweep vt+0x78, result vt+0x20 (0x4a9657-0x4a9712)"},
		{0x4aa320, "8a442404884131c20400", "collider vt+0x88: the back-face flag byte +0x31 (0x4aa320)"},
		{0x68d72c, "20a34a00", "the collider vtable's +0x88 slot (0x68d72c -> 0x4aa320)"},
		{0x68d71c, "50a24a00", "the collider vtable's +0x78 slot, the sweep (0x68d71c -> 0x4aa250)"},
		{0x4a9fe2, "c6453201c6453001c6453100", "the collider reset: +0x32 = 1, front +0x30 = 1, back +0x31 = 0 (0x4a9fe2)"},
		{0x5fd88c, "8a413584c0d9825c010000d84b048dba58010000dec1d903d80fdec1d95c24147515d9442414d81d30006800dfe0f6c4410f84a40000008a413484c07515d9442414d81d30006800dfe0f6c4410f8b88000000",
		 "a hull leaf tester's face filter: back clear rejects d > 0, front clear rejects d <= 0 (0x5fd88c-0x5fd8de)"},
		{0x4849ba, "b978a57200c744240800000000e824f2ffff68d0db6700e848d71e0083c4048b0c24b878a57200", "the zone manager is built at 0x72a578 and handed out (0x484990)"},
		{0x483f30, "8d81e0010000c3", "the zone manager's vt+0x5c: the current zone's path at +0x1e0 (0x483f30)"},
	}};

	// The zone list ([WorldBackfaces] ZoneCount + Z1..Zn). A zone path as the zone manager names it: lower-case
	// letters, digits, '_', '-', '.', and '/' between non-empty parts, 1..63 characters; written with '\' or upper
	// case it is normalized. `all` = no list: only the not-installed state (the rule needs a list).
	constexpr std::size_t max_zones = 512;
	// ZoneCount: decimal digits only, 1..512 (no sign, no space, no leading zero).
	inline std::optional<unsigned> parse_zone_count(const std::string_view text)
	{
		if (text.empty() || text.size() > 3 || text[0] == '0')
		{
			return std::nullopt;
		}
		unsigned n = 0;
		for (const char c : text)
		{
			if (c < '0' || c > '9')
			{
				return std::nullopt;
			}
			n = n * 10 + unsigned(c - '0');
		}
		return n >= 1 && n <= max_zones ? std::optional<unsigned>(n) : std::nullopt;
	}
	struct scope
	{
		bool all = true;
		std::vector<std::string> zones; // sorted, unique, normalized
	};
	inline std::optional<std::string> normalize_zone(const std::string_view text)
	{
		if (text.empty() || text.size() >= zone_name_size)
		{
			return std::nullopt;
		}
		std::string out;
		for (char c : text)
		{
			if (c == '\\')
			{
				c = '/';
			}
			if (c >= 'A' && c <= 'Z')
			{
				c = static_cast<char>(c - 'A' + 'a');
			}
			const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/';
			if (!ok || (c == '/' && (out.empty() || out.back() == '/')))
			{
				return std::nullopt;
			}
			out += c;
		}
		if (out.back() == '/' || out.find("..") != std::string::npos)
		{
			return std::nullopt;
		}
		return out;
	}
	// False for an empty, oversized, duplicate or malformed list.
	inline bool make_scope(std::vector<std::string> names, scope& out)
	{
		out = {};
		if (names.empty() || names.size() > max_zones)
		{
			return false;
		}
		for (auto& n : names)
		{
			const auto z = normalize_zone(n);
			if (!z)
			{
				return false;
			}
			n = *z;
		}
		std::sort(names.begin(), names.end());
		if (std::adjacent_find(names.begin(), names.end()) != names.end())
		{
			return false;
		}
		out.all = false;
		out.zones = std::move(names);
		return true;
	}
	// The zone manager's name (up to 0x40 bytes, NUL-terminated or not) in scope? An unnamed zone never is.
	inline bool in_scope(const scope& s, const char* name, const std::size_t size)
	{
		if (s.all)
		{
			return true;
		}
		std::size_t n = 0;
		while (n < size && name[n])
		{
			++n;
		}
		const auto z = normalize_zone(std::string_view(name, n));
		return z && std::binary_search(s.zones.begin(), s.zones.end(), *z);
	}
	// The flag the trace helper passes for a zone: X-Men Legends 1's 0 in scope, XMen2.exe's 1 elsewhere.
	inline std::uint8_t flag_for(const scope& s, const char* name, const std::size_t size)
	{
		return in_scope(s, name, size) ? xml1_back_flag : retail_back_flag;
	}
	// Rule 30's detail (docs/gameplay-agreement.md "Rule details"): "WBZ1", the zone count (u32 LE), then every zone
	// in sorted order as its length byte and its normalized path.
	inline std::vector<std::uint8_t> canonical_bytes(const scope& s)
	{
		std::vector<std::uint8_t> out{'W', 'B', 'Z', '1'};
		const auto n = static_cast<std::uint32_t>(s.zones.size());
		for (unsigned i = 0; i != 4; ++i)
		{
			out.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
		}
		for (const auto& z : s.zones)
		{
			out.push_back(static_cast<std::uint8_t>(z.size()));
			out.insert(out.end(), z.begin(), z.end());
		}
		return out;
	}
	inline std::array<std::uint8_t, 32> digest(const scope& s)
	{
		const auto bytes = canonical_bytes(s);
		return sha256_fips::digest(bytes.data(), bytes.size());
	}

	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - limits_rules::image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	// The leaf testers' face filter as the retail code runs it (0x5fd88c..0x5fd8df): whether a triangle with
	// d = normal . direction is tested at all, for the context's front / back flags.
	constexpr bool face_tested(const float d, const bool front, const bool back)
	{
		if (!back && d > 0.0f)
		{
			return false; // a back face (0x5fd8bd: d above zero -> skip)
		}
		if (!front && (d < 0.0f || d == 0.0f))
		{
			return false; // a front or edge-on face (0x5fd8d9; an unordered d passes, as the fnstsw test lets it)
		}
		return true;
	}

	// The settings word: absent, empty or "xml2" = the game's own (nothing patched, no agreement entry); "xml1" = the
	// first game's one-sided level; anything else is a failed request.
	enum class setting
	{
		native,
		xml1,
		invalid
	};
	inline setting parse(const std::string_view value)
	{
		if (value.empty() || value == "xml2" || value == "off")
		{
			return setting::native;
		}
		return value == "xml1" ? setting::xml1 : setting::invalid;
	}
}
