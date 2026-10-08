#pragma once

#include "ini_rules.hpp"

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace confirmation_release_rules
{
	constexpr std::uint32_t image_base = 0x400000;
	constexpr std::uint32_t raw_site = 0x62887d, raw_continue = 0x628884;
	constexpr std::uint32_t accept_site = 0x5eb7c6, accept_continue = 0x5eb7cc, cancel_check = 0x5eb677;
	constexpr std::uint32_t close_site = 0x5eb8ab, close_continue = 0x5eb8b2;
	constexpr std::uint32_t keyboard_site = 0x628621, keyboard_continue = 0x628626;
	constexpr std::uint32_t keyboard_retry_site = 0x628642, keyboard_retry_continue = 0x62864b;
	constexpr std::uint32_t mouse_site = 0x6286b7, mouse_continue = 0x6286bf;

	// Physical identity, independent of the current binding row, player or popup slot.
	struct source
	{
		std::uint32_t device = 0, control = 0;
		friend bool operator==(const source&, const source&) = default;
	};
	constexpr bool supported(const source s)
	{
		return (s.device == 1 && s.control < 256) ||
		       (s.device == 2 && s.control >= 9 && s.control <= 11) ||
		       (s.device >= 3 && s.device <= 12 && s.control >= 1 && s.control <= 49);
	}
	constexpr std::size_t source_index(const source s) { return (s.device - 1) * 256 + s.control; }

	struct pad_state
	{
		std::array<std::int32_t, 8> axes{};
		std::uint32_t pov = 0xffffffff;
		std::array<std::uint8_t, 29> buttons{};
	};
	struct physical_state
	{
		std::array<std::uint8_t, 256> keys{};
		std::array<std::uint8_t, 3> mouse{};
		std::array<pad_state, 10> pads{};
		bool keyboard_known = true, mouse_known = true;
		std::array<bool, 10> pad_known{true, true, true, true, true, true, true, true, true, true};
	};
	inline bool known(const physical_state& input, const source s)
	{
		if (!supported(s)) return false;
		if (s.device == 1) return input.keyboard_known;
		if (s.device == 2) return input.mouse_known;
		return input.pad_known[s.device - 3];
	}

	// Matches the binding's physical direction/button. Analog release is direction neutral,
	// not the translated action disappearing during menu/row transitions.
	inline bool down(const physical_state& input, const source s)
	{
		if (!supported(s)) return false;
		if (s.device == 1) return input.keys[s.control] != 0;
		if (s.device == 2) return input.mouse[s.control - 9] != 0;
		const auto& p = input.pads[s.device - 3];
		if (s.control >= 21) return p.buttons[s.control - 21] != 0;
		if (s.control <= 16)
		{
			const auto axis = p.axes[(s.control - 1) / 2];
			return (s.control & 1) ? axis > 0 : axis < 0;
		}
		const auto hat = p.pov;
		if (hat == 0xffffffff) return false;
		switch (s.control)
		{
		case 17: return hat >= 2250 && hat <= 15750;
		case 18: return hat >= 20250 && hat <= 33750;
		case 19: return hat >= 11250 && hat <= 24750;
		case 20: return hat >= 29250 || hat <= 6750;
		default: return false;
		}
	}

	struct latch
	{
		std::bitset<12 * 256> consumed;
		void sample(const physical_state& input)
		{
			for (std::size_t i = 0; i < consumed.size(); ++i)
			{
				const source s{static_cast<std::uint32_t>(i / 256 + 1), static_cast<std::uint32_t>(i % 256)};
				if (consumed[i] && known(input, s) && !down(input, s)) consumed.reset(i);
			}
		}
		bool available(const source s) const { return supported(s) && !consumed[source_index(s)]; }
		bool allows(const std::span<const source> candidates) const
		{
			for (const auto s : candidates) if (available(s)) return true;
			return false;
		}
		void consume(const std::span<const source> accepted)
		{
			for (const auto s : accepted) if (supported(s)) consumed.set(source_index(s));
		}
	};

	// Explicit feature until ordinary XML2 and network/co-op compatibility is established.
	inline bool enabled(const std::optional<std::string_view> value) { return ini_rules::flag(value).value_or(false); }

	// The popup's two mask producers: an owned player's input, with player 1's
	// native fallback; otherwise the aggregate or the ordinary menu participants.
	constexpr bool eligible_player(const int owner, const int player, const unsigned primary, const unsigned fallback,
	                               const bool aggregate, const int active, const bool active_remote,
	                               const bool participant, const bool aggregate_allowed)
	{
		if ((fallback & 16) && player == 0) return true;
		if (!(primary & 16)) return false;
		if (owner >= 0 && owner < 4) return player == owner;
		if (aggregate) return player == active || aggregate_allowed;
		return active_remote ? player == active : participant;
	}

	struct guard { std::uint32_t va; std::string_view hex; const char* what; };
	// The native adapter checks all of these before making any page writable.
	inline constexpr auto guards = std::to_array<guard>({
		{raw_site, "8b7c24108a4701", "raw input before menu suppression"},
		{accept_site, "8a8658150000", "popup accept entry"},
		{close_site, "8b078bcfff5078", "popup close query"},
		{keyboard_site, "3d1e000780", "keyboard read result before retry"},
		{keyboard_retry_site, "680001000050ff5124", "keyboard retry read"},
		{mouse_site, "8b106a1450ff5224", "mouse state read"},
		{0x5eb64a, "6a01518bc8ff926001000084c00f8569010000", "first accept dispatch"},
		{0x5eb664, "6a01538bc8ff926001000084c00f854f010000", "fallback accept dispatch"},
		{0x5d4d60, "8b442404c1e8042401c20800", "native accept bit"},
		{0x5eb338, "8b873c40000033db3bc37c1183f8037d0c69c0601500008d743818", "popup slot layout"},
		{0x5eb393, "0fbe8e5b150000", "popup selection"},
		{0x5eb41b, "8a865c150000", "popup owner"},
		{0x5eb496, "f6875640000002", "aggregate input mode"},
		{0x550b70, "8b91f80200008b817403000033c223c2c3", "controller action edges"},
		{0x5510d0, "8b54240483faff7e2d83fa047d288b449104", "logical to physical player"},
		{0x5511a0, "8b4424048b44810485c07d05", "menu participant"},
		{0x551360, "8b4134c3", "active player"},
		{0x5513f0, "8a411c84c074048b4120c38b4130c3", "player count"},
		{0x48d260, "568b74240885f6578bf97c19", "remote player test"},
		{0x48de70, "53568b74240c32db85f67c51", "aggregate input permission"},
		{0x6294b0, "8b54240885d27c30", "binding record layout"},
		{0x619c69, "b804000000c3", "accept action is binding 4"},
		{0x628600, "8dabe4250000558dbbe4260000", "raw keyboard layout"},
		{0x628746, "8a83dc04000084c0741e8a83c8040000", "raw mouse buttons"},
		{0x6287e0, "8da8f00400008d580c", "raw pad layout"},
		{0x6287d4, "33c98988cc290100", "successful pad read mask reset"},
		{0x628848, "0990cc290100", "successful pad read mask update"},
		{0x62886a, "81c51001000083f80a", "raw pad stride and count"},
		{0x6a24cc, "604d5d00", "menu accept method"},
		{0x69a180, "d0105500", "controller lookup method"},
		{0x69a188, "a0115500", "participant method"},
		{0x69a194, "60135500", "active player method"},
		{0x69a1a4, "f0135500", "player count method"},
		{0x69a10c, "700b5500", "controller edge method"},
		{0x6898d4, "60d24800", "remote player method"},
		{0x6898e8, "70de4800", "aggregate permission method"}
	});

	inline int nibble(const char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			for (std::size_t i = 0; i < g.hex.size() / 2; ++i)
				if (image[g.va - image_base + i] != static_cast<std::uint8_t>(nibble(g.hex[i * 2]) * 16 + nibble(g.hex[i * 2 + 1]))) return &g;
		return nullptr;
	}
	inline std::array<std::uint8_t, 9> jump(const std::uint32_t site, const std::uint32_t target)
	{
		const auto d = target - (site + 5);
		return {0xe9, static_cast<std::uint8_t>(d), static_cast<std::uint8_t>(d >> 8), static_cast<std::uint8_t>(d >> 16), static_cast<std::uint8_t>(d >> 24), 0x90, 0x90, 0x90, 0x90};
	}
}
