# NPC weapon compatibility candidate

This opt-in candidate restores the officer's original weapon behaviour on the
discrete non-bullet path without warm-up. It is not a general restoration of
every original weapon. Normal builder output does not enable it. See
[the investigation](weapon-brain-investigation.md) for the holder audit and the
failed earlier candidates.

The original weapon brain is selected independently of ordinary AI class. The
candidate uses the PC's corresponding ranged constructor and restores source
range and leader combo level. Target validation, navigation, repositioning and
close-combat paths remain native.

Revision 9 follows what the owner's original officer actually executes (S2 ring
of the original at 120/300/520 units, EP 0 and full; see
[the round-6 notes](npc-weapon-round6.md)):

- Held fire. The original never runs the burst/pause branch at `0xefc4a`; its
  burst timers stay at the constructor values for the whole engagement, and power
  plus fire (`0x90`) are held on every weapon-brain update. At the ranged brain's
  unresolved near-target continuation (`0x51a359`) the candidate runs the
  original's held-fire branch (`0xef8df..0xef981`): face the destination, hold
  `0x90`, aim at it, using the native PC helpers of the same branch. Cadence then
  comes only from the recoil gate below.
- Power press ends the update. The original power decision (`0xee260`) returns
  true after pressing a power, so the brain stops for that update and the press
  reaches the actor alone. XML2's decision (`0x518770`) presses the same bits but
  returns false; a bridge after the call (`0x519fe7`) treats the press as the
  original's true result. Without it the full-energy grenade press became a gun
  shot or a heavy melee attack.
- No input-lock exit. XML2's attack-state update (`0x512ed0`) returns before the
  brain while the actor is input locked in a flagged move (`0x512f01..0x512f14`);
  the original (`0x10ad70`) has no such exit. With it skipped, the brain keeps fire
  held during the grenade move, the input gate keeps requesting `idle` against it
  and the grenade move is held, as on the original (zero shots at full energy).

The actor input gate chooses `power_attack` against an entry-owned recoil clock;
the semi-auto chain callback keeps the move while fire is held. Converted gun
moves are excluded from the generic power lottery; ordinary powers and grenades
retain their existing path. Refresh uses the already resolved action table to
remove the converter-added gun capabilities and rebuild the aggregate mask.
Source-authored gun AI metadata is excluded by the exporter. Explicit scripted
bursts keep their authored events. The burst duration stays in the contract and
fingerprint but the officer's restored branch does not use it.

The restored branch retains the original requirement that the NPC pass the
camera view predicate. An off-screen period is not evidence of a recoil denial.

## Opt-in contract

The companion builder reads the owner's NPC, weapon and style trees and exports
schema 2. `[Game] NpcSemiAuto=xml1` requires `NpcPowerRule=xml1`.
`[NpcSemiAuto] Count` and records `W1` through `W64` carry:

`actor|recoil_bits|range_bits|burst_bits|leader`

Each word has eight lowercase hex digits. The first three encode finite IEEE-754
binary32 values. The final word is integer zero or one. Names are canonicalized;
recoil and burst must be positive and at most 60 seconds, range must be an integer
above 75 and at most 65535. Omitted source burst duration is 1 second. The source
leader flag selects the original top combo row; the existing NPC rule represents
that with PC row 11. Non-leaders use row 2.

Current source eligibility selects the officer. Bullet aiming, warm-up,
continuous firing and the pistol's different handler remain explicit exclusions.
The DLL contains no source actor list or per-weapon balance table.

## Agreement and installation

This changes existing NPC-combat rule **16**: it ships inside revision **10** (the same revision as the
reaction-spent combo steps of `NpcPowerRule=xml1`), as words 2 (installed) and 3 (source fingerprint); without
`NpcSemiAuto=xml1` those words are zero. It was developed as candidate revision 9; revision 8 used the
burst/pause branch the original officer never runs; revision 6 was the
failed generic-picker candidate; revision 7 retained converter-added capability
flags; revision 5 was an earlier withdrawn role experiment. The fingerprint
covers names, recoil, range, burst and leader flag.
OFF retains the released rule revision/values and returns before any image access
or weapon patch. No new rule ID, protocol, version or save format is assigned.

Seven sites install in one guarded transaction with readback and rollback:
handler lookup `0x4f6bf6`, constructor completion `0x504f5b`, capability/range
refresh `0x50fce8`, the movement/input call `0x4341b6`, the unresolved
near-target continuation `0x51a359`, the power-decision result `0x519fe7` and the
attack-state input-lock exit `0x512f0d`. The input wrapper calls native movement
once and preserves the released stun guard and input hook. Bridges preserve
registers, flags (except where the native code they replace sets them) and
floating-point state. Transient recoil state is indexed by World-token bits and
checked against actor incarnation and clock reset.

## Verification

Build `xml2_npc_semi_auto_test` for Win32 Release. Use the isolated native runner
with the owner's retail executable and original XBE paths. Tests cover retail
guards, metadata validation/fingerprints, peer revision rejection, OFF early
return, callback timing, input transitions and bridge register/stack preservation.
The fixture also executes the owner's hash-bound original recoil callback and
input gate with synthetic dependencies. No original code is embedded or shipped.
Live camera-matched ranged/melee trials remain separate acceptance gates.

## Current acceptance status

Revision 9 passes the native fixture suite (held-fire order and inputs, burst
timers untouched, power-press policy, bridge register/FPU preservation for the
new bridges, retail guards). The port-only measurement against the S2 original
table is in [the round-6 notes](npc-weapon-round6.md). Normal builder output
remains unchanged; damage parity, live co-op and other weapon families still need
acceptance.
