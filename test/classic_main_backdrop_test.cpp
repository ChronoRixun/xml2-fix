#include "classic_main_backdrop_rules.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <cstdio>

using namespace classic_main_backdrop_rules;

struct memory
{
	static constexpr address base = 0x10000;
	std::array<std::uint8_t, 2048> bytes{};
	bool read(address where, void* out, std::size_t size)
	{
		if (where < base || where - base > bytes.size() || size > bytes.size() - (where - base)) return false;
		std::memcpy(out, bytes.data() + where - base, size);
		return true;
	}
	void put(address where, address value) { std::memcpy(bytes.data() + where - base, &value, sizeof value); }
};

memory fixture()
{
	memory m;
	m.put(0x10024, 0x20000);
	m.put(0x10040, 0x10100);
	m.put(0x10044, 0x10200);
	for (auto container : {0x10100u, 0x10200u}) { m.put(container + 8, 2); m.put(container + 12, 2); }
	m.put(0x10110, 0x10700); m.put(0x10700, 0x10300);
	m.put(0x10210, 0x10740); m.put(0x10740, 0x10400);
	for (auto list : {0x10300u, 0x10400u}) { m.put(list + 8, 3); m.put(list + 12, 4); }
	m.put(0x10310, 0x10500); m.put(0x10410, 0x10600);
	for (auto data : {0x10500u, 0x10600u})
	{
		m.put(data, 0x40000); m.put(data + 4, 0x20000); m.put(data + 8, 0x50000);
	}
	return m;
}

int main()
{
	constexpr std::array<std::string_view, 2> labels{"tileA", "tileB"};
	assert(in_scene("menu/synthetic", "menu/synthetic", "folder/tileA", labels, .1f, true));
	assert(in_scene("menu/synthetic", "menu/synthetic", "folder\\tileB", labels, .1f, true));
	assert(!in_scene("play/synthetic", "menu/synthetic", "tileA", labels, .1f, true));
	assert(!in_scene("menu/synthetic", "menu/synthetic", "tileC", labels, .1f, true));
	assert(!in_scene("menu/synthetic", "menu/synthetic", "tileA", labels, .1f, false));
	assert(!in_scene("menu/synthetic", "menu/synthetic", "tileA", labels, 1.f, true));
	assert(!in_scene("menu/synthetic", "menu/synthetic", "tileA", labels, std::numeric_limits<float>::quiet_NaN(), true));

	auto m = fixture(); const auto before = m.bytes; list_plan result;
	assert(plan(m, 0x10000, 0x30000, result));
	assert(result.begin == 0x10300 && result.end == 0x10400 && result.add_begin && result.add_end);
	assert(m.bytes == before); // planning never mutates a live list
	m.put(0x10508, 0x30000); m.put(0x10608, 0x30000);
	assert(plan(m, 0x10000, 0x30000, result) && !result.add_begin && !result.add_end);
	m.put(0x10608, 0x50000);
	assert(plan(m, 0x10000, 0x30000, result) && !result.add_begin && result.add_end);

	for (auto mutation : {0, 1, 2, 3, 4, 5})
	{
		m = fixture(); result = {0xabcdef, 0xfedcba, false, false};
		if (mutation == 0) m.put(0x10208, 3); // second container differs: no partial plan
		if (mutation == 1) m.put(0x1040c, 2); // capacity below live count
		if (mutation == 2) m.put(0x10408, 65); // unbounded list
		if (mutation == 3) m.put(0x10604, 0x60000); // missing diffuse binding
		if (mutation == 4) m.put(0x10410, 0x7fffffff); // unreadable storage
		if (mutation == 5) m.put(0x10740, 0x10300); // begin/unwind aliases
		assert(!plan(m, 0x10000, 0x30000, result));
		assert(result.begin == 0xabcdef && result.end == 0xfedcba);
	}
	assert(!plan(m, 0x10000, 0, result));
	// Identical label/zone/material properties are insufficient without asset ownership.
	m = fixture();
	m.put(0x10108, 1); m.put(0x1010c, 1); m.put(0x10110, 0x10500);
	m.put(0x1012c, 0); m.put(0x10500, 0x10200); m.put(0x10218, 0x10000); m.put(0x10200,0x70000);
	assert(owned_by_directory(m, 0x10100, 0x10000,0x70000));
	assert(!owned_by_directory(m, 0x10100, 0x10080,0x70000)); // same label outside the room stays untouched
	const auto outside_before=m.bytes;
	const bool same_properties=in_scene("menu/synthetic","menu/synthetic","tileA",labels,.1f,true);
	assert(same_properties && !owned_by_directory(m,0x10100,0x10080,0x70000));
	assert(m.bytes==outside_before); // ownership refusal is read-only; no pass mutations occur
	m.put(0x1012c, 0x10300); m.put(0x10308, 1); m.put(0x1030c, 1);
	m.put(0x10310, 0x10600); m.put(0x10600, 0x10080);
	assert(!owned_by_directory(m, 0x10100, 0x10000,0x70000)); // sharing uses resolved pointers, not stale entries
	assert(owned_by_directory(m, 0x10100, 0x10080,0x70000));
	m.put(0x10308, 2);
	assert(!owned_by_directory(m, 0x10100, 0x10080,0x70000)); // unsupported layout fails closed

	// Room pin lifetime: one reference per distinct room, the previous one dropped, and the last one
	// handed back exactly once at shutdown - after which nothing is pinned again.
	{
		room_pin pin;
		auto s = pin.capture(0x5000);
		assert(s.retain == 0x5000 && s.release == 0 && pin.held == 0x5000);
		s = pin.capture(0x5000);
		assert(!s.retain && !s.release && pin.held == 0x5000); // same directory: no extra reference
		s = pin.capture(0);
		assert(!s.retain && !s.release && pin.held == 0x5000);
		s = pin.capture(0x6000);
		assert(s.retain == 0x6000 && s.release == 0x5000 && pin.held == 0x6000);
		assert(pin.close() == 0x6000 && pin.held == 0 && pin.closed);
		assert(pin.close() == 0); // released once
		s = pin.capture(0x7000);
		assert(!s.retain && !s.release && pin.held == 0); // a load during teardown is never pinned
		room_pin empty;
		assert(empty.close() == 0 && empty.closed);
	}
	// The shutdown hook replaces exactly WinMain's loop-exit call to 0x5f5720.
	{
		const auto retail = call_to(loop_exit_call, loop_exit_target);
		assert(std::equal(retail.begin(), retail.end(), loop_exit_bytes.begin() + (loop_exit_call - loop_exit_site)));
		assert(loop_exit_bytes[0] == 0xff && loop_exit_bytes[5] == 0x75 && // call [edx+0xc]; jne back to the loop head
		       loop_exit_site + 7 + std::int8_t(loop_exit_bytes[6]) == 0x403541);
		const auto hooked = call_to(loop_exit_call, 0x10001000);
		std::int32_t relative = 0;
		std::memcpy(&relative, hooked.data() + 1, 4);
		assert(hooked[0] == 0xe8 && loop_exit_call + 5 + relative == 0x10001000);
		const auto backwards = call_to(loop_exit_call, 0x401000);
		std::memcpy(&relative, backwards.data() + 1, 4);
		assert(loop_exit_call + 5 + relative == 0x401000);
	}
	std::puts("classic main backdrop: scope, paired-list planning, duplicates, malformed layouts and shutdown release PASS");
}
