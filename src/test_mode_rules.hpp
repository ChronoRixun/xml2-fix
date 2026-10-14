#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace test_mode_rules
{
	// Retail defaults (0x61b030..0x61b1c3) use F1 and F11. The remaining F keys
	// are available unless the player has rebound them in Advanced Controls.
	constexpr std::array<unsigned char, 3> default_keys{0x42, 0x43, 0x44}; // F8/F9/F10
	constexpr unsigned char function_key(std::string_view name)
	{
		if (name.size() < 2 || name.size() > 3 || (name[0] != 'F' && name[0] != 'f')) return 0;
		unsigned n = 0;
		for (std::size_t i = 1; i < name.size(); ++i) {
			if (name[i] < '0' || name[i] > '9') return 0;
			n = n * 10 + name[i] - '0';
		}
		if (n >= 1 && n <= 10) return static_cast<unsigned char>(0x3a + n);
		return n == 11 ? 0x57 : n == 12 ? 0x58 : 0;
	}
	struct edges
	{
		std::array<bool, 3> held{};
		// Losing input disarms until a released state is seen again; alt-tab and
		// holding a key across a load must never fire an action on return.
		void disarm() { held.fill(true); }
		std::array<bool, 3> read(const unsigned char* state, const std::array<unsigned char, 3>& keys)
		{
			std::array<bool, 3> result{};
			for (std::size_t i = 0; i < keys.size(); ++i) {
				const bool down = (state[keys[i]] & 0x80) != 0;
				result[i] = down && !held[i]; held[i] = down;
			}
			return result;
		}
	};
	constexpr bool party_member(std::uint32_t flags, int controller)
	{
		return (flags & (1u << 20)) && (flags & (1u << 29)) && controller >= 0 && controller < 4;
	}
	constexpr bool killable(std::uint32_t flags, float health, bool invulnerable,
	                        unsigned char life_flags, unsigned char boss_flags, bool scripted_actor)
	{
		return (flags & ((1u << 30) | (1u << 31))) && !(flags & (1u << 29)) &&
		       (flags & (1u << 20)) && health > 0 && !invulnerable &&
		       !(life_flags & 0x10) && !(boss_flags & 0x40) && !scripted_actor;
	}
	// A pain event can contain only a sound/effect. Only its script token protects
	// the actor. A stale/unreadable event must fail closed.
	template<class Reader> bool scripted_event(Reader& r, std::uint32_t token)
	{
		if (!token) return false;
		auto pool = r.template get<std::uint32_t>(0x72989c);
		if (!pool || !*pool) return true;
		auto mask = r.template get<std::uint32_t>(*pool + 0x2724);
		if (mask != 0x1ffu) return true;
		const auto i = token & *mask;
		if (i >= 310) return true;
		auto bits = r.template get<std::uint32_t>(*pool + 0x1d10 + 4 * (i / 32));
		if (!bits || !(*bits & (1u << (i % 32))) || r.template get<std::uint32_t>(*pool + 0x224c + 4 * i) != token) return true;
		return r.template get<std::uint32_t>(*pool + 24 * i + 8) != 0u;
	}
}
