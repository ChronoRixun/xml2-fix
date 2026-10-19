#pragma once

#include <Windows.h>

// [Debug] LoadLibraryTrace=1 (default off; no gameplay rule, not part of the agreement record): logs every
// LoadLibraryA/W/ExA/ExW call the game and its engine DLLs make - the requested name, the result (module handle or
// GetLastError) and the calling module + offset - plus the directory searches (FindFirstFile*) of the engine's plugin
// loader (igWin32LibraryLoader::load in libIGCore.dll). Diagnosis tool for "Alchemy Report Handler: Library
// <name> could not be loaded" at start-up (Application Verifier components on Windows 11 build 26200.9711).
// Nothing is patched or changed: the hooks forward to the real function and restore GetLastError.

namespace load_trace
{
	void install(HMODULE game);
	bool active();
}
