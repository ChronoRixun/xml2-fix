# Controller scan cache

`[Input] CacheDeviceScan=1` (default, also when absent) serves controller
enumerations from background snapshots and moves XInput slot discovery off the
frame thread. `CacheDeviceScan=0` restores the previous DirectInput enumeration
and XInput discovery paths. Both games use the same hooks; this changes no
gameplay agreement, device GUID, binding or controller layout.

The retail zone timer (`0x6256E0`) requests a controller scan after seven 0.1 s
ticks. Worker `0x618C90` calls `0x628E20`, whose DirectInput enumeration is at
`0x628E54`. The Controls menu can also enumerate synchronously (`0x626AA0` to
`0x62547C`). The hooked DirectInput 7 joystick enumerations cover libIGDisplay's
controller manager as well.

With a known attached pad, callback `0x628B40` reuses its existing device at
`0x628C2C`, then skips creation/setup at `0x628CE4`. The per-frame path still
calls Poll, optionally Acquire, and GetDeviceState (`0x628805` to `0x628832`).
The scan flags have no frame-thread wait. Thus slow enumeration is established,
but an internal game lock causing a visual hitch is **not confirmed**. Driver
contention or presentation effects remain possible. Live frame correlation and
an ON/OFF visual comparison are required to certify the visible fix.

## DirectInput

A separate worker copies each complete raw device list, preserving its order,
callback structure size, bytes and HRESULT. Keys include the DirectInput object,
interface version, A/W layout, type and flags. Raw Input handle/type/name sets
are compared every 250 ms; a changed set triggers an active query's real scan.
Active lists are also scanned every 30 seconds. Raw Input errors retry scanning
at most once per second. HRESULT failures publish no partial list and retry
after one second. Changes invisible to Raw Input rely on the 30-second refresh.

Only snapshot publication holds a short mutex. Driver calls and game callbacks
run outside it. A cold query waits for its first background publication to keep
DirectInput's complete-first-list contract; recurring queries never wait for a scan. Each callback is replayed on its caller's thread through the
existing spoof and virtual-pad code; DIENUM_STOP stops replay, without truncating
the stored list. The first physical query
returns a complete list after its background scan, with the same startup wait
as the original path. This preserves main-menu input and libIGDisplay's one-shot
controller initialization. The virtual-pad append follows that first list too. Controls-menu queries can return
the previous list while a refresh is in progress. Hardware discovery is normally
visible within about one scan cycle, plus driver latency.

The cache retains a COM reference while a query is active, preventing use after
Release. A query unused for 60 seconds is retired. One-shot engine/menu queries
are not continuously scanned. The worker pins the compatibility DLL for process
lifetime, so DLL unloading cannot destroy code during an enumeration. A worker
setup failure retains the original enumeration path.

## XInput

The existing loading thread stays alive to probe all four slots once a second.
A failed known-slot read clears its published bit and signals immediate refresh.
The frame path copies an atomic connection mask and reads only its selected slot.
Publication cannot resurrect a slot that failed during discovery. With the key
off, the four-slot per-read discovery and virtual pads' once-per-second discovery
remain as before. Worker setup failures use that same fallback.

## Offline verification

Build the Win32 targets `xml2_controller_scan_cache_test` and
`xml2_xinput_connections_test`. The first exercises the production cache and
replay hook with fake COM devices, covering A/W, versions 3/5/7/8, DX3/current
sizes, exact bytes, order, early stop, hot-plug, failures, refresh, retained COM
lifetime and a blocked scan. The second exercises production XInput discovery,
slot reads, failure signalling, publication races and OFF call counts.
`xml2_xinput_connections_test --timing-real` also measures actual XInput calls
without launching a game or injecting input.

Live acceptance: with a physical DirectInput or XInput pad connected, enter Team
Management and watch the rotating clouds for 60 seconds. Compare identical
sessions with `CacheDeviceScan=0` and `1` (restart between settings). On should
remove the once-per-second pause. Also disconnect/reconnect the pad during play
and in Controls; verify detection, button mapping, both sticks and local co-op
slot order. Observe startup and at least 35 seconds of play to cover the safety
refresh. Native tests do not certify that visual/hardware result.

Owner live check (2026-10-26, beta 8 candidate 080i, a physical pad): no once-a-second pause with the pad
connected, and unplugging and replugging it is picked up. Starting with the pad off and switching it
on later is still being checked.

The separate [late-controller arrival repair](controller-hotplug.md) refreshes
the primary gameplay input and rebinds players to actual post-scan device indices.
A device discovered by the worker belongs to a different input object; discovery
alone does not add it to gameplay. The original Options-only remap fix did not
address this split, as Owen's 2026-10-26 live check confirmed.
