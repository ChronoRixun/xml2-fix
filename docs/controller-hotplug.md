# Controllers connected after startup

The retail settings loader compares saved `Controls\Gamepads\Gamepad1` through
`Gamepad4` instance GUIDs with the first four enumerated devices. An absent saved
GUID becomes the internal value 666 (`0x6185CE`). Its action remapper then clears
every binding referring to that saved pad (`0x61877A` through `0x6187E4`). Turning
the pad on later still creates and reads a DirectInput device, but the actions
are already unbound, including in Advanced Options. Layout migration version 2
does not run again simply because a pad arrives. `ListChanged` reports a changed
list; it does not restore those actions.

`pad_hotplug` installs a guarded bridge at `0x61862A`, after GUID matching and
before action remapping. Known GUID matches retain their current device numbers.
Missing, new and empty entries take distinct unused numbers from 0 through 3.
The original remapper therefore preserves each control and assigns its device
number without deleting bindings for an absent pad. For an empty startup list,
the first arriving pad uses binding device 3 (player 1's default), the second
device 4, and so on. If the saved second pad is already present at device 3, its
match is reserved and the absent first pad gets device 4 for its later arrival.
An unmatched current instance can also fill an unmatched saved slot, covering a
changed Bluetooth instance GUID.

This changes the settings loader's in-memory map. The game's normal settings
save remains responsible for persistence. Physical GUIDs, saved custom controls,
the Logitech Dual Action presentation, XInput state, acquisition and scan timing
retain their existing paths. It adds no gameplay agreement rule. Runtime removal
and list compaction remain the retail behavior; controller identity is not pinned
to a player across a runtime list compaction.

The controller scan worker checks Raw Input topology every 250 ms; the retail
worker requests scans about every 0.7 s; XInput connection discovery runs once
per second. A normal arrival should become usable in about one or two seconds,
plus driver latency. Devices whose topology change is invisible to Raw Input
still rely on the existing 30-second safety refresh.

`xml2_pad_hotplug_test <owned-retail-exe>` executes the retail GUID mapping,
42-action remap, recurring device scan and ten-pad frame read loop with fake COM
devices. It reproduces the stock erased-binding failure before installing the
production bridge. Cases cover startup presence, late first and later pads,
removal/re-addition, reversed GUID order, a known second pad with the first
absent, a changed instance GUID, empty saved settings, every guard-byte refusal,
and exhaustive mapping combinations. No game session or hardware input is used.

Live acceptance for Owen: Bluetooth off, start the game, then Bluetooth on. After
one or two seconds, check movement, buttons, both sticks and Advanced Options.
Repeat with a pad present at launch, disconnect/reconnect, and a second pad
connected later. Check that each pad controls only its intended player.
