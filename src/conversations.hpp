#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

// Conversations written for X-Men Legends 1's engine (the X-Men Legends 1 port) on XMen2.exe's:
//
//   [Game]
//   AutoAdvance = 1   ; a line the mod marks (a negative timeDelay) goes on by itself once its voice has played
//   ReplyVoices = 1   ; a chosen reply's voice plays out before its answer (the game cut it one frame in)
//   ReplyCursor = 1   ; a reply menu come back to keeps a highlighted reply (the game's clamp lost it)
//   OnlineConversations = lockstep ; online: a marked line goes on |timeDelay| s of game time after it was drawn, on
//                       ; every PC on the same frame, and a reply's voice is cut (gameplay agreement rule 27)
//
// Each is on unless its key is 0, and each is written only when every byte it relies on is the
// retail build's; the research and every byte are in conversations_rules.hpp. A menu of two or
// more replies is never picked by the fix.
namespace conversations
{
	void install(HMODULE game);
	// [Game] OnlineConversations set (lockstep, or an invalid value): rule 27 registers.
	bool online_requested();
	bool online_verified();
	std::array<std::uint32_t, 4> online_values();
}
