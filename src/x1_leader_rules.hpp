#pragma once

// [Game] X1Leader: X-Men Legends 1's stats `leader` bit (issue #15), for the port's builds (the
// builder ships Data\x1leader.txt with the stats entries XML1 marked leader="true"). Kept apart
// from the hooks so xml2_test can check it without the game: the ini flag's meaning, the sidecar's
// format, the decision, every retail byte the change relies on and the code it writes.
//
// Every XMen2.exe address is the retail build's (image base 0x400000, no relocations).
//
// What XML1 did (default.xbe). The stats parser (0xb03f5) keeps `leader` as CStats+0x3a1 bit 3.
// Two code sites test it, both live in retail; the other two spawn-time call sites reach empty
// stubs and did nothing even on the Xbox:
//  - the pain gate (FUN_00044410, actor vt+0xa8): a hero (team 0x1b) hit by a projectile
//    (attack kind 5) whose owner carries the bit does not play the flinch reaction;
//  - the death-drop roller (FUN_00070fb0, party-manager vt+0x1b0): a flagged corpse always rolls
//    item drops (others roll a 15-65 % chance) and, from the second drop on, forces the named
//    HEALTH_ITEM / ENERGY_ITEM / XTREME_PIP slots.
//
// What XMen2.exe does. The same two functions survive as ports, keyed to actor+0x3db bit 3 (the
// engine's x3 spawn-variant flag, also its x3 kill-XP multiplier) because its stats parser has no
// leader field: the pain gate FUN_00435850 tests [attacker+0x3db]&8 at 0x43597b before clearing
// the flinch flag (ctx+0xac bit 3), and the drop roller FUN_0046e490 tests [corpse+0x3db]&8 at
// 0x46e4ed. The roller is XML2's own version of the rule, not a copy of the first game's: the flag
// counts only for a drop kind > 0, skips the probability roll (table 0x686dec), passes rarity
// 1-3 instead of 6, draws a 2-5 drop count on one of its two count paths, from the second drop forces
// XTREME_PIP (0x6d839c) with chance 0.33 when the manager's vt+0x19c(-1) exceeds vt+0x194(), else
// a normal pick; HEALTH_ITEM / ENERGY_ITEM are no longer forced. This fix restores that engine
// rule for the first game's leaders; it does not re-create the Xbox item mix. Neither flag is
// settable per character in data (the spawn `leader` attribute is a different, danger-room
// mechanic; the x3 variant would also triple kill XP), so this fix extends the two tests: an actor
// whose stats name is in the sidecar counts as a leader, exactly like a flagged one. Flagged
// actors keep the native behaviour untouched (decide() tests the bit first).
//
// The two sites run the same seven bytes - test byte ptr [reg+0x3db], 8 - and their branch is
// kept: the patch replaces only those seven bytes with a jmp to a bridge that reproduces the
// test (flag bit, then the sidecar lookup) and jumps back to the branch with ZF set as the test
// would have left it. A jmp, not a call: the bridge leaves through a jmp, so a call's return
// address would stay on the game's stack (the function's own epilogue would then return into the
// patched site and run its tail twice).

#include "limits_rules.hpp" // guard, matches, image_base

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace x1_leader_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;
	using address = std::uint32_t;

	// ---- The retail sites (both are `test byte ptr [reg+0x3db], 8`) ------------------------------

	constexpr address pain_site_va = 0x43597b;   // FUN_00435850; edi = the attack's owner
	constexpr address pain_resume_va = 0x435982; // its jne (suppress at 0x4359a9) is kept
	constexpr address drop_site_va = 0x46e4ed;   // FUN_0046e490; esi = the corpse
	constexpr address drop_resume_va = 0x46e4f4; // its je (skip at 0x46e503) is kept

	inline constexpr std::array<guard, 2> guards{{
	    {pain_site_va, "f687db03000008", "the pain gate's leader-flag test (FUN_00435850, 0x43597b)"},
	    {drop_site_va, "f686db03000008", "the drop roller's leader-flag test (FUN_0046e490, 0x46e4ed)"},
	}};

	// ---- The actor fields the decision reads ------------------------------------------------------

	constexpr std::uint32_t actor_flags = 0x3db; // bit 3 (8): the native x3-variant / leader flag
	constexpr std::uint32_t actor_stats = 0x35c; // -> CStats
	constexpr std::uint32_t stats_name = 0x150;  // the stats entry's name (FUN_0041c760's wolverine check)
	constexpr std::uint8_t native_leader_bit = 8;
	constexpr std::size_t name_field = 0x20;     // CStats+0x150 holds the name in 0x20 bytes, NUL included

	template <typename T>
	inline T at(const void* base, const address offset)
	{
		T value{};
		std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof value);
		return value;
	}

	// ASCII-only lowercasing (the names are C-identifiers): no locale/ctype dependency, safe on the
	// game's own thread from inside the bridge.
	inline char ascii_lower(const char c)
	{
		return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
	}

	// ---- The sidecar: Data\x1leader.txt (xml1build.characters derives it from leader="true") ------

	// One stats name per line; '#' and ';' start comments; blank lines are ignored; matching is
	// ASCII case-insensitive (the builder writes the names as they appear in the source stats).
	class leader_names
	{
	public:
		void add(std::string name)
		{
			for (auto& c : name)
			{
				c = ascii_lower(c);
			}
			names_.push_back(std::move(name));
		}
		// No allocation (the bridges call this on the game's thread for every projectile hit on a hero and
		// every death): the name is read up to the stats entry's 0x20-byte field, lowered into a local.
		bool contains(const char* name) const
		{
			if (!name)
			{
				return false;
			}
			char lower[name_field];
			std::size_t n = 0;
			for (; n < name_field && name[n] != 0; ++n)
			{
				lower[n] = ascii_lower(name[n]);
			}
			if (n == name_field)
			{
				return false; // no terminator inside the field: not a stats name
			}
			const std::string_view key(lower, n);
			const auto it = std::lower_bound(names_.begin(), names_.end(), key,
			                                 [](const std::string& a, const std::string_view b) { return std::string_view(a) < b; });
			return it != names_.end() && std::string_view(*it) == key;
		}
		std::size_t size() const { return names_.size(); }
		void finish()
		{
			std::sort(names_.begin(), names_.end());
			names_.erase(std::unique(names_.begin(), names_.end()), names_.end());
		}
		static leader_names parse(const std::string_view text)
		{
			leader_names out;
			std::size_t at = 0;
			while (at <= text.size())
			{
				const auto end = text.find('\n', at);
				auto row = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
				if (!row.empty() && row.back() == '\r')
				{
					row.remove_suffix(1);
				}
				while (!row.empty() && (row.front() == ' ' || row.front() == '\t'))
				{
					row.remove_prefix(1);
				}
				while (!row.empty() && (row.back() == ' ' || row.back() == '\t'))
				{
					row.remove_suffix(1);
				}
				if (!row.empty() && row.front() != '#' && row.front() != ';')
				{
					out.add(std::string(row));
				}
				if (end == std::string_view::npos)
				{
					break;
				}
				at = end + 1;
			}
			out.finish();
			return out;
		}

	private:
		std::vector<std::string> names_;
	};

	// ---- The decision (runs on the game's thread; the bridge saves every register) -----------------

	inline const char* actor_name(const void* actor)
	{
		if (!actor)
		{
			return nullptr;
		}
		const auto* stats = at<const std::uint8_t*>(actor, actor_stats);
		if (!stats)
		{
			return nullptr;
		}
		return reinterpret_cast<const char*>(stats + stats_name);
	}

	// The patched test: the native flag exactly as the game tested it, then the sidecar lookup.
	inline bool decide(const void* actor, const leader_names& names)
	{
		if (actor && (at<std::uint8_t>(actor, actor_flags) & native_leader_bit) != 0)
		{
			return true; // XML2's x3 variants behave natively
		}
		return names.contains(actor_name(actor));
	}

	// ---- The bridge each site is patched with ------------------------------------------------------
	//
	// Entered by the site's jmp with the game's stack untouched: save; push the actor; call the
	// fix's decide; test eax,eax; restore; jmp resume. popad leaves the flags from test eax,eax
	// standing, which is what the kept branch reads (ZF clear = a leader, as the native test).
	namespace detail
	{
		inline void word(std::vector<std::uint8_t>& b, const std::uint32_t v)
		{
			for (int i = 0; i < 4; ++i)
			{
				b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
			}
		}
	}

	// actor_reg: 0x57 = push edi (pain site), 0x56 = push esi (drop site). The final jmp's rel32 is
	// left zero; the caller fills it once the block's load address is known (placement-relative).
	inline std::vector<std::uint8_t> bridge(const std::uint8_t actor_reg, const address callback)
	{
		std::vector<std::uint8_t> b{0x60, actor_reg, 0xb8}; // pushad; push actor; mov eax, callback
		detail::word(b, callback);
		b.insert(b.end(), {0xff, 0xd0,       // call eax
		                   0x83, 0xc4, 0x04, // add esp, 4
		                   0x85, 0xc0,       // test eax, eax
		                   0x61});           // popad (flags untouched)
		b.push_back(0xe9);                   // jmp resume (rel32 filled by the caller)
		detail::word(b, 0);
		return b;
	}

	// The seven bytes written over a site: jmp rel32 into the bridge + two nops (never executed: the
	// bridge resumes at the kept branch, site + 7). rel32 is from the end of the jmp to the bridge.
	inline std::vector<std::uint8_t> jmp_bridge(const address site, const address bridge_va)
	{
		std::vector<std::uint8_t> b{0xe9};
		detail::word(b, bridge_va - (site + 5));
		b.insert(b.end(), {0x90, 0x90});
		return b;
	}
}
