#pragma once

#include <Windows.h>

// Always on, no ini key: libIGCore.dll's plugin enumeration of an EMPTY repository location ("\*.*", the drive root) is
// answered "nothing there", so a DLL in the drive root (appverifUI.dll / vfcompat.dll on C:) is never loaded as an
// Alchemy plugin. See plugin_scan_rules.hpp. The game's real plugin folders are not touched.

namespace plugin_scan
{
	// Hooks the FindFirstFileA/W import of libIGCore.dll (only). A missing module or import leaves everything stock with
	// one log line. Install after the mod loader (it hooks the same import and calls the function directly).
	void install();
	// The hook installed into `module`'s imports (the test points it at its own image); returns true when an import was hooked.
	bool install_into(HMODULE module);
	bool installed();
	unsigned skipped();
}
