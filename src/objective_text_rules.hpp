#pragma once

// Completion text is keyed by the native state index, not record address: the
// engine sorts whole objective records after parsing. No saved record is changed.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace objective_text_rules
{
	constexpr std::size_t text_capacity = 160; // native description char[160]
	constexpr std::uint32_t state_bytes = 0x72b118;
	constexpr std::size_t index_offset = 0x1a9;
	constexpr std::size_t description_offset = 0xe0;
	constexpr std::uint32_t attribute_parser = 0x488890;
	constexpr std::uint32_t attribute_call = 0x489a2f;
	constexpr std::uint32_t allocate_site = 0x489a01;
	constexpr std::uint32_t allocate_continue = 0x489a07;
	constexpr std::uint32_t journal_site = 0x5cd5e7;
	constexpr std::uint32_t journal_continue = 0x5cd5ed;
	constexpr std::uint32_t primary_site = 0x5ce1c9;
	constexpr std::uint32_t primary_continue = 0x5ce1d5;

	inline bool same_field(std::string_view text, std::string_view expected) noexcept
	{
		if (text.size() != expected.size()) return false;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			const char c = text[i] >= 'A' && text[i] <= 'Z' ? char(text[i] + ('a' - 'A')) : text[i];
			if (c != expected[i]) return false;
		}
		return true;
	}

	struct texts
	{
		std::array<std::array<char, text_capacity>, 256> entries{};
		void reset(std::uint8_t index) noexcept { entries[index].fill(0); }
		void attribute(std::uint8_t index, std::string_view key, std::string_view value) noexcept
		{
			if (!same_field(key, "updatedescription")) return;
			reset(index);
			const auto n = std::min(value.size(), text_capacity - 1);
			if (n) std::memcpy(entries[index].data(), value.data(), n);
		}
		const char* select(std::uint8_t index, std::uint8_t state, const char* original) const noexcept
		{
			return (state & 0x40) && entries[index][0] ? entries[index].data() : original;
		}
	};

	struct guard { std::uint32_t va; std::string_view hex; };
	// Retail instructions checked before all four hooks are installed together.
	constexpr std::array<guard, 7> guards{{
		{attribute_parser, "56578b7c240c6878926800578bf1"},
		{attribute_call, "e85ceeffff8b442410"},
		{allocate_site, "8890a90100008b5d0c42"},
		{primary_site, "8a8636ffffff8dbe36ffffff83c40c84c07438"},
		{journal_site, "8d96e0000000528d842434010000"},
		{0x488ab0, "0fb681a90100008a8018b17200c0e8062401c3"},
		{0x488430, "535556578b7c24146a208bf15756"}, // whole-record copy used by objective sorting
	}};

	template <std::size_t N>
	std::array<std::uint8_t, N> branch(std::uint32_t site, std::uint32_t target, bool call = false)
	{
		static_assert(N >= 5);
		std::array<std::uint8_t, N> bytes{};
		bytes.fill(0x90);
		bytes[0] = call ? 0xe8 : 0xe9;
		const auto displacement = target - site - 5;
		std::memcpy(bytes.data() + 1, &displacement, sizeof displacement);
		return bytes;
	}
}
