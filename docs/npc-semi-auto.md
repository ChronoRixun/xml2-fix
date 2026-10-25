# NPC weapon compatibility candidate

This opt-in candidate restores the original weapon brain and input gate for
every source gun primary: discrete bullet, beam and projectile weapons, weapons
with a warm-up before every shot, continuous beams (lightning, flame) and the
pistol's default-handler primary. Normal builder output does not enable it. See
[the investigation](weapon-brain-investigation.md) for the holder audit and the
failed earlier candidates, and [round 8](npc-weapon-round8.md) for the static
reading behind the warm-up, continuous, pistol, line-of-sight and empty-query
paths.

The original weapon brain is selected independently of ordinary AI class. The
candidate uses the PC's corresponding ranged constructor and restores source
range and leader combo level. Target validation, navigation, repositioning,
the near-target query with its held-fire branch and the close-combat paths
remain native.

## Brain

- Held fire. When the near-target query (PC `0x50a250`, original `0x103850`)
  finds its entity, the native PC held-fire branch (`0x51a284..0x51a356`, the
  original `0xef8df..0xef981`) faces it, holds power plus fire (`0x90`) and aims
  at it. This is what the owner's original officer runs in every S2 window
  ([round 6](npc-weapon-round6.md)) and what the PC ran in every measured
  round 7 window.
- Empty near-target query (`0x51a359`). The candidate runs the original's
  branches: bullet weapons `0xef9bc` (fire held while the burst timer is
  non-negative, 1.0 s pause, rand + 2.0 s bursts, aim smoothing into the actor
  aim point, the rapid-fire pause pull, the ground-plane turn) and other types
  `0xefc4a` (strafe flag, turning, a continuous beam's 0.015 rad sweep, the
  burst/pause cycle with the source burst time and the warm-up interplay).
  Revisions 9 and 10 had placed the held-fire branch here.
- Bullet line of sight. At the top of every update (`0x519e3a`, after the
  target resolve) a bullet weapon whose target is not visible resets its burst
  timers and drops the AI to state 0 (`0xef3cd..0xef434`), using XML2's own
  visibility test `0x505250` (same contract as the original `0xfe550`; XML2
  measures from bones with a 15-unit box where the original used +60 eye height
  and a 1-unit box; both treat 600 units and beyond as visible).
- Power press ends the update. The original power decision (`0xee260`) returns
  true after pressing a power, so the brain stops for that update and the press
  reaches the actor alone. XML2's decision (`0x518770`) presses the same bits but
  returns false; a bridge after the call (`0x519fe7`) treats the press as the
  original's true result.
- No input-lock exit. XML2's attack-state update (`0x512ed0`) returns before the
  brain while the actor is input locked in a flagged move (`0x512f01..0x512f14`);
  the original (`0x10ad70`) has no such exit.

## Input gate (`0x42f60`)

With power plus fire held and the actor not input locked: outside the firing
animations it requests `idle` and arms the recoil deadline 0.25 s ahead
(`0x3ca9e0`; revisions 9/10 used 0.05 s). Otherwise, once the recoil deadline
(armed by the semi-auto entry `0xe14d0` at entry + recoil) has passed, a weapon
with a warm-up starts it once (deadline `actor+0x49c`) and waits for it, then
`power_attack` is requested with play flags 1 for a continuous beam (the
warm-up stays started until fire is released) or 9 for a discrete weapon (the
warm-up is cleared, so every shot warms up again). Releasing fire clears the
warm-up. The original also plays the weapon's charge effect at the warm-up
start (actor vfunc `+0x84`); the PC conversion authors that effect inside the
primary at the shot, so only the timing is restored. The pistol's primary has
no semi-auto entry: only the gate's idle transition arms its deadline, so the
gate requests the primary on every update in which it may; whether the move
then restarts or plays out to its 0.5 s shot is decided by the native request
and the move's own input lock (`lockangles`), which still needs measuring.

The semi-auto chain callback keeps the move while fire is held. Converted gun
moves (and the pistol's converted primary) are excluded from the generic power
lottery; ordinary powers, grenades and the flame sweep power retain their
existing path. Refresh uses the already resolved action table to remove the
converter-added gun capabilities and rebuild the aggregate mask.
Source-authored gun AI metadata is excluded by the exporter.

## Opt-in contract

The companion builder reads the owner's NPC, weapon and style trees and exports
schema 3. `[Game] NpcSemiAuto=xml1` requires `NpcPowerRule=xml1`.
`[NpcSemiAuto] Count` and records `W1` through `W64` carry:

`actor|recoil_bits|range_bits|burst_bits|leader|warmup_bits|flags`

Each word has eight lowercase hex digits. Recoil, range, burst and warm-up
encode finite IEEE-754 binary32 values; leader is integer zero or one. Flags:
bits 0-1 the original prototype's type (bullet 0, flame 1, projectile 2, beam 3),
`0x10` continuous beam, `0x20` rapid fire, `0x40` default-handler primary; other
bits are refused. Names are canonicalized; recoil is positive and at most 60
seconds (zero only for a continuous beam), burst positive and at most 60,
warm-up zero to 60, range an integer above 75 and at most 65535; a
default-handler row has no warm-up and no continuous beam. Schema 2 rows are
refused. Omitted source burst duration is 1 second. The source leader flag
selects the original top combo row (PC row 11); non-leaders use row 2.

Current source eligibility selects all twenty source weapon NPCs; the eight
melee-weapon holders are exclusions. The DLL contains no source actor list or
per-weapon balance table.

## Agreement and installation

This changes existing NPC-combat rule **16**: it ships inside revision **10** (the same revision as the
reaction-spent combo steps of `NpcPowerRule=xml1`), as words 2 (installed) and 3 (source fingerprint); without
`NpcSemiAuto=xml1` those words are zero. The fingerprint covers names and all six
words of every row, so a peer with another table, or an older revision 10
candidate (schema 2 rows, held fire at the empty-query continuation, 0.05 s
transition), is refused by value. Earlier candidates: revision 9 was the held-fire
officer brain, revision 8 used the burst/pause branch for the officer's found
query, revision 6 was the failed generic-picker candidate, revision 7 retained
converter-added capability flags, revision 5 was an earlier withdrawn role
experiment. OFF retains the released rule revision/values and returns before any
image access or weapon patch. No new rule ID, protocol, version or save format is
assigned.

Eight sites install in one guarded transaction with readback and rollback:
handler lookup `0x4f6bf6`, constructor completion `0x504f5b`, capability/range
refresh `0x50fce8`, the movement/input call `0x4341b6`, the empty near-target
query continuation `0x51a359`, the power-decision result `0x519fe7`, the
attack-state input-lock exit `0x512f0d` and the brain's target-resolve
continuation `0x519e3a`. The input wrapper calls native movement once and
preserves the released stun guard and input hook. Bridges preserve registers,
flags (except where the native code they replace sets them) and floating-point
state. Transient recoil and warm-up state is indexed by World-token bits and
checked against actor incarnation and clock reset.

## Verification

Build `xml2_npc_semi_auto_test` for Win32 Release. Use the isolated native runner
with the owner's retail executable and original XBE paths. Tests cover retail
guards, metadata validation/fingerprints, peer revision rejection, OFF early
return, callback timing, input transitions, the brain branches and bridge
register/stack preservation. The fixture executes the owner's hash-bound original
recoil callback and input gate with synthetic dependencies, including a
frame-by-frame held-fire timeline for every weapon family at 30, 49 and 60 fps
compared bit for bit with the restored gate; the transition constant is read from
the XBE. No original code is embedded or shipped. Live camera-matched trials
remain separate acceptance gates.

## Current acceptance status

The gate families pass the executed-original comparison; the empty-query
branches and the line-of-sight drop have native fixture tests only (their
original code is not executable in isolation). The officer and MP5 port
measurements are in [round 6](npc-weapon-round6.md) and
[round 7](npc-weapon-round7.md); the acceptance runs for the other families
are listed in [round 8](npc-weapon-round8.md). Normal builder output remains
unchanged; damage parity, live co-op and the new families still need acceptance.
