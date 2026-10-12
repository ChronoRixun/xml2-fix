# Confirmation release

`[Game] ConfirmationRelease=1` prevents a held confirmation from answering a
second popup after the first popup closes. The default is `0`: this is an explicit
compatibility feature pending wider retail XML2 and network/co-op acceptance.
It uses the common INI boolean syntax. An absent or invalid value leaves it off.

An entrance popup can reopen immediately after its second, empty-action option.
That reopening is also observed in the original engine. The fix preserves the
trigger, popup default, empty action and immediate replacement. The consumed
physical binding cannot answer the replacement until released; a fresh press can.
There is no timer, trigger cooldown, occupancy test or change to authored data.

## Hook contract

All addresses below are for the supported retail executable at image base
`0x400000`. Every installation dependency has a byte guard. Any mismatch or
failure to make a patch site writable refuses the complete installation and logs
the reason. All sites are made writable before any instruction is changed.

| Site | Purpose |
| --- | --- |
| `0x62887d` | Sample freshly polled keyboard, mouse and pad state before the game's modal input suppression. Replay the two displaced instructions. |
| `0x5eb7c6` | Check an acceptance already requested by the native mask checks. On rejection return to the native cancellation checks at `0x5eb677`, before sound, action selection or popup closure. |
| `0x5eb8ab` | Replay the native popup-visible query and consume contributing sources only on the path that closes a visible accepted popup. Cancellation has no pending sources to consume. |
| `0x628621`, `0x628642`, `0x6286b7` | Observe the primary keyboard read, its retry, and the mouse read results, preserving their original instructions and results. Failed reads cannot masquerade as release. Pad validity uses the native successful-read mask. |

The latch belongs to the DLL's game-thread state, independent of popup object,
slot and binding row. A source is the physical `(device, control)` pair; changing
the popup, owner, row or translated action does not count as release. Raw input
polling releases each consumed source independently. A second player's fresh
confirmation remains available while the first player's button is held.
Failed device reads retain the latch, including when the game clears a failed
read's buffer. A successful released sample rearms it. Removal of a device is
treated as release. Read-result bookkeeping resets on each poll.

Candidate sources come from action 4's current binding records, after applying
the game's logical-player remapping, owner, native player-1 fallback and shared
popup participant/remote-player gates. The game still decides whether an accept
action exists. Only sources contributing to that action are consumed. Simultaneous
fresh sources answer the same popup and are all consumed together.

Keyboard scancodes, the three supported mouse button bindings, and pad buttons,
axes and POV directions are covered. Analog sources remain consumed until their
physical direction is neutral; this intentionally requires a release, not merely
a transient loss of the translated action. Wheel/mouse-axis confirmation bindings
are unsupported. If a runtime object, source or binding cannot be identified,
that acceptance stays native and a once-per-process refusal is logged.

Conversation response menus and other consumers outside the popup manager
are not patched. Xtraction dialogs and tutorials using the popup manager pass
through the same release rule. This feature adds no save fields; the latch starts empty in a new
process and releases through input polling while a game loads.

## Verification

Build for MSVC Win32, then run `xml2_confirmation_release_test.exe`. It uses
synthetic input and images to check independent release, replacement across long
holds, simultaneous sources, owner/fallback/participant gates, supported control
bounds, INI decisions, branch encoding and refusal on every changed guard byte.
Failed-read tests also cover zeroed buffers, recovery and independent sources.
An optional executable path also compares all guards with a local retail PE
mapped only as data: `xml2_confirmation_release_test.exe <XMen2.exe>`.

In a private windowed test build, enable the feature and select an entrance
popup's empty second option. Hold keyboard Enter or pad A for 80, 500 and 1500 ms
and a longer interval. Verify the exterior and living party remain after ten
seconds without input, then release and press again: the replacement's first
option must enter. Use live popup selection/action reads and native acceptance
traces in addition to screenshots. Test Yes, fresh No, cancellation, tutorial
tips, conversation responses and Xtraction menus. Controller tests must export
and restore the shared gamepad registry list.

The default stays off because the hook deliberately changes repeated-held
acceptance for every popup-manager user, and wider retail and online/co-op
behavior has not been established by these local rules tests.
