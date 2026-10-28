// address_space_rules.hpp without the game (E12): the LAA flag word, the 2 GB straddle test the guard relies on,
// the high-address test's reservation sizes, the region totals and the status words; and the test pipe's `memory`
// verb. Invented addresses; no game data.
#include "address_space_rules.hpp"
#include "test_input_rules.hpp"
#include "test_state_rules.hpp"

#include <cstring>

#include <cstdio>

namespace
{
	int failures = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		failures += ok ? 0 : 1;
	}
	using namespace address_space_rules;
}

int main()
{
	std::setbuf(stdout, nullptr);
	check(!large_address_aware(retail_characteristics) && large_address_aware(retail_characteristics | laa_flag) &&
	          (retail_characteristics | laa_flag) == 0x012F, "the retail word 0x010f is not LAA; 0x012f is");

	check(!straddles_two_gb(0x7FFE0000, 0x10000) && !straddles_two_gb(0x7FFF0000, 0x10000) && !straddles_two_gb(0x80000000, 0x10000),
	      "blocks ending at or starting at 2 GB do not straddle it");
	check(straddles_two_gb(0x7FFF0000, 0x20000) && straddles_two_gb(0x7FFFF000, 0x2000) && !straddles_two_gb(0x7FFF0000, 0),
	      "a block from below 2 GB to above it straddles; an empty one does not");
	check(guard_base + guard_size == two_gb, "the guard block is the last 64 KB below 2 GB");

	check(reservable(0x10000000, 0x100000, 0x40000) == 0x40000, "the test takes no more than the excess");
	check(reservable(0x10001000, 0x30000, 1ull << 32) == 0x20000 && reservable_start(0x10001000) == 0x10010000,
	      "the start is rounded up to 64 KB; only whole blocks");
	check(reservable(0x7FF00000, 0x200000, 1ull << 32) == 0x100000, "never past 2 GB");
	check(reservable(0x80000000, 0x100000, 1ull << 32) == 0 && reservable(0x10000000, 0x100000, 0xFFFF) == 0,
	      "nothing above 2 GB, nothing for an excess under 64 KB");
	check(reservable(0x10000000, 0x30000, 0x28000) == 0x20000, "the excess is rounded down to whole blocks");

	usage u;
	add_region(u, 0x00010000, 0x10000, 0x10000, 0, 0);            // free low
	add_region(u, 0x10000000, 0x200000, 0x1000, 0x20000, 0x10000000); // committed private low
	add_region(u, 0x90000000, 0x300000, 0x1000, 0x20000, 0x90000000); // committed private high
	add_region(u, 0xA0000000, 0x100000, 0x2000, 0x20000, 0xA0000000); // reserved high: not committed
	add_region(u, 0xB0000000, 0x100000, 0x1000, 0x1000000, 0xB0000000); // committed image high
	check(u.free_low == 0x10000 && u.committed_private == 0x500000 && u.committed_high == 0x400000 && u.highest_committed == 0xB0100000 &&
	          !u.straddling, "the totals: private, above 2 GB, free below 2 GB, the highest committed byte");
	usage s;
	add_region(s, 0x80000000, 0x10000, 0x2000, 0x20000, 0x7FFF0000); // the upper part of a block allocated below 2 GB
	add_region(s, 0x7FFF0000, 0x20000, 0x10000, 0, 0);               // a free region across 2 GB counts only its low half
	check(s.straddling && s.free_low == 0x10000, "a block whose allocation starts below 2 GB and reaches above it is reported");

	const auto words = describe("on", 0xFFFEFFFF, u, "on", 0);
	check(words == "laa on; top 0xFFFEFFFF; above 2 GB 4 MB; private 5 MB; highest 0xB0100000; 2 GB guard on", words.c_str());
	const auto tested = describe("on", 0xFFFEFFFF, s, "unavailable", 1536ull * mb);
	check(tested.find("; a block crosses 2 GB") != std::string::npos && tested.find("high-address test 1536 MB reserved below 2 GB") != std::string::npos,
	      "the status names a straddling block and the test's reservation");
	check(describe("off", 0x7FFEFFFF, usage{}, "off", 0).rfind("laa off; top 0x7FFEFFFF;", 0) == 0, "LAA off: just the words");

	using test_input_rules::parse_command;
	const auto memory = parse_command("memory high 256");
	check(memory.what == test_input_rules::command::kind::memory && memory.text.find("high 256") != std::string::npos,
	      "the pipe's memory verb hands the rest of the line on");
	check(parse_command("MEMORY").what == test_input_rules::command::kind::memory, "memory alone (status)");
	check(parse_command("memoryx").what != test_input_rules::command::kind::memory, "only the whole verb");

	// the test pipe's state reader (test_state_rules.hpp) reads game objects above 2 GB (it refused them before E12)
	struct high_memory
	{
		bool read(std::uint32_t a, void* dst, std::size_t n)
		{
			if (a < 0x90000000u || a + n > 0x90001000u) return false;
			const std::uint32_t value = 42;
			std::memset(dst, 0, n);
			std::memcpy(dst, &value, n < 4 ? n : 4);
			return true;
		}
	} hm;
	test_state_rules::reader<high_memory> high_reader(hm);
	check(high_reader.get<std::uint32_t>(0x90000000u) == 42u, "the state reader reads an object above 2 GB");
	check(!high_reader.get<std::uint32_t>(0xFFFF0000u) && !high_reader.get<std::uint32_t>(0x8000u), "the top 64 KB and the first 64 KB stay refused");

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
	return failures ? 1 : 0;
}
