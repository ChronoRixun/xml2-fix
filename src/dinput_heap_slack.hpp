#pragma once

#include <Windows.h>

// Windows' DirectInput 7 (SysWOW64\dinput.dll, seen in 10.0.26100.8115) keeps its HID device list in one
// LocalAlloc block: a 12-byte header (count, capacity, ...) and capacity 0x48-byte entries, capacity 16,
// doubled when full. After every rebuild of the list (DirectInputCreateEx, EnumDevices refreshes) it walks the
// entries and tests entry i's field +0x44 BEFORE it tests i < count. When the PC's HID count equals the
// capacity (16, 32, ...) that read lands 0x44 bytes past the end of the block; it only faults when the block
// is the last one before an uncommitted heap page - the rare "crash before the main menu" at
// dinput.dll+0x1a353 (an access violation in DirectInputCreateEx, called from libIGDisplay.dll).
// dinput8.dll has the same list with the test the right way round.
//
// The fix: the system dinput.dll's LocalAlloc / LocalReAlloc imports are pointed here, and every block it
// asks for gets `slack` more bytes, always zero. The walk's extra read then lands in our zeroed tail, reads 0
// and ends the walk the way an unused entry does. Nothing else in dinput.dll reads past what it asked for, and
// it does not ask a block's size (no LocalSize import), so the extra bytes are never seen.
namespace dinput_heap_slack
{
	constexpr SIZE_T slack = 0x100; // at least one 0x48-byte entry

	struct result
	{
		bool alloc = false;   // LocalAlloc import redirected
		bool realloc = false; // LocalReAlloc import redirected
	};

	// Redirects `system_dinput`'s LocalAlloc / LocalReAlloc imports (KERNEL32.dll or an api-ms-win heap set).
	// Call it right after LoadLibrary, before any of the module's exports. Safe to call once per module.
	result install(HMODULE system_dinput);

	// The system dinput.dll's HID list, read for the log: found only when the module's walk has the
	// read-before-count shape (that of 10.0.26100.8115; any other build reports shape false and nothing is read).
	struct hid_list_state
	{
		bool shape = false;   // the walk's code was recognized
		DWORD compare_rva = 0; // the walk's field compare (0x1a353 in 10.0.26100.8115)
		int count = -1;       // devices in the list (-1: no list yet)
		int capacity = -1;
	};
	hid_list_state hid_list(HMODULE system_dinput);

	// The replacements (they call what the imports pointed at before install, or kernel32's own).
	HLOCAL WINAPI padded_local_alloc(UINT flags, SIZE_T bytes);
	HLOCAL WINAPI padded_local_realloc(HLOCAL memory, SIZE_T bytes, UINT flags);
}
