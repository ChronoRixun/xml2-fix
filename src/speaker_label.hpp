#pragma once
#include <Windows.h>
#include <string>

// [Game] ClassicSpeakers = 1 (display only): a conversation line that carries the attribute
// speakerLabel="<name>" shows <name> as its speaker label, while its speaker key - the portrait head and the
// entity that gets the talk animation - stays the line's own %TOKEN%. XMen2.exe takes all three from the one
// token (the line parser 0x458820 -> 0x456d00 replaces a leading %NAME% with the stats entry's display name and
// "\n"); X-Men Legends 1 labelled its %ALISON% lines "Alison" (default.xbe string 503) while keying them to Magma,
// so the Legends Classic builder keeps %MAGMA% on those lines and writes speakerLabel="Alison" beside it.
namespace speaker_label
{
	void install(HMODULE game);

	// The rule, pure: `parsed` is the line's text after the game's own speaker pass, `raw` the attribute it came
	// from, `label` the speakerLabel value. True (and `out` set) only when the raw text starts with a %TOKEN% the
	// game resolved - `parsed` then begins with the display name and "\n", which is replaced by `label` and "\n".
	bool relabel(const char* parsed, const char* raw, const char* label, std::string& out);
}
