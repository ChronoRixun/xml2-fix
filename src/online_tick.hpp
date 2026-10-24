#pragma once

// [Online] TickRate: the frames per second an online session runs at.
//
//   [Online]
//   TickRate = 60   ; 60 or 30; absent = the game's own 30 (no agreement entry)
//
// CClient::frame (0x401d70) writes its minimum frame time at the top of every frame: 1/30 s while the session's
// online flag (0x610d20, session +0x420) is set, 1/60 s otherwise, and spins at the end of the frame until that much
// has passed. Lockstep runs one simulation step, one input record per participant and (every 30 frames) one checksum
// per frame, with each step's duration the slowest participant's measured frame time (0x559190), so the simulation
// keeps wall-clock speed at either rate. TickRate=60 replaces the online 1/30 with 1/60 (frame_rate_rules.hpp,
// online_tick_patch), from DllMain, after checking the bytes; [Display] FrameRate's own patch of the offline 1/60
// shares those bytes and accepts this one. Gameplay-agreement rule 29 (TickRate) carries the rate in effect: a 60 PC
// and a 30 PC refuse each other by name, and a failed patch advertises 30 (it plays with 30 PCs).

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace online_tick
{
	void install(HMODULE game);
	bool requested();       // the key is in xml2-fix.ini: rule 29 is registered
	unsigned rate();        // the online frames per second in effect (30 or 60)
	std::array<std::uint32_t, 4> agreement_values();
	// `count_at_30` frames at 30 fps as the same length of time at the session's rate.
	std::size_t frames_for(std::size_t count_at_30);
}
