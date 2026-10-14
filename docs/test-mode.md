# Opt-in player testing mode

`[TestMode] ForgeEverywhere` and `Hotkeys` default to false. Key assignments do not
enable the feature. Both switches are latched at startup through the existing
network exclusion path; feature installation additionally requires that exclusion
to have been requested. No save-format, content-version, protocol or rule-ID change
is involved.

The Forge override transforms the current Xtraction services mask only while its
menu is being built: set `forge`, clear `forge_campaign`, preserve every other bit.
Campaign membership is never written. The existing finalize-getter wrapper uses
the hook transaction for write, readback, protection restoration and rollback.

The keyboard observer is shared with the automation harness, but enabling player
testing does not enable a pipe, background keyboard acquisition or synthetic input.
It observes the game's DirectInput state, after any harness keys have been merged.
Rising edges run once, and loss of input disarms held keys until release. Actions
require a living, bound party and normal offline play: no loading, movie, menu,
popup, conversation or script control lock.

## Engine operations

| Operation | Native basis |
| --- | --- |
| Enemy defeat | `killEntity` (`0x4a8ce0`) calls entity virtual slot `+8`, `0x499740` for actors. That constructs a direct damage event and calls virtual slot `+0xb0`. The testing adapter uses the same event construction (`0x429860`) and kill fields, with the active hero's generation handle as attacker so the engine credits XP and kills. |
| Party protection | A transaction wraps the predicate at `0x496a20`. Its complete 13-byte prologue is copied into an executable bridge. Seated hero actors are protected while the session toggle is on; everyone else executes the native predicate. No actor deadline or save field is written. |
| Party return | `moveHeroesToEnt` (`0x4a2560`), queued as `runscript` through the existing guarded console path. It resets movement and velocity and places living heroes at the authored start's four offsets. |
| Floor verification | The native query interface, including the installed raised-capacity implementation. Four downward volume sweeps subtract noncolliding entities and actors. Each must report nearby floor with an upward normal before a move is queued. Failure leaves the party in place. |
| Offline exclusion | Existing host/join socket and admission hooks. A testing socket refusal replaces the next native error popup's explanation through its guarded text slot (`0x6a3344`, original `0x5e9e90`), preserving its normal lifecycle. This covers manually assembled dialogs too and avoids recursively creating another dialog inside the original popup's input poll. |

Enemy selection is deliberately conservative. Require a live, enabled `CActor`
on an enemy team; exclude hero-team actors, native invulnerability, undying and
boss-undying flags, and actual pain/death/extreme-death scripts. Ordinary death
targets and drops remain part of the native death path. The batch snapshots
generation handles once and resolves them again after callbacks; it never chases
new reinforcements. A script transition or control lock stops the batch.

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
credited native kill-event arguments. `xml2_gameplay_agreement_test` checks each
testing switch independently, socket denial after a verified agreement, the named
popup explanation, join cleanup and all-off forwarding. Existing startup, Xtraction
and hook-transaction suites cover the shared installation machinery.

Live verification still matters: use ordinary key input, observe health and actor
state, check movement after a return, and distinguish setup teleports/roster changes
from player actions. A scripted actor intentionally left alive is not a promise
that the testing key can bypass that encounter.
