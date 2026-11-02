# Controllers connected after startup

The gameplay input and the controller-discovery worker own separate `CInput`
objects. Gameplay's input is published at `0xA6ABFC` and scanned during setup at
`0x61BC73`. The recurring worker scans another input (`0xA6A7E4`, call at
`0x618D0A`). Seeing a late controller and enumerating its objects in that worker
therefore does not install a device in gameplay's ten-pad array. The per-frame
poll (`0x6285C0`) reads the primary array without refreshing it.

The previous bridge at `0x61862A` protects the Advanced Options accept handler
(`0x618390`, registered at `0x61EDC1`), rather than the primary input's arrival
path. Its installation line did not prove when that handler executed. Its
standalone fixture called the remapper directly and reused one input manager,
which missed the live startup/discovery split. The bridge remains useful when accepting
Options: an absent saved GUID otherwise becomes sentinel 666 and that panel
clears the corresponding actions.

## Configuration

`[Input] LatePads=1` (default, including an absent key) enables late controller
arrival routing and Advanced Options binding preservation. Set `LatePads=0`
to retain the behavior from before the late-pad fix: no bridge at `0x61862A`,
no primary-input arrival refresh, binding/state/metadata writes or topology
updates from this feature. Restart the game after changing the key. The normal
DirectInput/XInput hooks and the independent `CacheDeviceScan` setting continue
to work. This uses the usual INI switch syntax (`1`/`0`, `true`/`false`,
`yes`/`no`, `on`/`off`); empty or invalid values use the default.

## Arrival routing

`gamepad_fix` calls `pad_hotplug::on_keyboard_read` before its original
`GetDeviceState`. Only a 256-byte read of the actual primary input's keyboard
can drive the refresh. At most once per 500 ms, on that input's own polling
thread, it invokes the guarded retail refresh `0x628E20`. With
`CacheDeviceScan=1`, its existing background cache key stays active and the
recurring enumeration replays published snapshots. No new worker mutates the
game's input. With caching disabled, this refresh uses real enumeration.

Startup captures each player's saved pad GUID role and current live bindings.
After the refresh, roles are matched to the GUIDs and device indices the primary
input actually received. Known roles are reserved first; a newly arrived,
unclaimed GUID can fill a missing role, including a changed Bluetooth instance.
Bindings in all five rows retain their control IDs and keyboard entries, while
pad device numbers follow the actual post-scan indices. The game's `0x6295A0`
helper rebuilds each map's derived first-pad device at `+0x964`. Unused saved
numbers stay unchanged until arrival; a missing role moves to a vacant number
when compaction put another controller at its previous index. Explicitly
unbound controls remain unbound. Maps have a fixed capacity of 50 actions;
malformed counts are refused before traversal or mutation.

Retail compaction clears a moved pad's current/previous state. The refresh
restores those buffers by GUID for continuously attached devices and retains
their cached action values, preventing an already-held button from becoming a
new press when another pad is removed. Missing or replaced roles clear their
cached actions even if their reserved device number stays the same.

The system mouse and keyboard seen in DirectInput 7's all-device enumeration
are not controller ordinals. The retail controller query requests class 4 and
keeps keyboard/mouse pointers separately. Binding device `3 + index` refers to
the actual ten-pad array index, not the position in an all-device list.

XInput synthesis retains a physical GUID's actual XInput user index while that
index remains connected. It does not re-index a surviving pad through the
shrinking list of connected slots. Primary topology releases absent GUID leases;
a disconnected XInput index can also be reassigned when a rapid reconnect was
not visible in a DirectInput snapshot. Before validated gameplay topology is
available, source selection retains the previous ordinal behavior. Logitech
Dual Action product/object/state layout and the foreground-only input policy
are preserved. No registry write or gameplay agreement rule is added.

## Diagnostics

The log distinguishes installation from execution:

- `late controller assignment installed ... waiting for startup map` means the
  supported hooks are ready, not that a player has been assigned.
- `main input ... keyboard ... discovery input ...` identifies both managers.
- `startup map player ... saved slot ... GUID ... binding device ... active
  device ...` records the loaded player role and whether its device is missing.
- `arrival bound player ... actual device index ... binding device ... (was
  ...) ... bindings updated in 5 rows` proves the actual arrival assignment.
- Stable XInput-index assignment, successful state reads, focus idling and
  focus recovery identify the remaining input gates without changing focus.

Raw Input topology is checked every 250 ms and XInput connections once per
second. Together with the 500 ms gameplay refresh, a normal driver-reported
arrival is expected to become usable within about one or two seconds. Hardware
latency remains a live check; topology changes invisible to Raw Input still
use the existing 30-second safety refresh.

## Offline verification and live acceptance

`xml2_pad_hotplug_live_test <owned-retail-exe>` reproduces startup with exactly
DirectInput 7's system mouse `2B60:6F1D` (type `0x102`) and keyboard
`2B61:6F1D` (type `0x403`), distinct discovery/gameplay inputs, an absent saved
Xbox GUID and a late Bluetooth `045E:0B22` controller. The old implementation
fails because gameplay's device array stays empty although discovery reads
XInput. The fixture executes retail scan/read code and production XInput to
Logitech synthesis, checking all five live maps, stale active-device metadata,
startup presence, a late second pad, two-pad removal/re-addition, changed GUIDs,
sparse saved slots, focus loss/recovery and source-slot reconnect behavior.
Held-button continuity and cached-action release are checked during compaction.
`xml2_pad_hotplug_test` retains the Options remap/guard fixtures; the separate
scan-cache suite covers asynchronous publication and enumeration replay.

Owen's live acceptance: Bluetooth off -> start -> Bluetooth on. Verify player 1
within one or two seconds, including movement, both sticks, buttons and Advanced
Options. Repeat with a pad present at launch, removal/re-addition, and a second
pad arriving later. If input remains idle, retain the startup map, arrival
binding, XInput source and focus lines together. Native tests do not replace
that hardware check.
