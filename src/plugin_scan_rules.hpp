#pragma once

// Alchemy's plugin helper (libIGCore.dll igPluginHelper, the enumeration at +0x68950) lists a plugin repository with
// FindFirstFileA("%s\*.*") and then LoadLibrary's every .dll it finds. A repository whose location is EMPTY (the engine's
// default) therefore scans "\*.*" = the root of the current drive. Any DLL there is loaded as an Alchemy plugin: on a
// game run from C:\ the Windows-update leftovers appverifUI.dll and vfcompat.dll raise two "Alchemy Report Handler:
// Library ... could not be loaded" dialogs (also with older builds). The real repositories (<game>\plugins, ...) always
// have a directory part; only the empty one is skipped.

#include <cstddef>

namespace plugin_scan_rules
{
	// True for a pattern that is nothing but a path separator or two followed by "*.*": the empty repository location
	// ("\*.*", "\\*.*", "/*.*"). A bare "*.*" (the current directory) and every pattern with a directory part are left alone.
	template <typename Char>
	inline bool empty_repository_pattern(const Char* pattern)
	{
		if (!pattern) return false;
		std::size_t at = 0;
		while (pattern[at] == Char('\\') || pattern[at] == Char('/')) ++at;
		if (at == 0 || at > 2) return false;
		return pattern[at] == Char('*') && pattern[at + 1] == Char('.') && pattern[at + 2] == Char('*') && pattern[at + 3] == Char(0);
	}
}
