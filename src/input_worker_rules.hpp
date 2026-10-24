#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace input_worker_rules
{
	constexpr std::uint32_t image_base = 0x400000;
	constexpr std::uint32_t entry = 0x618c90, entry_next = 0x618c96;
	constexpr std::uint32_t finish = 0x618e5a, finish_next = 0x618e6b;
	constexpr std::uint32_t manager = 0xa6a7e4, stopped = 0xa6a7e1;
	struct guard { std::uint32_t va; std::string_view hex; };
	// Short destination and dependency signatures, not a replacement worker.
	inline constexpr guard guards[] {
		{entry, "558bec83e4f8"},
		{finish, "c705e4a7a60000000000c605e1a7a60001"},
		{0x618cd0, "8b15e4a7a60085d2"}, // initial manager check
		{0x618d20, "8b0de4a7a6008d5e01e842e60000"}, // count reload/call
		{0x618d32, "8b0de4a7a60056e852e60000"}, // name reload/call
		{0x618e46, "a0e0a7a60084c00f847dfeffff"}, // stop loop
		{0x618e53, "8b8c246c020000"}, // cookie before finish
		{finish_next, "33c0e8ef9205005f5e5b8be55dc3"}, // shared return
		{0x61fc80, "68908c6100"}, // thread entry registration
		{0x61c296, "c605e0a7a60001"}, // shutdown request
		{0x61c2bd, "381de1a7a60074e0"}, // shutdown waits for completion
	};
	inline unsigned nibble(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			for (std::size_t i = 0; i < g.hex.size() / 2; ++i)
				if (image[g.va - image_base + i] != ((nibble(g.hex[2*i]) << 4) | nibble(g.hex[2*i+1]))) return &g;
		return nullptr;
	}
}
