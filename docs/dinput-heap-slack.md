# DirectInput 7 heap slack (the rare crash before the main menu)

## Symptom

About 0.2 s after start, before the window shows the movies or the main menu, the game ends with an access
violation in Windows' own `SysWOW64\dinput.dll` (seen in 10.0.26100.8115, offset `0x1a353`), reached from
`DirectInputCreateEx`, which the engine's `libIGDisplay.dll` calls through the fix's proxy. The fix's log ends with
`crash: exception 0xc0000005 at ... (dinput.dll+0x1a353)` and `edi 00000020` (or `00000010`). It happens on a small
share of boots, only on PCs where the condition below holds, and never later in play.

## Cause

DirectInput 7 keeps its list of HID devices in one `LocalAlloc` block: a 12-byte header (count, capacity, ...)
followed by 0x48-byte entries, capacity 16 at first and doubled when full. After each rebuild of the list it walks
the entries, and the walk tests an entry's field at +0x44 *before* it tests the index against the count. When the
number of HID collections DirectInput opens equals the capacity (16 or 32; a PC with gaming mice, keyboards and
RGB devices easily has 30+), the walk reads 4 bytes 0x44 past the end of the block. Usually that lands in other heap memory and
nothing happens; when the block is the last one before an uncommitted heap page, the read faults. That is why the
crash is rare and depends on the PC's devices (connecting or disconnecting a controller changes the count). `dinput8.dll` builds the same list but tests the count first.

## Fix

`dinput_heap_slack::install` runs right after the proxy loads the system `dinput.dll`, before any of its exports:
it points that module's `LocalAlloc` and `LocalReAlloc` imports at wrappers that ask for 256 more bytes and zero
them. The walk's extra read then lands in zeroed memory, reads 0 and ends the walk the way an unused entry does.
The module never asks a block's size (no `LocalSize` import), so the extra bytes are invisible to it. Only the
system `dinput.dll` loaded by the proxy is affected; no other module's allocations change.

The log shows `dinput heap slack: on (LocalAlloc and LocalReAlloc imports)` at load and, once after the first
`DirectInputCreateEx`, `dinput heap slack: HID list N of M`. "full" there means this PC is one that would have
crashed now and then without the fix. On a `dinput.dll` whose walk is not the known one, the count is not read
(the padding is still applied).

## Test

`xml2_dinput_heap_slack_test` (in `scripts/run_native.py`):

1. the wrappers: slack present and zero with and without `LMEM_ZEROINIT`, data kept on grow and shrink, the
   caller's last error kept, moveable blocks left unlocked, `LMEM_MODIFY` passed through;
2. a walk written like the system one over lists grown like the system one, each block placed against a
   no-access page: unpadded it faults exactly at 16, 32 and 64 devices; padded it never faults (1-64 devices);
3. the PC's own `dinput.dll` in child processes under the same page-end placement, with the HID interface
   enumeration cut so the list is exactly full (16 and 32): unhooked it faults at the walk's compare every time,
   hooked it succeeds and reports the full list. The PC's natural count is reported as well.
