# NPC power rule (`[Game] NpcPowerRule`)

```
[Game]
NpcPowerRule = xml1   ; computer-controlled characters use powers and melee combos as in X-Men Legends 1
```

Without the key, or with `xml2`, nothing is patched.

## Problem

Played on the X-Men Legends 1 port, the first game's enemies with powers (seen with the Brotherhood Marauders and
Revolutionaries at HAARP) use them far more often than on the Xbox, and a party gets stun- and knock-back-locked.
The data is the first game's; the engine decides differently.

## What the two games do

The design is shared. Each AI has four power slots, each with an aitype. The aitype selects a mask from the same
18-entry table and carries an aireusetime. One picker takes a random power whose reuse time has passed and whose
aitype range holds the target's distance: XML1 0x1081f0, XMen2.exe 0x50fe50. The same states call it. Three things
differ:

| | X-Men Legends 1 (default.xbe) | XMen2.exe |
|---|---|---|
| Who pays a power's cost | everyone except an AI-flagged character whose AI time (+0x550) is set and whose controller says no (0xcde95) | an AI-flagged character (actor+0x3d8 bit 0, every enemy) pays only when its controller says yes (0x4318e7). 0x503160 says yes once after a queued command (AI+0x2bc), so enemies never pay |
| Melee-range power decision re-check | 2.0 s, or 0.5 s for a character with a controller (0xee34a..0xee374) | 0.3 s for everyone (0x5188d8, 0x681ebc) |
| Energy before that decision | max / energy <= 2.0 unless AI flag bit 3 (0xee37a..0xee3ae) | none |

The melee combos differ in two more places (the brain's attack decision: XML1 0xeea60, XMen2.exe 0x518fd0):

| | X-Men Legends 1 | XMen2.exe |
|---|---|---|
| Combo level | set by the AI init (0xfd330): 6 (all six combos) for melee-type NPCs and party heroes, 2 for a ranged-type NPC without the leader bit, 3 for Toad and Pyro | the same choice (0x504a20; its top level is row 11 of 12), then overridden by npcstat `combolevel` (0x505011): 0 for every entry without it, and level 0 never starts a combo |
| Combos and weights | 6 combos, A / A S / A A A / S S / A S A S / A S A A A, weights 5 / 25 / 20 / 15 / 15 / 15 (0x452bac, 0x452c30, 0x452c18) | 11 combos, 12 named rows of weights (0x6dd4ac, 0x6dd4d8, 0x6dd508) |
| After starting a combo | the decision's tail sets the next attack time: now + 0.6 + rand x 1.2 s (meleetimeroffset / meleetimerrandomadd) and the crowd delay (0xef040 -> 0xef199) | returns at once (0x51993a, 0x519b04); only single attacks reach the tail (0x519840) |

So in the port the first game's NPCs (no `combolevel` in their data) never chained a hit: their melee was a stream of
single `attacklight1`s. With combos alone (a staged test that set the level), they chained but attacked again the
moment a combo ended (median gap 0.03 s against the original's 2.4-4.7 s).

XMen2.exe's actor+0x768 is XML1's +0x550: the same writers clear it, for example 0x5068a0 and 0x10b5c0. Its
actor+0x3d4 is XML1's +0x330: the same test gates the buff-self power in 0x512ed0 and 0x10ad70. Its AI+0x2b6 bit 3
is XML1's AI+0x28a bit 3: the timer bypass that both decisions read.

## Visibility before the power decision

The visibility result is also an AI scheduling decision. XML1 `0xfe550` calls the shared query scheduler at
`0xfc990`; XMen2.exe `0x505250` calls `0x502730`. When the scheduler declines a query, the visibility function
returns false and sets the caller's output byte. That byte means **deferred**, not "a wall blocked the target."
The power-decision timer is armed only after visibility succeeds. A deferred decision can retry.

| | X-Men Legends 1 | XMen2.exe |
|---|---|---|
| Ray endpoints | actor and target positions, each raised by 60 | actor-relative offset transformed into world coordinates; target's virtual position |
| Sweep extents | 1 in each axis | 15 in each axis |
| Query cutoff | below 600 units; at or beyond it, pass without querying | same cutoff, measured from its different endpoints |
| Per-tick query budget | one | two |
| Priority reservation | a reserved actor retains priority across ticks | the first request of each tick may bypass the reservation |

Neither visibility function has a facing cone. Both exclude hero, enemy and alternate-enemy team members from the
physical-entity candidate set; another attacker is not a line-of-sight blocker. The PC query also intersects a
collision-property set, retained here: it removed no candidates in the sampled arena. A served query accepts no hit
or the intended target, and rejects another hit. The existing query's cached-result behavior is preserved.

The picker's aitype range table agrees in both games: melee requires a distance strictly between 0 and 90, beam
between 100 and 500, and AOE between 0 and 120. An attack trigger's `maxrange` is not this AI selection range.
There is no beam-specific facing check in this picker. The separate ranged-state probe (`0xf07c0` / `0x51aac0`)
has the same 60–250 horizontal range and approximately 0.89 facing threshold in both games.

## The change

Every write below goes in one transaction (`xml1_hook_transaction`). Bridges, constants and combo tables occupy
one execute-read page; a separate read-write page holds the 32 per-brain press deadlines.

1. **0x4318e7 (9 bytes, jmp).** The bridge makes XML1's test. Without the AI flag, the character pays. With it, the
   character pays when -0.5 < actor+0x768 < 0.001. Otherwise it asks the controller at 0x4318f0, as before.
2. **0x5188d8 (6 bytes, call in place of `fadd [0x681ebc]`).** The bridge adds 2.0 when actor+0x3d4 == -1,
   otherwise 0.5.
3. **0x518920 (5 bytes, call in place of `call 0x50fe50`).** Unless AI+0x2b6 bit 3 is set, the bridge returns
   al = 0 (the picker's "nothing") when max (+0x314) > 2 x energy (getter 0x41d5d0). Otherwise it jumps to the
   picker with the caller's frame unchanged.
4. **0x505011 (3 bytes).** The AI init's `call [edx+0x44]` with npcstat `combolevel` becomes `add esp,4`: the level
   stays the one the init chose the first game's way (2, 3 or the top level 11).
5. **The combo tables (.data).** Slots 0..5 point at XML1's six combos (in the block) with their lengths; rows 2, 3
   and 11 - the levels the init picks - hold XML1's levels 2, 3 and 6 (the first 2, 3, 6 of 5 / 25 / 20 / 15 / 15 /
   15). Rows 0, 1 and 4..10 keep their weights and are reached only through `combolevel`, which no XML1 entry has.
6. **0x51993a and 0x519b04 (6 bytes each).** The start bridge arms the first press deadline, then jumps to the
   decision's tail at 0x519840: the next attack time and crowd delay are set as in XML1.
7. **0x5027bd (18 bytes) and 0x5027e0 (3 bytes).** Restore the original priority-reservation predicate and
   one-query budget in the shared AI query scheduler. The existing queued ages, timestamp, cache and collision
   implementation are retained. These queries are shared with other AI decisions.
8. **0x505250 (6 bytes, jmp).** The visibility adapter resolves the same physical target and calls the existing
   query with XML1's endpoints and extents, including its original cutoff and deferred/hit decisions. It reads the
   hit's packed token directly from the stable trace record, so it composes with the entity-capacity query owner.

9. **0x51920b (6 bytes).** The continuation waits until the press deadline has passed, advances the index, and
   reaches the existing press or completion code. XML1's per-button delays come from 0x452b94; its continuation
   is 0xeebec..0xeecb7. PC's immediate chain-availability test can otherwise abort a newly started combo and set
   the next attack time to now minus one. The timer advances even while the actor hold bit is set, as in XML1.
   A remaining deadline above the table's maximum delay (0.6 seconds) is treated as expired. This bounds a stale
   slot's wait after a clock rewind or reuse without changing ordinary equal/decreasing deadlines. An unarmed
   zero deadline advances once the clock is positive. Unordered clocks retain the previous wait behavior.
10. **0x5192cb (6 bytes).** Completion adds the final button delay to the existing time/offset/random accumulator.
   PC adds a fixed 0.2 seconds. The original single-hit combo adds zero; its longest combo adds 0.6 seconds.
11. **0x435b6e (7 bytes, revision 10).** A hit's reaction no longer lets the combo resume. XML1's brain 0xeea60 runs
   every frame whatever the body does and presses each step on its clock (0xeebec..0xeecb7), so the steps that come
   due while the NPC is in `pain` or `block` (shared_nodes: only `idle` chains out of them) are spent, and a hit
   usually ends the combo. XMen2.exe does not call the melee brain while the body is busy (measured: about 3 calls/s
   in `pain` against 29-54/s in idle/strafe), so the first call after the reaction finds the step overdue and
   presses it at once - from idle, which chains `attack`, `smash` and `powers` - and the combo resumes. At the end
   of the pain decision 0x435850, where bit 0x08 of the hit's reaction byte (+0xac) still set means the victim
   reacts (pain or block, chosen after it), the bridge marks the AI slots whose actor (AI+0x10) is the victim
   (a word per slot after the 32 press times). The continue step's front (the 0x51920b jump now enters it first)
   clears a mark and spends every step whose press time has passed: index + 1, press time + that step's delay, and
   the native completion when none is left; then the continue step as before waits for the first step still to
   come. A combo start clears its slot's mark (the exits' jump enters a front that unmarks, then the start
   bridge). The completion pause of a combo spent this way counts from the brain's first call after the
   reaction, not from the spent step's time (XML1's), so it can start up to one reaction later.
12. **0x518770 (5 bytes, revision 10).** The melee-range power decision has the same gap: XML1's 0xee260 also runs
   while the body reacts (1187 of its calls with the BrotherhoodEnergy in `pain` in one matched session), so a
   re-check that falls due then is spent - the power press finds no `powers` chain in pain or block - and re-armed;
   XMen2.exe's first call after the reaction found the re-check overdue and the power went off from idle (enemy
   `pain` -> `idle` -> `power_smash` with no decision between). The mark bridge also marks a second word per slot;
   the decision's entry front clears it and re-arms AI+0x1cc by the rule's re-check (2.0 s, or 0.5 s with a
   controller) once for each re-check that fell due before now (the clock 0x518849 reads: 0x46dce0 vt+0x160), at
   most eight, then runs the decision, which finds the timer in the future. The visibility/energy conditions XML1
   checks before re-arming are not replayed for the spent re-checks.
13. **0x518fd0 (5 bytes, revision 10).** The attack decision itself: with no combo running, XML1's deadline
   (AI+0x1c) can fall due while the body reacts; the decision is then made and spent (no chain out of pain/block)
   and its tail re-arms the next attack time (now + meleetimeroffset + random x meleetimerrandomadd). XMen2.exe made
   the overdue decision on the first idle frame after the reaction (a fresh attack within 0.3 s of 12-14 pains per
   four matched windows, 1 on the Xbox). A third mark word per slot; the melee decision's entry front, when no
   combo is running (a running combo keeps the mark for after its end), clears it and re-arms AI+0x1c from the due
   time by AI+0x288 + random x AI+0x28c (the draw 0x560190(1) the completion code uses) for every decision that fell
   due before now, at most four, then the decision as before. The crowd delay of the tail is not added for these.

XML1's second combo table (brain mode 1) has no XMen2.exe brain. The earlier target-loss cancellation branch and
the PC's press notification remain unchanged. No claim of complete AI equivalence is made.

All guards are checked before anything is written: the retail bytes of every site, the controller read at 0x5188c3,
the energy getter's read, the combo exits' tail, the five table runs, the visibility/query entry helpers, and the
AI pool's base, stride, count and embedded-brain offset. Brains outside that guarded pool use the native step.
A mismatch leaves the game as it is and writes one log line. A failed write is rolled
back. A failed rollback refuses startup.

Party heroes are affected by the same rules, as in XML1. An AI teammate decides every 0.5 s instead of 0.3 s and
needs half its energy for a melee-range power. Whether it pays still depends on its controller, unchanged, because
its AI time is set.

## Gameplay agreement

Online play is lockstep and the rule changes AI decisions and energy, so it is shared gameplay rule **16**
(`gameplay_capabilities::npc_power_rule_id`, [gameplay-agreement.md](gameplay-agreement.md)): revision 10,
`{installed ? 1 : 0, installed ? 1 : 0, officer weapon brain installed ? 1 : 0, its source fingerprint}`. Word 1
advertises XML1 visibility geometry and query scheduling; words 2 and 3 are zero unless the separate opt-in
`[Game] NpcSemiAuto=xml1` with its `[NpcSemiAuto]` rows is present ([npc-semi-auto.md](npc-semi-auto.md)).
Revision 2 had only the power/combo word, revision 3 lacked the press/completion timers, revision 4 lacked the
reaction-spent combo steps, power re-checks and attack decisions (items 11-13). Revisions 5-9 were unreleased
candidates and are not reused: 5 a withdrawn role experiment and the first reaction-spent test build, 6-9 the officer
weapon candidates (9 = the held-fire brain, now words 2-3 of revision 10). Registered in an LC installation or when
requested; absent or `xml2` retains **revision 3** with verified `{0,0,0,0}`, matching the released OFF record. Only
requested configurations use revision 10; every other enabled revision (3, 4, 5-9) is refused both ways, and a
revision 10 PC with the officer brain and one without refuse each other by value, all naming rule 16 (NpcPowerRule).
Requested but not installed (a guard mismatch, protection, readback, an unknown value, `NpcSemiAuto=xml1` without
`NpcPowerRule=xml1` or without valid rows) is unverified and closes host/join.

### Bridge register contracts

At `0x5192d1`, the completion jump reaches `0x5192ed`, which reloads ECX with the AI before passing the x87
accumulator to `0x5382a0`. That callee overwrites EAX from its stack argument and writes the next attack time;
it never reads EDX. The later paths reload EDX or return a boolean in AL. Incoming EAX/ECX/EDX are dead.

At `0x519840`, EAX is loaded from the existing stack frame before the branch. The fallthrough initializes a
stack-local query with a fresh ECX, then `0x5252e0` supplies its own registers; the alternate `0x519c6a` path
reloads EDX/EAX from the brain. Later ECX stack pushes reserve storage overwritten by `fstp`, rather than pass
the old ECX value. Incoming EAX/ECX/EDX are dead on both paths; EBP and the original stack frame remain live.

### Clock and transition review

AI clock writes at `0x511d4f`, `0x5125ff` and `0x51672b` copy the game clock (vtable +0x160, accessor
`0x469740`, game+0x3e8). A bounded live check saved and loaded during combat, then changed zones with a combo
in progress and returned. The clock kept advancing through these paths; departing AI slots were retired and
new slots initialized. Combos advanced after load and after the round trip, including reused pool indices.
The observer checked the active-slot bitmap and generation tokens, excluding stale bytes in retired slots.

This did **not** reproduce a natural clock-reset stall. The maximum-wait clamp is a defensive correction,
verified against the old emitted wait branch with a synthetic rewound clock. The live trial staged zone
loads, authored spawn-point visits and invulnerability (reapplied after load). It establishes these transition
paths, not a guarantee that the clock is monotonic for every game lifecycle or an isolated combat-rate result.

### Revision 4 acceptance status

This is a **candidate awaiting live acceptance**, not a measured resolution of the remaining stun gap. Native
tests execute the emitted bridges with synthetic brains, and private characterization compares their timing
with the original executable's continuation. The previous unshipped experiment retained PC's completion
addition and an extra hold gate; revision 4 corrects both. Synthetic completion pauses with offset 0.6 and zero
random addition are 0.6, 0.8, 0.8, 0.8, 0.8 and 1.2 seconds, matching the original code.

The historical combat tables below predate revision 4. Two new release-base/candidate pairs and vulnerable
real-input fights are described in the follow-up below. The new rate measurements had other test games running
and do not establish timing acceptance. Isolated repeats, party AI, online play and save/load transitions remain
untested for this candidate. Hero reaction handlers have not changed;
the missing original stun handler and PC stun-powerup behavior require separate investigation and a separately
coordinated gameplay rule if an engine restoration is needed.

## Evidence

Staging, the same in both games: the zone loaded by script / console, the hero invulnerable and idle, a teleport onto
one of the zone's spawner points, the zone's own spawns. A read-only tracer logged every FightMove start of every
character (port actor+0x378, original actor+0x2f4; the original ran in xemu, its rates per game-clock minute).
"Chained/heavy" counts the second and later hits of a combo and the heavy, trip and stun attacks. Hero free: the share
of time the hero is not in a stun, knock-back, thrown, get-up or wall-slump move. Two runs per column unless noted;
small samples (one Marauder per run, 120-150 s).

HAARP ice tunnel (`haarp/int/icetunnel1`), per enemy per minute:

| | Original | 0.2.1 (1 run) | 0.2.2 | Power rules only | Power + combo rules |
|---|---|---|---|---|---|
| Revolutionary powers | 4.3-6.4 | 16.9 | 16.0-17.8 | 9.8-10.2 | 10.2-10.6 |
| Revolutionary stun arc (power_smash) | 1.8-4.4 | 15.0 | 11.8-16.0 | 5.4-6.3 | 5.1-5.7 |
| Revolutionary chained / heavy hits | 4.6-4.8 | 2.5 | 1.0-1.2 | 0.3-0.6 | 3.8-5.0 |
| Revolutionary pause between attacks (median) | 2.4-4.2 s | 2.1 s | 2.7 s | 3.2-3.5 s | 3.2-3.5 s |
| Marauder powers | 10.0-10.7 | (not engaged) | 19.6 | 16.7-17.7 | 5.6-17.6 |
| Marauder chained / heavy hits | 3.3-6.9 | (not engaged) | 0 | 0 | 2.4-5.2 |
| Hero free | 51-53 % | 6.5 % | 0-1.5 % | 18-20 % | 22-38 % |

HAARP interior (`haarp/int/haarp2_1`), GRSO soldiers (the port's guns are powers made by the builder, aitype
beamanyrange; see the correction below the table):

| | Original | 0.2.2 (1 run) | Power + combo rules |
|---|---|---|---|
| HAARP Soldier shots + punches / min | 35.5-35.8 (all attacks) | 45.7 (34.8 powers + 10.9 melee) | 31.5-37.5 (20-24.5 powers + 11.5-13 melee) |
| HAARP Leader attacks / min | 44-46 (incl. 3.9-5.1 smash) | 39.5 (31.5 powers) | 31-35 (11-15 powers) |

*Correction, 2026-10-23: the pooled "shots + punches" total hid a difference, and the first game's guns are not
melee-style attacks. In matched 45 s windows (attack-101b round 5; officer on the same spot, initial energy 0) the
first game's HAARP officer fired 49 shots with 0 melee entries (median gap 0.91 s, the freeze gun's 0.9 s recoil),
while the released port fired 0 shots and made 24 melee entries, because the converted gun has the generic melee
brain and range 90 where the first game's weapon brain uses range 550. So these totals match while the behaviour
does not. A restoration exists only as the opt-in `NpcSemiAuto=xml1` part of gameplay rule 16 revision 10 for that one officer (not
in this release; candidate revision 9 introduced it, revision 8 used a burst/pause branch the original officer never runs); the other 19 gun families are not covered. Online play, party AI and other encounter types were not
live-tested.*

The first game's AI decisions were also counted directly in xemu (breakpoints at default.xbe 0xee2f4 / 0xee3f1, 30
game seconds): the melee-range decision passed its timer 9-23 times per AI and picked a power 3-6 times, against 10-11
and 4-5 per Revolutionary with the rule (Frida on 0x518770 / 0x5188e7 / 0x518925).

*Note on the attack-rate figures for #101 (2026-10-23): two measurement protocols exist and are not pooled. With a
moving, pursued hero (block-025; 17 original and 8 port trials) unarmed enemies make 18.7 against 27.8 attacks per
enemy-minute, 1.49 times as many in the port (descriptive interval 1.21-1.82). With a still, controlled hero
(attack-101; 3 trials per arm, ice tunnel) the figures are 20.4 against 32.3, 1.58 times. Both put the port at about
1.5 times the original; a plain 60/49 frame-rate ratio (1.22) does not explain it, and the cause is not established.*

### Visibility follow-up

One 150-second control with the power/combo rule and two 150-second runs with the compiled visibility fix used
four engaged Revolutionaries and one engaged Marauder. In addition to the earlier staging, the hero briefly
visited the Marauder's verified spawn point, then returned to the measurement point, to ensure it was engaged.
Rates use the same per-actor observed-window analyzer as the earlier port runs; inactive spawns are excluded.

| | Original reference | Power/combo control | Visibility run 1 | Visibility run 2 |
|---|---|---|---|---|
| Marauder powers/min | 8.6–10.7 | 20.9 | 12.3 | 15.7 |
| Revolutionary beams/min | 2.0–3.5 | 3.1 | 2.5 | 4.6 |
| Hero free | 51–53% | 18.3% | 21.2% | 24.8% |

The original figures are historical measurements, not a new original-game comparison. This is a partial
improvement in small samples, not proof that the remaining aggression or beam discrepancy is resolved.
The Marauder's visibility failures observed in the compiled runs were query deferrals, not physical obstructions
(some Revolutionary queries were physically obstructed). The shared
query policy changes when the AI can advance its decisions; the ray restoration is supported by the executable
comparison, not by an observed crowd-blocking effect.

The source's synthetic visibility tests cover invalid targets, geometry, cutoff, coincident points, deferral and
hit classification. An executable test runs the patched reservation/budget fragment against synthetic actors;
retail-byte tests reject each changed guard. The full native suite and agreement suite pass. A pipe-driven hero
movement check after the second measurement also succeeded. Online play, party AI and other encounter types
were not live-tested in this follow-up.

Open: revision 4's press timing still needs an isolated live comparison. Successful power selection also has a different
aim/return path: XML1 normally turns and returns before
its fallback NPC pause, so merely changing the PC's post-pick pause to one second is not an established fix.

### Revision 4 follow-up (timing acceptance still pending)

Two fresh 150-second pairs used a locally built release-base DLL and the compiled revision-4 candidate on the
same released content. Each had four engaged Revolutionaries and one Marauder. The hero was idle and invulnerable;
the zone load and visits to the two authored spawner points were staged. The unchanged transition tracer and
per-actor observed-window analyzer were reused. Inactive spawns were excluded.

| Metric | Historical original | Release control, two runs | Revision 4, two runs |
|---|---:|---:|---:|
| Marauder powers/min | 8.6–10.7 | 13.6, 18.5 | 12.1, 12.8 |
| Hero free | 51–53% | 23.4%, 17.3% | 31.7%, 27.6% |
| Revolutionary beams/min | 2.0–3.5 | 3.8, 4.3 | 3.3, 3.1 |

**Other active test games were observed during every run. These rates are timing-confounded and are not valid
release-acceptance measurements.** The observed direction is encouraging, but neither a causal effect size nor
original-game parity is established. The original column is historical; no new original runtime was used.

Separate vulnerable fights used real virtual-pad inputs. The fresh level-1 hero performed attack moves and died.
A level-10 retry, staged with an XP award and calibrated stick aiming, recorded movement, an attack, enemy health
loss and another hero death. These were solo tests against the full group, not wins or campaign playthroughs.
A separate real-stick movement check at the observed entrance covered about 99 units with health unchanged.

Hero-side checks do not support a simple slower-get-up explanation: the audited playback speeds and get-up
handler entry, exit, cancel and tick operations agree. Historical traces have a different mixture of wall-land
and back-land reactions, so longer average knockdown bouts cannot be attributed to one slower handler. The
original stun handler and PC timed powerup still differ; this candidate changes neither.

A separate 90-second observer confirmed ten PC idle-selection redirects into stun while the stun-powerup flag
remained set, with seventeen observed applications and seventeen clears. One stun applied during a landing
reaction persisted through get-up; the hero then entered stun for another 1.115 game-clock seconds before the
powerup cleared. The original handler sets its flag on stun entry and clears it on exit. This establishes the PC
mechanism in this encounter, not its total contribution to the gap or the correct complete restoration. Any
hero-side engine change needs a separately coordinated gameplay rule; no new rule ID is allocated here.
