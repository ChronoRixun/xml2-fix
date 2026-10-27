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
  timers and drops the AI to state 0 (`0xef3cd..0xef434`), calling XMen2.exe's
  visibility entry `0x505250` (same contract as the original `0xfe550`: false plus
  an untouched flag = blocked). This opt-in requires `NpcPowerRule=xml1`, whose
  item 8 redirects that entry to its XML1 visibility adapter, so the test is XML1's
  geometry (+60 eye height, a 1-unit box, XML1's query reservation and budget),
  not XML2's bone-based 15-unit box; both treat 600 units and beyond as visible.
  The weapon brain guards the 10 retail bytes after the redirected entry
  (`0x505256`).
- Power press ends the update. The original power decision (`0xee260`) returns
  true after pressing a power, so the brain stops for that update and the press
  reaches the actor alone. XML2's decision (`0x518770`) presses the same bits but
  returns false; a bridge after the call (`0x519fe7`) treats the press as the
  original's true result.
- No input-lock exit. XML2's attack-state update (`0x512ed0`) returns before the
  brain while the actor is input locked in a flagged move (`0x512f01..0x512f14`);
  the original (`0x10ad70`) has no such exit. With it skipped, the brain keeps fire
  held during the grenade move, the input gate keeps requesting `idle` against it
  and the grenade move is held, as on the original (zero shots at full energy).
  `NpcPowerRule=xml1` (which this opt-in requires) now removes that exit for every
  actor (rule 16 revision 11 item 16, [npc-power-rule.md](npc-power-rule.md)), so
  the separate bridge for listed weapon NPCs at `0x512f0d` is no longer installed.

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
and the move's own chaining lock (`lockchaining`), which still needs measuring
live (no native test executes the move request or its firing event: see "Live
checks").

The semi-auto chain callback keeps the move while fire is held. Converted gun
moves (and the pistol's converted primary) are excluded from the generic power
lottery; ordinary powers, grenades and the flame sweep power retain their
existing path. Refresh uses the already resolved action table to remove the
converter-added gun capabilities and rebuild the aggregate mask.
Source-authored gun AI metadata is excluded by the exporter.

## Opt-in contract

The companion builder reads the owner's NPC, weapon and style trees and exports
schema 4. `[Game] NpcSemiAuto=xml1` requires `NpcPowerRule=xml1`.
`[NpcSemiAuto] Count` and records `W1` through `W64` carry:

`actor|recoil_bits|range_bits|burst_bits|leader|warmup_bits|flags|tracetime_bits|accuracy_bits`

Each word has eight lowercase hex digits. Recoil, range, burst, warm-up, trace time and accuracy
encode finite IEEE-754 binary32 values; leader is integer zero or one. Flags:
bits 0-1 the original prototype's type (bullet 0, flame 1, projectile 2, beam 3),
`0x10` continuous beam, `0x20` rapid fire, `0x40` default-handler primary; other
bits are refused. Names are canonicalized; recoil is positive and at most 60
seconds (zero only for a continuous beam), burst positive and at most 60,
warm-up zero to 60, range an integer above 75 and at most 65535; a
default-handler row has no warm-up and no continuous beam. Schema 2 and 3 rows are
refused. Trace time is 0 to 60 seconds, accuracy 0 to 360 degrees; both default to zero when omitted. Omitted source burst duration is 1 second. The source leader flag
selects the original top combo row (PC row 11); non-leaders use row 2.

Current source eligibility selects all twenty source weapon NPCs; the eight
melee-weapon holders are exclusions. The DLL contains no source actor list or
per-weapon balance table.

## Agreement and installation

This changes existing NPC-combat rule **16**: the character bullet ray uses revision
**13**. Revisions 11 (the beta 7 base) and 12 (a separate live AI candidate) are
refused; OFF still advertises the released revision 3. The weapon brain uses words
2 (installed) and 3 (source fingerprint); without
`NpcSemiAuto=xml1` those words are zero. The rows themselves are bound by rule 16's
**detail** ([gameplay-agreement.md](gameplay-agreement.md), "Rule details"): the
SHA-256 of the complete canonical table (`npc_semi_auto_rules::canonical_bytes`:
`NSA4`, the row count, then every row in actor order as the name's length byte, the
lower-case name and the eight words, little endian), which the agreement digest
covers. Word 3 is the first four bytes of that SHA-256 (a short name for logs; the
install log and the `online: rule 16 detail sha256 ...` line show the whole
digest). A peer with any other table, or any older enabled revision, is
therefore refused by value. Beta 7's first build folded the rows into a 32-bit
FNV-1a in word 3 alone, and two valid one-row tables
(`haarpsoldier|3f20094f|...|3dce7218|00000020` and
`haarpsoldier|3e0e857e|...|3d07fa5d|00000020`, warm-ups about 0.1008 s and
0.0332 s) produced the same record (GPT Astra review, 2026-10-25);
`xml2_npc_semi_auto_test` now refuses that pair both ways.

The requested configuration is all or nothing: when `NpcSemiAuto=xml1` is set but
the weapon brain cannot install (no or invalid rows, a guard, the transaction), it
withdraws `NpcPowerRule`'s sites too (`npc_power::withdraw`, one verified
transaction back to the retail bytes; a failed rollback refuses startup), so
offline play runs XMen2.exe's own enemy AI instead of half of rule 16. The request
stays recorded, so rule 16 is unverified and host/join stay closed.

The aiming is lockstep simulation: its exponentials and trigonometry use
`det_math.hpp` (only correctly rounded double operations in a fixed order),
never the CRT's CPU-dispatched `pow` / `sin` / `cos`. Every entry point (the six
hooks' callbacks and the semi-auto entry callback) runs inside `det_math::fp_env`:
the game's own simulation x87 control word, 0x027f (53-bit precision, round to
nearest), and MXCSR 0x1f80, both restored on return. Both matter: MSVC's x86 SSE2
code still emits x87 instructions (a returned double is often finished by an x87
`fmul`), and at 24-bit precision the aim coefficient at dt = 1/60 came out 0x3cfef7c0
instead of 0x3cfef7d0 (review round 2). The word is the game's (review round 3):
XMen2.exe's CRT startup sets 53-bit precision (0x6729f6, `_controlfp(_PC_53,
_MCW_PC)`), and libIGGfx.dll creates the Direct3D 8 device with
`D3DCREATE_FPU_PRESERVE` (alchemy.ini `preserveFPU`, true unless set; a live log shows
behaviour 0x86), so Direct3D never lowers it; the display hook keeps that flag (and
adds it should an edited alchemy.ini drop it), resets take no behaviour, and no other
module of the install keeps a changed word (evidence/fpu-cw-audit.txt in the beta 7
fixes work folder). So the game routines the brain calls meanwhile (random draw,
angles, turns, move requests, visibility and entity-grid queries) compute exactly as
outside the guard: `xml2_entity_grid_test` registers an entity under the game's word
at a float/double cell boundary (min -1024, inverse 1/256, x 512.4999389648438: cell 5
in double, 6 in float) and finds it from inside fp_env for 24/53/64-bit callers, and
misses it at 24 bits unguarded. The x87 transcendentals among those routines (fpatan
in the direction-to-angles routine at 0x560835 / 0x56088e, fsin/fcos elsewhere) are
the game's own: lockstep already depends on them everywhere in retail, so the brain
adds no new cross-CPU risk. `xml2_npc_semi_auto_test` pins the output bits (from an independent
IEEE-double re-implementation), runs the aim smoothing, turn, burst timers and
entry deadline under 24/53/64-bit x87 precision x four rounding modes x three MXCSR
settings and requires identical bits and the caller's environment back, and checks
each hook's `ldmxcsr`. The entry points cannot throw (no allocation, no throwing
call; they are `noexcept`, so a C++ exception would terminate rather than unwind
through a naked hook frame) and use no `longjmp`; a fault inside game code they call
is an SEH crash of the game, as without the hook. A failed withdrawal of the power
rule refuses startup. Earlier candidates: revision 9 was the held-fire
officer brain, revision 8 used the burst/pause branch for the officer's found
query, revision 6 was the failed generic-picker candidate, revision 7 retained
converter-added capability flags, revision 5 was an earlier withdrawn role
experiment. OFF retains the released rule revision/values and returns before any
image access or weapon patch. No new rule ID, protocol, version or save format is
assigned.

Seven sites install in one guarded transaction with readback and rollback:
handler lookup `0x4f6bf6`, constructor completion `0x504f5b`, capability/range
refresh `0x50fce8`, the movement/input call `0x4341b6`, the empty near-target
query continuation `0x51a359`, the power-decision result `0x519fe7` and the
brain's target-resolve continuation `0x519e3a` (the attack-state input-lock exit
is rule 16's, see above). The input wrapper calls native movement once and
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

## Live checks

The native suites execute the gate, the brain branches and the bridges against fixtures; they do not execute the
native move request (`0x432030`), the move's own firing event or the emitted projectiles. The pistol/continuous
fixture counts gate *requests* (691 at 60 Hz in the reviewer's run), not shots. These stay live acceptance checks
(game agent, camera-matched against the original on xemu where marked):

1. Police pistol (default-handler primary): with fire held on a visible hero, count bullets actually emitted in 10 s
   at 30 and 60 fps, and whether a repeated request restarts the move before its 0.5 s firing event (zero or too few
   shots = restart starvation). Original: one shot per firing event.
2. Continuous beams (lightning commander, flamethrowers): the beam stays one continuous effect while fire is held (no
   restart flicker every gate update), the warm-up plays once per hold, and the sweep turns 0.015 rad per update.
3. Discrete warm-up weapons (nullifier, knockback officers): one warm-up before every shot.
4. Two-PC co-op with gunmen (lockstep): no desync over a gunfight of a few minutes (the aiming is det_math's, the
   rows are bound by rule 16's detail); both PCs' logs show the same `online: rule 16 detail sha256`.
5. All or nothing: a candidate ini with one broken `[NpcSemiAuto]` row logs `NpcPowerRule withdrawn with it` and
   enemies use XMen2.exe's own AI (energy-free powers every 0.3 s); host/join stay closed.

## Current acceptance status

The gate families pass the executed-original comparison; the empty-query
branches and the line-of-sight drop have native fixture tests only (their
original code is not executable in isolation). The officer and MP5 port
measurements are in [round 6](npc-weapon-round6.md) and
[round 7](npc-weapon-round7.md); the acceptance runs for the other families
are listed in [round 8](npc-weapon-round8.md). Normal builder output remains
unchanged; damage parity, live co-op and the new families still need acceptance.

## Converted bullet trace gate (beta 7 follow-up, A)

XML1 separates shot presentation from collision: its actor timestamp (+0x4a4)
limits bullet traces independently of recoil (+0x498). For a nonnegative last
trace, `double(now) - double(last) < double(tracetime)` rejects collision; equality
admits and writes now. A negative last timestamp bypasses without being updated.
The side table keys the full World token and actor pointer; input entry, fire
release, target changes and burst transitions do not touch it. New incarnations
start at zero. The MP5's source row appends `3f000000|00000000` (0.5 s, zero accuracy).

The builder marks converted bullet events with `split_count="0"` and
`split_damage="32766 32766"`. PC stores the latter as two literal signed 16-bit
values at event +0x3c; the zero split count makes those damage values inert in
stock execution. Ordinary beams, flames and projectile events carry no marker.
Both direct weapon-fire replacements and inherited named weapon events carry
it. Bullet sound/muzzle triggers precede their beam at the same authored time.

At ce_atk_beam 0x4dcfe0 the DLL scopes only marked events belonging to a listed
bullet actor. Before any native collision at 0x452225 it consumes XML1's three
character spread RNG draws (including accuracy zero), emits the native tracer,
and clears only the local stack copy of that tracer argument. It then tests the
trace timer under `det_math::fp_env` (game x87 0x027f, MXCSR 0x1f80). Rejected shots
skip the muzzle-obstruction query and, at 0x452350, the main query/contact/damage
block and its query disposal; native endpoint math and the presentation tail
still run. An empty normal bypasses the secondary split query. The scope masks
unrelated nested events and restores the outer scope on return. The event definition stays immutable; the attack
keeps its existing damage/type/modifier bytes and receives contact geometry. Every new patch belongs to the existing
all-or-nothing guarded transaction; OFF installs none of them.

Admitted marked bullets use the character ray in `npc_bullet_ray.hpp`. The muzzle
is the first weapon model's `Gun1` node, composed with the actor's authored hand
bolt world frame (PC weapon slot +0x7a8; XML1 +0x590). Missing actor models use the
entity frame; a failed actor bolt uses identity before the optional Gun1
composition, as XML1 does. The adapter consumes cached actor aim at PC +0x3ac.
For these zero-projectile-speed bullets, XML1 first lowers cached Z by 36, then
normalizes aim-minus-muzzle. Accuracy degrees use the original parser's widened
coefficient `0.01745329238474369`, rounded to float before sine. The existing
three simulation draws supply Z, Y, X spread offsets; direction is normalized.
The native RNG result is kept in binary64, preserving the seed bits that XML1
consumes directly from x87 ST0; each completed offset is rounded at the source
store. No additional random calls are made.
When its dot with the muzzle forward is **below 0.9**, XML1 interpolates from
forward toward direction by that dot, without a final normalization. Equality,
above-threshold and unordered dots skip interpolation (0xBA0CD..0xBA1C8).

The query sweeps zero extents from muzzle-36*direction to muzzle+range*direction
(MP5 range 550). It excludes the shooter, applies XML1's conditional source-team
and neutral-character filters, then property filters `(1,1)` and `(2,0xe)`
(XML1's latter property is 0xd). The friendlyfire debug switch and game mode
bypass team filtering. The XML2-only team-23 addition in `0x44fe10` is omitted.
Queries and contact orders use the same native virtual methods as
`xml1_turrets.cpp`; there are no fixed retail bitmap/slot offsets. Both pools are
checked before acquisition, including the temporary neutral-character query.
The first time-of-impact record stops the ray, including a world contact with
no entity. The adapter never searches beyond a prop/wall and adds no turret
visibility ray. Physents (characters and destructible props) receive the native
attack directly through PC virtual +0xb0 with `(attack,1,1,1)`, matching XML1
`0xBA79F`; there is no weapon to-hit roll. Damage values and receive multipliers
are unchanged. Presentation and the trace timer retain part A's ordering; no
extra RNG draws are introduced. Every new floating-point operation, including
engine matrix/query/hit calls, runs inside `det_math::fp_env`.

Native fixtures cover initial/equal/negative timestamps, incarnation changes,
clock rewind, FP restoration, presentation and RNG on rejected shots, nested and
unmarked controls, immutable events, both assembly continuations, and all eight
words in rule 16's full digest. The supplied owned XBE additionally executes
its own Gun1 composition, direction/spread, exact dot boundaries, segment,
first-contact filters and direct take-hit instructions alongside the adapter.
The owned PC post-aim beam fragment executes both admitted and rejected shots,
proving stack fields and the collision bypass. Query fixtures cover high slots
in a 1024-entity world, world/prop-first contacts, and pool exhaustion; caller
FP modes produce identical output bits and are restored. A local owned-source export can additionally be
passed as the native test's third path: first line is independently computed
canonical SHA-256, followed by the twenty schema-4 rows. It is never game content
in the repository.

### Game-agent re-measurement (not performed by this code-only task)

Reset the zone for each 120/300/520-unit cell, initially zero/full EP. Capture the
original at 49 Hz and the port at 49/60/30 Hz, measuring actual distance every
shot (port soldiers may approach 300-340 units). Compare baseline, A enabled and
A disabled and A+B enabled with the same content. Record shot events separately
from trace admissions/hits, actor token/incarnation, last trace, cached aim,
muzzle/direction, collision target/query-return handle, brain +0x1c/+0x20,
grenades and EP. Trace intervals must follow the weapon row, and presentation
must remain on rejected shots. Equality admits; rejected timestamps do not
advance last trace. Burst/input/target changes must not re-arm it.

Original probes: 0xBCC18 (shot), 0xBA515 (gate), 0xBA674 (query), 0xBA79F
(take-hit). PC: distinguish 0x4DCFE0 shot from begin_bullet_trace admission and
0x51A284 from 0x51A359 brain paths. Do not reuse the crashing 0x453555 probe.
A should remove the every-shot damage excess; residual distance-dependent misses
and the separate damage-per-hit discrepancy require independent measurement.
For the 300-unit cells the requested live acceptance is **0.71-0.76 hits per
admitted trace**. This code-only fixture work does not establish that live rate.
