#pragma once

#include <Windows.h>

// Conversations written for X-Men Legends 1's engine (the X-Men Legends 1 port) on XMen2.exe's:
//
//   [Game]
//   AutoAdvance = 1   ; a line the mod marks (a negative timeDelay) goes on by itself once its voice has played
//   ReplyVoices = 1   ; a chosen reply's voice plays out before its answer (the game cut it one frame in)
//   ReplyCursor = 1   ; a reply menu come back to keeps a highlighted reply (the game's clamp lost it)
//
// Each is on unless its key is 0, and each is written only when every byte it relies on is the
// retail build's; the research and every byte are in conversations_rules.hpp. A menu of two or
// more replies is never picked by the fix.
namespace conversations
{
	void install(HMODULE game);
}
