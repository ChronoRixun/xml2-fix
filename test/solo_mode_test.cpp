// solo_mode_rules.hpp without the game: the party toggle's guards, membership by handle, and the automatic
// regroup schedule (default.xbe 0x33a70, 0x73420). Invented handles; no game data.
#include "solo_mode_rules.hpp"
#include <cstdio>

namespace
{
	int failures = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		failures += ok ? 0 : 1;
	}
	using namespace solo_mode_rules;
	member hero(std::uint32_t h, bool human) { member m; m.handle = h; m.character = true; m.seated = true; m.human = human; return m; }
}

int main()
{
	std::setbuf(stdout, nullptr);
	const std::array<member, 4> four{hero(0x1001, true), hero(0x1002, false), hero(0x1003, false), hero(0x1004, false)};

	party p;
	check(!p.active() && !p.contains(0x1001), "a new party is not solo");
	check(p.toggle(false, four, 4) == toggle_result::unchanged, "leaving while not solo changes nothing");
	check(p.toggle(true, four, 4) == toggle_result::entered && p.size() == 4, "entering marks every seated member");
	check(p.contains(0x1001) && p.contains(0x1004) && !p.contains(0x2000) && !p.contains(0), "membership is by handle; 0 is never solo");
	check(p.toggle(true, four, 4) == toggle_result::unchanged, "entering again changes nothing");
	check(either(p, 0x2000, 0x1002) && either(p, 0x1003, 0x2000) && !either(p, 0x2000, 0x2001), "AI test: follower or leader solo");
	check(p.toggle(false, four, 4) == toggle_result::left && !p.active() && !p.contains(0x1001), "leaving clears the whole party");

	// 0x33a70's guards: more than one player refuses; a party of one refuses; unseated or non-characters don't count.
	std::array<member, 4> coop = four;
	coop[1].human = true;
	check(p.toggle(true, coop, 4) == toggle_result::refused_players && !p.active(), "two players: refused");
	std::array<member, 4> alone{hero(0x1001, true), member{}, member{}, member{}};
	check(p.toggle(true, alone, 4) == toggle_result::too_few && !p.active(), "a party of one: refused");
	std::array<member, 4> unseated = alone;
	unseated[1] = hero(0x1002, false);
	unseated[1].seated = false;
	unseated[2].handle = 0x1003; // not a character
	check(p.toggle(true, unseated, 4) == toggle_result::too_few, "unseated members and non-characters are not in the party list");
	unseated[1].seated = true;
	check(p.toggle(true, unseated, 4) == toggle_result::entered && p.size() == 2 && !p.contains(0x1003), "only listed members become solo");
	// A body made after the toggle (new handle) is not solo, as default.xbe's constructor clears the bit.
	check(!p.contains(0x1005), "a new body is not solo");
	p.clear();

	// 0x73420: first check 1.5 s after solo began, then every 1.0 s, strictly after the scheduled time.
	p.toggle(true, four, 4);
	check(!p.regroup_due(true, 10.0f) && p.next_check() == 11.5f, "solo begins: first check scheduled 1.5 s later");
	check(!p.regroup_due(true, 11.5f), "not due at the scheduled time itself");
	check(p.regroup_due(true, 11.6f) && p.next_check() == 12.6f, "due after it, next one 1.0 s later");
	check(!p.regroup_due(true, 12.0f) && p.regroup_due(true, 12.7f), "then once a second");
	check(!p.regroup_due(false, 13.0f) && p.next_check() == 0.0f, "a followed hero that isn't solo resets the schedule");
	check(!p.regroup_due(true, 20.0f) && p.next_check() == 21.5f, "solo again: 1.5 s from then");
	p.clear();
	check(p.next_check() == 0.0f && !p.active(), "clear resets the schedule");

	// Constants the original reads (default.xbe 0x3ca844 / 0x3c6e40, 0x34bb0, 0x31860 / 0x318b0).
	check(first_check_seconds == 1.5f && retry_seconds == 1.0f, "regroup timing");
	check(regroup_radius == 80.0f && regroup_steps == 200 && regroup_reached == 2 && regroup_view_margin == 150.0f, "regroup query");
	check(voice_begin == 0x13 && voice_end == 0x14, "voice lines");

	// More than max_members listed: the rest are ignored, never written past the array.
	std::array<member, 10> many{};
	for (std::uint32_t i = 0; i < 10; ++i) many[i] = hero(0x3000 + i, i == 0);
	check(p.toggle(true, many, 10) == toggle_result::entered && p.size() == max_members && !p.contains(0x3009), "bounded membership");
	return failures ? 1 : 0;
}
