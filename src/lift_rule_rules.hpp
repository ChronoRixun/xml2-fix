#pragma once

// [Game] LiftRule=xml1: X-Men Legends 1's lift rule for objects that carry XML1's heaviness, so Might rank 3 lifts
// the first game's heaviness-4 objects again (gameplay rule 38, docs/lift-rule.md). Kept apart from the patch so
// the tests can check every rule and retail byte without the game.
//
// What X-Men Legends 1 does (default.xbe 0x38400, the only lift test): not while the object is carried (+0x2b9 bit 2);
// yes under the debug lift-anything overrides (0xbf730/0xbf750/0xbf740/0xc04d0); otherwise no when the object's
// heaviness (physent +0x2c3, 0..5) is 5 or more, else yes when the might rank (0xb1ba0) + 1 >= heaviness.
//
// What XMen2.exe does (0x427f60, the only lift test, called by the pickup handler at 0x4ec4df): the same carried test
// (+0x30d bit 2) and overrides (0x4c87a0/0x4c87c0/0x4c87b0/0x4c9c60), then no when the heaviness byte (+0x31d,
// clamped 0..3 where it is read, 0x498995) is 3 or more, else yes when might_heaviness (stats +0x10a bits 3-6,
// 0x427dc0) >= heaviness.
//
// The port's builder writes XML1 heaviness h as max(h - 1, 0), 4 and 5 both as 3, and gives might rank r the
// affecter might_heaviness r. That reproduces XML1 for heaviness 0-3 at every rank, but XML1's 4 (Might 3) and 5
// (never) share XMen2.exe's never-lifted 3. The builder now also keeps XML1's number in `xml1heaviness` for those
// two (an attribute XMen2.exe never reads). With the rule on, the fix reads it where the game reads `heaviness`
// and, for an object that carries it and whose heaviness byte is still what the builder wrote for it, decides the
// lift with XML1's rule. Revision 2 also restores thrown impact level, carried throw damage and the TK limit.
// The byte itself and the remaining physics/AI readers stay unchanged (docs/lift-rule.md lists them).

#include "limits_rules.hpp" // guard, matches, image_base

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lift_rule_rules
{
	using limits_rules::guard;
	using address = std::uint32_t;

	constexpr std::uint16_t agreement_revision = 2;
	constexpr const char* heaviness_attribute = "xml1heaviness";

	enum class setting { native, xml1, invalid };
	inline setting parse(std::string_view v)
	{
		auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
		while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
		while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
		auto is = [&](std::string_view w) {
			if (v.size() != w.size()) return false;
			for (std::size_t i = 0; i < v.size(); ++i) if (lower(v[i]) != w[i]) return false;
			return true;
		};
		if (v.empty() || is("xml2") || is("off")) return setting::native;
		if (is("xml1")) return setting::xml1;
		return setting::invalid;
	}

	constexpr int xml1_max_heaviness = 5; // XML1 never lifts 5 (0x38443 jae 5)
	// The heaviness byte the builder writes for XML1 heaviness h (x1schema.HEAVINESS_X1_TO_X2).
	constexpr std::array<int, 6> engine_of{0, 0, 1, 2, 3, 3};
	constexpr int engine_heaviness(int xml1) { return engine_of[xml1 < 0 ? 0 : xml1 > 5 ? 5 : xml1]; }
	// default.xbe 0x38443..0x3846e (after the carried test and the overrides).
	constexpr bool xml1_liftable(int might, int heaviness) { return heaviness < xml1_max_heaviness && might + 1 >= heaviness; }
	// XMen2.exe 0x427fa3..0x427fc7 (after the same tests).
	constexpr bool game_liftable(int might_heaviness, int heaviness_byte) { return heaviness_byte < 3 && might_heaviness >= heaviness_byte; }

	// Revision 2 consumers: trust the attribute only while its converted byte still agrees. Without one,
	// invert the builder's scale (0 stays 0; 1/2/3 -> 2/3/4). Legacy byte 3 cannot distinguish XML1 4/5:
	// rebuild with the companion builder for exact results. A script-mutated byte keeps native behavior.
	constexpr int consumer_heaviness(std::optional<int> xml1, int byte)
	{
		if (xml1) return *xml1 >= 0 && *xml1 <= 5 && engine_heaviness(*xml1) == byte ? *xml1 : byte;
		return byte >= 1 && byte <= 3 ? byte + 1 : byte;
	}
	constexpr int impact_level(int h) { return (h + 1) * 2; } // XML1 0x3fd7a..0x3fd9e (in 0x3f980)
	constexpr bool telekinesis_allows(int h, int limit) { return h <= limit; } // XML1 0xdf426
	// Pairing correction: 0xdf3b0 is the TK filter and 0x39a30 attenuates impulse. The actual corresponding
	// throw path is 0xdff10, at 0xe07ef: signed damagelevel / 3, independent of heaviness. XML2 0x4f3e85
	// instead uses max(damage / 10, 3) * max(byte, 1). The suspend cache's +0xb is tklevel (0x4f3691), not source damagelevel.
	// Rule46 TelekinesisRule replaces this entire callback and reads separate
	// damagelevel from the attack record. Without rule46 the legacy revision2
	// bridge below remains in effect; its parameter pairing is not source parity.
	constexpr int carried_damage(int damagelevel) { return damagelevel / 3; }

	// The gate's answer: the game's rule unless the object carries XML1's heaviness and its byte is still the one
	// the builder wrote for it (a script that changed the byte gets the game's rule back).
	enum class verdict { refuse = 0, lift = 1, game = 2 };
	constexpr verdict decide(std::optional<int> xml1, int heaviness_byte, int might_heaviness)
	{
		if (!xml1 || *xml1 < 0 || *xml1 > xml1_max_heaviness || engine_heaviness(*xml1) != heaviness_byte) return verdict::game;
		return xml1_liftable(might_heaviness, *xml1) ? verdict::lift : verdict::refuse;
	}

	// Remembered by the object's handle (+0x1c), as break_rule keeps xml1structure.
	class heaviness_store
	{
	public:
		static constexpr std::uint32_t slots = 4096;
		void set(std::uint32_t handle, std::optional<int> xml1)
		{
			auto& s = entries_[handle % slots];
			s.handle = handle;
			s.value = xml1 && *xml1 >= 0 && *xml1 <= xml1_max_heaviness ? static_cast<std::int8_t>(*xml1) : std::int8_t{-1};
		}
		std::optional<int> get(std::uint32_t handle) const
		{
			const auto& s = entries_[handle % slots];
			return handle && s.handle == handle && s.value >= 0 ? std::optional<int>(s.value) : std::nullopt;
		}
	private:
		struct entry { std::uint32_t handle = 0; std::int8_t value = -1; };
		std::array<entry, slots> entries_{};
	};

	// ---- XMen2.exe -------------------------------------------------------------------------------------------------
	constexpr address parse_hook_va = 0x498991;   // mov al,[esp+0x14]; cmp al,3; jbe; mov al,3 (the heaviness clamp)
	constexpr address parse_resume_va = 0x49899b;
	constexpr address gate_hook_va = 0x427fa3;    // cmp byte [esi+0x31d],3; jae 0x427fce
	constexpr address gate_native_va = 0x427fac;  // lea ecx,[edi+0x470] (the game's might compare)
	constexpr address gate_refuse_va = 0x427fce;  // pop edi; xor al,al; pop esi; ret 4
	constexpr address might_getter_va = 0x427dc0;
	constexpr address impact_hook_va = 0x4306c7, impact_resume_va = 0x4306d1;
	constexpr address carry_hook_va = 0x4f3e85, carry_resume_va = 0x4f3ebd;
	constexpr address tk_hook_va = 0x4f2e16, tk_resume_va = 0x4f2e1d;
	constexpr std::array<std::uint8_t, 10> impact_displaced{0x8a,0x86,0x1d,0x03,0,0,0xfe,0xc0,0xd0,0xe0};
	constexpr std::array<std::uint8_t, 7> tk_displaced{0x0f,0xb6,0x96,0x1d,0x03,0,0};
	constexpr std::array<std::uint8_t, 56> carry_displaced{
		0x8a,0x86,0x1d,0x03,0,0,0x3c,0x01,0xbb,0x01,0,0,0,0x72,0x03,0x0f,0xb6,0xd8,
		0x8b,0x44,0x24,0x10,0x0f,0xbf,0x48,0x08,0xb8,0x67,0x66,0x66,0x66,0xf7,0xe9,
		0xc1,0xfa,0x02,0x8b,0xc2,0xc1,0xe8,0x1f,0x03,0xc2,0x83,0xf8,0x03,0x7d,0x05,
		0xb8,0x03,0,0,0,0x0f,0xaf,0xc3};
	constexpr std::uint32_t object_heaviness = 0x31d, entity_handle = 0x1c, actor_lift_stats = 0x470, reader_number = 0x18;
	constexpr std::array<std::uint8_t, 10> parse_displaced{0x8a, 0x44, 0x24, 0x14, 0x3c, 0x03, 0x76, 0x02, 0xb0, 0x03};
	constexpr std::array<std::uint8_t, 9> gate_displaced{0x80, 0xbe, 0x1d, 0x03, 0x00, 0x00, 0x03, 0x73, 0x22};

	inline constexpr std::array<guard, 12> guards{{
		{impact_hook_va, "8a861d030000fec0d0e08886e2020000", "throw impact level, then store at +0x2e2"},
		{carry_hook_va, "8a861d0300003c01bb0100000072030fb6d88b4424100fbf4808b867666666f7e9c1fa028bc2c1e81f03c283f8037d05b8030000000fafc389442428db442428d95c2420", "carried throw damage, then integer-to-float conversion"},
		{tk_hook_va, "0fb6961d0300000fbe470a3bd07f51", "TK unsigned heaviness vs signed trigger limit, skip 0x4f2e76"},
		{0x4f3674, "558d4b1ae8f388fbff558d4b1888470ae89776feff558d4b1c66894708e8da88fbff88470b", "cache heaviness +0xa, ImpactDamage +8 and tklevel +0xb"},
		{0x4f3948, "b9e09c7800e8defbffff89442410", "throw cache entry kept at stack +0x10"},
		{0x4f3ef1, "8b442420d99ef0020000d94424208986b8020000", "computed carried damage stored at physent +0x2b8"},
		{0x4f2e76, "32c05ec3", "TK filter refusal epilogue"},
		{0x498980, "8b178d4424145068ac9d68008bcfff5218", "physent reader asks for `heaviness` (number getter vt+0x18)"},
		{parse_hook_va, "8a4424143c037602b003", "the heaviness clamp to 0..3"},
		{parse_resume_va, "8d9e880200005388861d030000", "the heaviness byte store at +0x31d"},
		{0x427f60, "568b74240885f6578bf97462f6860d030000047559e826080a0084c07425e83d080a0084c0751ce824080a0084c07513e8cb1c0a006683784c0074075fb0015ec2040080be1d0300000373228d8f70040000e809feffff660fb68e1d030000660fb6c0663bc15f0f9dc05ec204005f32c05ec204",
		 "the lift test 0x427f60 (carried, overrides, heaviness 3, might compare)"},
		{might_getter_va, "568bf1f6860a010000017405e84f4111008a860a010000c0e803240f5ec3", "might_heaviness getter (stats +0x10a bits 3-6)"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + (g.va - limits_rules::image_base), g.hex)) return &g;
		return nullptr;
	}
}
