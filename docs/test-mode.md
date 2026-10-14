# Opt-in player testing mode

`[TestMode] ForgeEverywhere` and `Hotkeys` default to false. Key assignments do not
enable the feature. Both switches are latched at startup through the existing
network exclusion path; feature installation additionally requires that exclusion
to have been requested. No save-format, content-version, protocol or rule-ID change
is involved.

The Forge override transforms the current Xtraction services mask only while its
menu is being built: set `forge`, clear `forge_campaign`, preserve every other bit.
When `[Game] XtractionShop=0`, only Forge is appended; Load, Healer, Danger Room,
Change Team suppression, and the shared XtractionShop capability remain off.
Campaign membership is never written. The existing finalize-getter wrapper uses
the hook transaction for write, readback, protection restoration and rollback.

The keyboard observer is shared with the automation harness, but enabling player
testing does not enable a pipe, background keyboard acquisition or synthetic input.
With the pipe off, it watches function-key messages on the window supplied to the
keyboard's successful `SetCooperativeLevel` call, preserving its original procedure
and cooperative flags. This also works without a display-mode override. F10 system
key messages are handled; Alt combinations are forwarded. With the pipe on, the
existing DirectInput rising-edge observer is used instead, avoiding duplicate input.
Neither callback runs damage or movement: one pending action is consumed at the
console-frame getter call (`0x402205`). Party generation and zone are rechecked
before execution. Focus loss cancels a pending window action. Actions
require a living, bound party and normal offline play: no loading, movie, menu,
popup, conversation or script control lock.

## Engine operations

| Operation | Native basis |
| --- | --- |
| Enemy defeat | `killEntity` (`0x4a8ce0`) calls entity virtual slot `+8`, `0x499740` for actors. That constructs a direct damage event and calls virtual slot `+0xb0`. The testing adapter uses the same event construction (`0x429860`) and kill fields, with the active hero's generation handle as attacker so the engine credits XP and kills. |
| Party protection | A transaction wraps the predicate at `0x496a20`. Its complete 13-byte prologue is copied into an executable bridge. Seated hero actors are protected while the session toggle is on; everyone else executes the native predicate. No actor deadline or save field is written. |
| Party return | `moveHeroesToEnt` (`0x4a2560`), queued as `runscript` through the existing guarded console path. It resets movement and velocity and places living heroes at the authored start's four offsets. |
| Floor verification | The native query interface, including the installed raised-capacity implementation. The nearest player start to the active party hero is selected; an obstructed nearest start refuses rather than choosing a farther one. Four downward sweeps subtract noncolliding entities and actors. Each must find nearby floor at or below the actual arrival feet, then a fresh upward sweep must clear the standing body volume. The living party's collision bounds size the sweeps. Failure leaves the party in place. |
| Offline exclusion | Existing host/join socket and admission hooks. A testing socket refusal replaces the next native error popup's explanation through its guarded text slot (`0x6a3344`, original `0x5e9e90`), preserving its normal lifecycle. This covers manually assembled dialogs too and avoids recursively creating another dialog inside the original popup's input poll. An unconsumed refusal is cleared at the next console-frame boundary, after the failed callback has returned. |

Enemy selection is deliberately conservative. Require a live, enabled `CActor`
on an enemy team; exclude hero-team actors, neutral actors, enemies temporarily
inverted onto the hero team, native invulnerability, undying and boss-undying flags,
and actual pain/death/extreme-death scripts. The guarded script entity resolver
(`0x4a1700`) also remembers generation handles referenced by scripts. Such actors
are excluded even when they only have a health-polling script, with no event script
or boss-undying flag. This is intentionally conservative: any resolved script
reference protects the actor, not just a `getHealth` call. Ordinary death
targets and drops remain part of the native death path. The batch snapshots
generation handles once and resolves them again after callbacks; it never chases
new reinforcements. A script transition or control lock stops the batch.

The native team getter (`0x41a220`) prioritizes bit 29 (hero), then 30 (enemy), then
31 (alternate enemy); no team bit means neutral. Entity `+0x6c` bit 3 swaps enemy
teams to hero and hero to alternate enemy. `bossundying` is `+0x30d & 0x40`;
`undying` is `+0x30c & 0x10`. Neither is a universal scripted-boss classification.
Protection from the resolver covers references already made this session; this
cannot predict a future script reference or prove every campaign boss safe.

`moveHeroesToEnt` constructs world-axis offsets `(40,0,10)`, `(0,40,10)`,
`(-40,0,10)`, `(0,-40,10)`, adds them directly to the target origin, then copies
angles separately. The target's yaw does not rotate the offsets. The resolver uses
`atoi`, so a signed decimal string preserves a high-bit generation handle without
positive signed overflow. `0x41c320` returns actor local bounds at `+0x158/+0x164`.

Invulnerability remains a session setting. The HUD reminds the player after a zone
change or load while it is enabled.

An event alone does **not** imply a script: ordinary pain sounds create events too.
The event pool at pointer cell `0x72989c` has 310 records of 24 bytes, script token
at record `+8`, live bits at `+0x1d10`, generation handles at `+0x224c`, and mask at
`+0x2724`. Unknown/stale event records exclude the actor. Actor event handles are
at `+0x29c` (pain), `+0x2a0` (death) and `+0x2ac` (extreme death). Enabled is property
20, confirmed by `setEnable` through `0x474d40`; property 24 is not the actor enable
flag. The retail default keyboard table at `0x61b030` uses F1 and F11, leaving the
chosen F8/F9/F10 defaults available.

## Noclip investigation

`setNoClip` (`0x4a2a40`) changes entity property 0; `setNoCollide` (`0x4a3300`)
changes property 1. Both refresh collision state through virtual slot `+0x48`.
Neither supplies a free-flight controller, a floor-safe exit, or a party recovery
contract. They are not exposed as player toggles. The floor-checked return to an
authored player start is the supported recovery action.

## Regression checks

`xml2_test_mode_test <private XMen2.exe>` checks synthetic enemy/event selection,
held keys and focus loss, all service-mask combinations, each production install
guard, the real predicate bridge, preservation of scripted protection, and the
credited native kill-event arguments. It also executes the installed console-frame
hook, the original numeric lookup through its bridge, a hidden window's message
observer, fresh collision queries, nearest-start selection, ceiling refusals,
script-polled actor protection, and the actual Forge-only service wrapper. `xml2_gameplay_agreement_test` checks each
testing switch independently, socket denial after a verified agreement, the named
popup explanation, join cleanup and all-off forwarding. Existing startup, Xtraction
and hook-transaction suites cover the shared installation machinery.

Live verification still matters: use ordinary key input, observe health and actor
state, check movement after a return, and distinguish setup teleports/roster changes
from player actions. A scripted actor intentionally left alive is not a promise
that the testing key can bypass that encounter.
