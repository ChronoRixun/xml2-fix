#pragma once

// E12 (Legends Classic PLAN): the address space of a large-address-aware XMen2.exe, without the game.
//
// The Legends Classic build sets IMAGE_FILE_LARGE_ADDRESS_AWARE on its own copy of XMen2.exe (one word of the PE
// file header). A 32-bit process on 64-bit Windows (and Wine / Proton) then gets 4 GB of address space instead of 2.
// Nothing here changes how the game runs; it describes the space for the log, the test pipe's status and the crash
// line, and it decides two test-free guards:
//
//  * the 2 GB guard: with the flag on, the 64 KB block at 0x7FFF0000 (the last one below 2 GB, the no-access block a
//    2 GB process always has) is reserved at start, so no allocation can straddle 0x80000000. The static risk check
//    found signed pointer compares in an MMX mip-filter loop that D3DX8 links into XMen2.exe, cgD3D8.dll and
//    libIGGfx.dll (`cmp edi, ebx` / `jge` with ebx = edi + row bytes): they go wrong only for a buffer that
//    starts below 2 GB and ends above it. Reserving that block keeps every block on one side, as in a 2 GB process.
//  * the high-address test (test pipe only, `memory high <MB>`): reserves the free space below 2 GB except <MB>, so
//    the game's next allocations land above 2 GB - a controlled check that the game works with pointers that have
//    the top bit set (not the reserve-everything stress: <MB> stays free for the game's small allocations).

#include <cstdint>
#include <string>

namespace address_space_rules
{
	constexpr std::uint16_t laa_flag = 0x0020;              // IMAGE_FILE_LARGE_ADDRESS_AWARE
	constexpr std::uint32_t two_gb = 0x80000000u;
	constexpr std::uint32_t guard_base = 0x7FFF0000u;       // the last 64 KB block below 2 GB
	constexpr std::uint32_t guard_size = 0x10000u;
	constexpr std::uint32_t granularity = 0x10000u;         // VirtualAlloc's reservation granularity
	constexpr std::uint32_t retail_characteristics = 0x010F;
	constexpr std::uint32_t mb = 1024u * 1024u;

	constexpr bool large_address_aware(const std::uint16_t characteristics) { return (characteristics & laa_flag) != 0; }

	// A block [base, base + size) straddles 2 GB: it starts below 0x80000000 and ends above it.
	constexpr bool straddles_two_gb(const std::uint32_t base, const std::uint32_t size)
	{
		return size != 0 && base < two_gb && static_cast<std::uint64_t>(base) + size > two_gb;
	}

	// What the high-address test reserves of a free block below 2 GB, given how much free space below 2 GB the test
	// still has to take away (`excess` = free below 2 GB - the MB left free): whole 64 KB blocks only, the start
	// rounded up to the granularity, never past 2 GB. 0 = leave it.
	constexpr std::uint32_t reservable(const std::uint32_t base, const std::uint32_t size, const std::uint64_t excess)
	{
		if (base >= two_gb || size == 0 || excess < granularity) return 0;
		const std::uint64_t start = (static_cast<std::uint64_t>(base) + granularity - 1) / granularity * granularity;
		std::uint64_t end = static_cast<std::uint64_t>(base) + size;
		if (end > two_gb) end = two_gb;
		if (end <= start) return 0;
		std::uint64_t take = (end - start) / granularity * granularity;
		if (take > excess) take = excess / granularity * granularity;
		return static_cast<std::uint32_t>(take);
	}

	// The aligned start of the block reservable() measured.
	constexpr std::uint32_t reservable_start(const std::uint32_t base)
	{
		return static_cast<std::uint32_t>((static_cast<std::uint64_t>(base) + granularity - 1) / granularity * granularity);
	}

	struct usage
	{
		std::uint64_t committed_private = 0; // MEM_COMMIT + MEM_PRIVATE bytes (heaps, VirtualAlloc)
		std::uint64_t committed_high = 0;    // committed bytes at or above 2 GB, any type
		std::uint64_t free_low = 0;          // MEM_FREE bytes below 2 GB
		std::uint32_t highest_committed = 0; // the last committed byte's address + 1 (0 = none)
		bool straddling = false;             // a reserved or committed block crosses 0x80000000
	};

	// One VirtualQuery region into the totals (state: MEM_COMMIT 0x1000, MEM_RESERVE 0x2000, MEM_FREE 0x10000;
	// type: MEM_PRIVATE 0x20000). Regions are cut at 2 GB for the high / low totals.
	inline void add_region(usage& u, const std::uint32_t base, const std::uint32_t size, const std::uint32_t state,
	                       const std::uint32_t type, const std::uint32_t allocation_base)
	{
		const std::uint64_t end = static_cast<std::uint64_t>(base) + size;
		const std::uint64_t low = base >= two_gb ? 0 : (end > two_gb ? two_gb - base : size);
		const std::uint64_t high = size - low;
		if (state == 0x10000u)
		{
			u.free_low += low;
			return;
		}
		if (allocation_base < two_gb && end > two_gb) u.straddling = true;
		if (state != 0x1000u) return;
		if (type == 0x20000u) u.committed_private += size;
		u.committed_high += high;
		if (end > u.highest_committed) u.highest_committed = static_cast<std::uint32_t>(end > 0xFFFFFFFFull ? 0xFFFFFFFFull : end);
	}

	inline std::string hex32(const std::uint32_t v)
	{
		char text[11];
		static const char digits[] = "0123456789ABCDEF";
		text[0] = '0';
		text[1] = 'x';
		for (int i = 0; i < 8; ++i) text[2 + i] = digits[(v >> (28 - 4 * i)) & 0xF];
		text[10] = 0;
		return text;
	}

	inline std::string megabytes(const std::uint64_t bytes) { return std::to_string((bytes + mb / 2) / mb) + " MB"; }

	// The test pipe's status words: "laa on; top 0xFFFEFFFF; above 2 GB 812 MB; private 1530 MB; highest 0xFFE60000;
	// 2 GB guard on" (laa = the exe header; top = the highest user address Windows reports).
	inline std::string describe(const char* laa, const std::uint32_t top, const usage& u, const char* guard,
	                            const std::uint64_t test_reserved)
	{
		std::string s = std::string("laa ") + laa + "; top " + hex32(top) + "; above 2 GB " + megabytes(u.committed_high) +
		                "; private " + megabytes(u.committed_private) + "; highest " + hex32(u.highest_committed) +
		                "; 2 GB guard " + guard;
		if (u.straddling) s += "; a block crosses 2 GB";
		if (test_reserved) s += "; high-address test " + megabytes(test_reserved) + " reserved below 2 GB";
		return s;
	}
}
