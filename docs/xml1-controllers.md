# XML1 Master Mold controller

`Game.Xml1MasterMold` defaults to off. The first-game builder enables it only with
its generated, validated saved-stage binding. The scan turret controller
(`Game.Xml1Turrets`) is described in its own section below.

Master Mold runs at the native brain state dispatcher, `0x5173f3`, after native
lifecycle, conversation and wait gates. Its handled path rejoins native movement
and output postprocessing at `0x517a79`; other entities retain the original state
dispatch. Queued native script commands take precedence. The earlier default-AI
vtable hook was insufficient: other native states bypassed its timing decisions.
The controller consumes existing pattern requests and drives native
shield/blaster nodes. The two-second shield wait and four-second core attack
window use game time, with a nine-second retry deadline. A generation-checked
core handle survives ordinary AI target clearing; the beam bridge corrects the
direction after the native muzzle calculation. Collision, damage, structure
requirements and death scripts remain native.

The controller also implements the original platform-marker selection and native
death dispatch, phase-change catwalk activation, stomp/ranged/floor attack
decisions, and waypoint movement. Floor attacks keep three independent deadlines
and the original priority: an unavailable due action postpones that action by
half a second without trying a later action during the same update. Successful
floor attacks wait one second and renew their deadline by ten seconds. The native
wait-bit contract is preserved upstream. A target is checked again
after aiming, immediately before a core attack; a missing target starts the
original nine-second retry delay without firing.

The authored spawn script's `setDefaultTarget` call resets transient controller
state before that script reissues the saved phase request. `Game.Xml1MoldStage`
must bind the builder's packed stage as `flag:first_bit:width` (one-based bits).
The native saved-flag reader reconstructs later combat after a post-shield load,
whose spawn script does not reissue a pattern. Missing/invalid binding refuses
installation. No DLL-owned event is left in the native scheduler, and no new save
field is introduced. A staged
combat fixture has exercised both health thresholds, native core deaths and a
Save Game -> process restart -> Load Game cycle with the shield and broken-core
state preserved. Real switch interactions lowered the shield; native party
attacks then triggered the death script and ending credits. The fixture used
party XP, invulnerability and resurrection assistance and omitted movie files.
These checks do not certify an unassisted playthrough or movie playback.

The original companion engagement/follow rules use native roster aliases and
generation-checked AI tokens. Core tethers use the native head attachment and FX
services, with authored lifetime and native zone-group cleanup. Source-only
companion states with no observed entry in the shipped selector callers are not
assigned invented PC state numbers.

Core selection uses the native action-entity query and its spatial bounds/order:
800 units around the boss for a forced request, or 400 around the active hero
when that hero is strictly within 380 of the boss. The original XBE loop at
`0x1162f0..0x11637f` compares a candidate's activation distance to its distance
from itself and retains the last eligible result. It does not impose an extra
360-unit hero-to-core center-distance check. PC action eligibility is `0x41a820`;
the action class descriptor is `0x70b678`. XML1 team bits none/enemy/altenemy
26/28/29 translate to PC 28/30/31, and aishootme 23 to PC 24.

All controller entry replacements and the beam patch are acquired, written and
read back as one startup transaction. Signature, protection, write, readback or
cache-flush failures refuse activation. A verified rollback retains the existing
unshielded encounter fallback; failed rollback, cache flush or page-protection
restoration refuses process startup. The query wrapper checks availability
of both native temporary pools before acquiring either and releases them before
starting combat. Its retail pool-layout guards are checked again at boss spawn,
after all startup installers, so an incompatible capacity replacement retains
the native encounter fallback.

## Shared boss dispatcher

`xml1_bosses` owns the native brain-dispatch site `0x5173f3` (resume `0x5173f9`, handled updates continue at the
postprocess `0x517a79`) and the three script-table entries (sequence `0x4a4830`, combat node `0x4a51a0`, default
target `0x4a4e20`; request-driven bosses use them). Its clients are Master Mold (`Game.Xml1MasterMold`, rule 3) and the bosses
`[Game] Xml1Bosses=<comma list>` names (rule 40); the builder writes both keys. Each client checks its own guards;
the dispatcher writes one transaction for all of them, in Master Mold's order (its beam patch, the three script
entries, the brain dispatch). With only Master Mold requested the transaction is the one it wrote before; with only
bosses it is the brain dispatch alone, plus the script entries for Avalanche, Pyro, Mystique, Shadow King, Magneto or Blob.
Mystique also contributes two guarded skin-copy bridges to that transaction. Nothing requested writes nothing.
Clients check zone and generation-checked actor identity; Mystique shares asteroid2_1 with Magneto. A refused
transaction keeps native encounters except a requested Magneto controller, whose ON content requires startup refusal; a failed rollback refuses startup.

Rule 40 (revision 1) registers only when the list names a boss: `{installed boss bits, FNV-1a-32 of
"name:revision;" over the installed bosses, option bits, 0}`. Boss bits: 1 = dopples, 2 = avalanche, 4 = pyro,
8 = mystique, 16 = shadowking, 32 = marrow (retained native brain, no replacement), 64 = magneto, 128 = blob. Avalanche is revision 2;
the other entries are revision 1. An unknown name, or a listed
boss that did not install, registers unverified (host/join closed). Magneto also refuses offline startup because its controller contract removes the SPEC 36 protection substitute; other clients keep their native fallback.

## Doppelgangers

`[Game] Xml1Bosses` listing `dopples` restores X-Men Legends I's colosseum dopples (`astral/ast3/colosseum`,
instances `evilwolverine`, `evilcolossus`, `eviljean`, `evilcyc`). In the original (default.xbe) the `dop_*`
setstates (ids 0x22..0x25) store the dopples' handles in 0x4feda0..ac, run the overlay init 0x10f760 through AI
state 0x38 and return the brain to state 0; the AI pre-dispatch 0x117030 then runs the overlay 0x116f70 on every
update of an enemy-team actor while 0x4fee08 is set, and a handled update skips the native state machine - the same
position as the PC dispatch site. XMen2.exe parses the unknown `dop_*` names as `attackphysents`; the overlay binds
by zone and instance name instead.

| Original | Behaviour kept (decision rules: `src/dopple_rules.hpp`) |
|---|---|
| tick `0x110470` | once per distinct game time: re-arm time passed, no fastball running, Wolverine and Colossus alive and closer than 400 -> both fastball flags 1, re-arm + 5 s, Wolverine's movement target and navigation cleared |
| Wolverine `0x1136f0` | flag 1: move to Colossus (speed 3.0); closer than 55: stop, flag 2, face him (immediate transform). Flag 2: 1 s brain waits. Otherwise native. Colossus missing, not a character or dead: flag 0, native |
| Colossus `0x1138a0` | flag 1: move to Wolverine; closer than 55: stop, flag 2, face him (movement transform). Flag 2: face Wolverine; alternate slot 3 (guard/action) available: combat target = the active hero, buttons 0x100, both flags 0, 1 s wait |
| Jean `0x113c50` | state 0: game time past the hazard time -> + 7 s, state 3. State 3 with an active hero: brain target = hero, the nearest enabled aishootme actionent within 400 of the hero whose activation radius covers the hero (`0x50a250`, flag 1) is activated by Jean (vt+0xd4, activator + 0), state 0, 1 s wait. States 1/2 as the original has them (2: normal slot 5 -> buttons 0x90, state 1; 1: Cyclops' flag 1); nothing enters state 2, and none is invented |
| Cyclops `0x113b70` | flag 1: normal slot 5 -> buttons 0x90, 0.4 s wait; flag 2: held; otherwise native (dormant: Jean's state 1 is never reached) |
| every handler | random < 0.05 -> boss taunt first (`0x5279c0`: the gameplay random stream, then the native taunt `0x420e10`) |
| setstate `0xf4355` | the brain's bossfull flags: bit 0 set, bit 2 clear (PC brain +0x2b8), once per dopple brain |

Not in the original: a fastball not thrown 10 s after it started is abandoned (both flags 0, native AI), so a
throw the PC move set never performs cannot hold both dopples. `[Game] Xml1BossFull=0` skips the bossfull write
(rule 40 option bit 1 clear). Native script commands keep precedence. Every native call is guarded
(`src/boss_dopple_sites.hpp`, 30 sites; `xml2_xml1_controllers_test <XMen2.exe>` checks them against the retail exe).
Online: every peer runs the overlay from the same lockstep update with the game clock, the gameplay random stream and
entity state; the active hero is the shared player-slot hero inside a session.

Live checks (not yet done; the capture decides): **D1** Colossus' 0x100 next to Wolverine performs a grab/throw
toward the hero on the PC move set (if it does nothing, the watchdog ends the hold; the throw must then be bound to
the dopple's authored move). **D2** no fastball reaches the 10 s watchdog in normal play. **D3** the bossfull flag
does not change the dopples' fight against the original (otherwise `Xml1BossFull=0`).

## Avalanche

`[Game] Xml1Bosses` listing `avalanche` restores X-Men Legends I's Avalanche brain in `mount/mount/mount2` and
`astral/ast3/astral3_4` (instance `avalanche`). Original (default.xbe): setstate `avalanche` / `avalanche_astral`
(id 9) -> AI state 0x24 (init 0x10f6f0 zeroes 0x4feda0..ac) -> state 0x25, brain 0x111ab0, which owns every update
until `endbattle` (setstate 0x2a) leaves the state. The controller binds the instance in those zones, returns every
update as handled (native script commands and death keep precedence) and stops at the script's
`setPatternSequence("avalanche","endbattle")`.

| Original 0x111ab0 | Behaviour kept (decision rules: `src/avalanche_rules.hpp`) |
|---|---|
| health band | k = trunc(6 - trunc(health %) x 0.06f) (bands change at 83, 66, 50, 33, 16, 0 %); on a change the brain's flag bit 3 (XML1 +0x28b, PC +0x2b7: the same two decision sites test it, XML1 0x10afa7 / 0x10bf51 = PC 0x513146 / 0x514613) is set for odd k and cleared for even k |
| target | nearest living hero, including AI companions; separately retain the last controlled hero in native list order for mid-range facing |
| closer than 100 | past the close deadline and normal slot 6 (power_smash) available: buttons 0xa0, deadline + 4 s, taunt (chance 1); before the deadline: random < 0.5 -> 0x20 (smash), else 0x10 (attack); then face the nearest hero. Expired deadline with unavailable slot 6 has no random fallback |
| closer than 400 | past the mid deadline: face the last hero; slot 6 available: buttons 0x90 (the original tests slot 6 and presses power_attack), deadline + 4 s, taunt with chance 0.5 (0x10f690 = PC 0x5279c0) |
| farther | nothing |

Rule 40 boss bit 2, revision 2. No wait accompanies the mid-range attack. The band flag suppresses native combat
selection; damage immunity has not been established. Live checks (not yet done): **A1** the band flag (+0x2b7 bit 3) changes his fight on
PC as in the original at the 83/66/50/33/16 % bands; **A2** with several players online, the hero order (party
order here) and the AI-control bit of a remote player's hero read the same on every peer (as for the turrets).
Also check fractional health percentages, nearest AI versus farther controlled hero, separate four-second
deadlines, unavailable slot 6, and endbattle/outro in both encounters.

## Pyro, Mystique, Shadow King and native Marrow

The opt-in entries below share the dispatcher above. Native death, wait, queued commands and conversation gates
retain priority. Owner generation, zone departure and a clock rewind reset transient state. No replacement ticks
Marrow, and no new agreement ID is introduced. `[Test] BossTrace=1` logs actions and requests.

| Entry | Binding and implementation |
|---|---|
| `pyro` (bit 4) | astral/ast3/astral3_3, instance pyro. Native all-player hero list, living and absolute Z difference below 36. Team setter 0x41d3e0 uses PC none 28 / enemy 30. Fire-up: slot 8, below 300, three-second deadline. Fire-down: slot 5 below 200 or slot 8 below 300, five-second deadline. Saved Y/Z with current X supplies the facing origin. Fire-up releases the five attacker reservations at brain +0x18c..0x1ac through generation-checked AI lookup, state reset and navigation/destination cancellation. HAARP Pyro is outside scope. |
| `mystique` (bit 8) | nyc/alison/nyc1_1_2b, mansion/dr_mag2/mag_nyc3, astroid_m/visit1/asteroid2_1, instance mystique. myst1 preserves native state; myst2 starts grenade mode after two seconds, then named power_xtreme every four seconds, without slot availability. myst3 restores native control and boost-selection mask +0x438 = 0x204; gopattern/mystique initializes copied-form pursuit. Retain a living target; reacquire nearest AI hero before controlled fallback. Within 70, uncopied emits 0x180; copied attacks only on random >0.98, then attack/smash 50/50. |
| Mystique restoration | Native effect 0x4dff50 still selects/copies the skin. Our entry bridge retains the current trigger context; the success site 0x4e01a9 observes the actual copied target. Literal Knockback at trigger +4 supplies 10/15/22 seconds. Owner-scoped expiry or target loss restores the actor's own stats skin through 0x42b240 and plays transform_back. The PC has no equivalent of XML1's timed effect type 0x3a; that number is never passed to the PC. Nonliteral/modded duration expressions are unsupported and logged. |
| `shadowking` (bit 16) | astral/savepx/final_astral, instances shadowking / shadowking2, separate owner state. Initializer 0x10f7c0 starts both attack deadlines at trunc(now+6), sets bossfull bits 0+2 and preferred distance 350 (PC +0x280, confirmed at 0x511824). First form spear: 100 < distance <300, slot 5 / 0x90, integer deadline trunc(now+6). Smash: below 120, slot 6 / 0xa0, trunc(now+7). Successful attacks wait 0.6 seconds and attempt a separate 50% taunt. sk3 queues slot 7 / 0x180 below 300; first-form preferred distance becomes 90. sk4 queues slot 8 / 0xc0 and a 0.3-second wait, then continues branch processing. Shield uses authored def_damage scale 0, life 31, with xtreme lifetime override 0.01. Chase starts above 300, continues above 180, then uses PC close movement 0x5061d0. Second-form spear range is 100..170 exclusive with trunc(now+5); below 100 its two output words retain the original distinct movement/attack bits. |
| `marrow` (bit 32, native) | sewers/hub/sewers1_2_4, instance marrow. Six signatures verify backboss parsing, native dispatch and routines 0x529890 / 0x529a10. This entry writes no hook; a Marrow-only list keeps native AI and adds only agreement metadata. |

The builder removes the SPEC 36 hard invulnerability script substitute only when planning the Shadow King
controller. It lists that controller only with both actor names and the two generated scripts free of the old
substitute. The first-form floor relocation remains: no causal pillar damage fix has been established.

Live acceptance remains outstanding for all wave-two entries. Use at least two matched original/PC runs with the
same party and level, compare observable phases and fight length (+/-30%), then test two lockstep peers.

- Pyro: cross both 36-unit Z boundaries; displace X, Y and Z separately; trace none/enemy changes and all five
  attacker reservations, three/five-second deadlines, brazier and shield timing.
- Mystique: asteroid mystique2 stays inactive until gopattern; NYC/Danger Room native paths and retreats retain
  priority; two/four-second grenade cadence; successful copy, authored expiry, early target-loss restoration,
  repeated copies and zone/reload cleanup. Magneto in the shared zone must never be claimed.
- Shadow King: shield requests at 84/68/52/36/20%, normal attacks versus xtremes under the shield, 30-second
  dropshield and both forms' attacks/movement. Compare identical unshielded attacks on floor and pillar, tracing
  beam direction 0x45230b, melee contact 0x453160, damage receive 0x437490 and health +0x27c. Record actor origin,
  collision bounds, selected target and actual hit entity; identify the first failing boundary before patching.
- Marrow: confirm state 0x1f and calls to 0x529a10, route index/direction/reversal latch, 84/150/200/300 boundaries
  with one and four heroes, hazard-query eligibility and projectile impacts, and ending conversation. Authored
  steam hazards declare team hero; do not force activation without observing native query results.

## Entity capacity

Core selection and waypoint lookup call the native query entries `0x4ab550`
(manager), `0x4ab6d0` (acquire), `0x4ab770` (ordered results), `0x4ab720` and
`0x4ab7d0` (release), and the query/order vtable slots, under both entity layouts
([entity capacity](entity-capacity.md)). The installed layout comes from the
capacity module, published after the startup transaction and before this
controller installs. Stock: the entries must hold their retail bytes and the retail
busy bytes (query +0x33, stride 0x1e4; order +0xdc0, stride 0x814) are tested before
acquiring, because the retail acquisition assumes a free query. Raised
(`EntityCapacity=1`): each entry must be the installed E9 jump into this DLL and the
native getter must return the installed query manager; a busy owner returns null,
which ends the selection. Result counts are bounded by the installed World (256 or
1024). The same checks run again at boss spawn. A mismatch keeps the unshielded
native encounter.

## Online play

XML2 co-op is lockstep, not host-authoritative. Every peer simulates the whole
zone: the actor update `0x434540` reads the session flag (`0x610d20`) only to
choose player-input handling, and its AI branch `0x424b70` calls the AI manager
(`vt+0x1c4`, `0x525370` -> `0x526380` / `vt+0x1c` `0x522630`) and the brain's
`vt+0x10` (`0x51d880` / `0x51d610`), which reaches the brain update `0x517370` with
no host or client test. Session setup (`0x613df0`) seeds the gameplay random stream
(`0x560190(1)`) to the same constant on every peer, and the per-frame sync message
`0x2e` (`0x614a40`) checksums hero positions with that stream's call count;
a divergent peer is dropped. Restricting the controller to the host would
therefore desynchronize the session. Instead every peer runs the controller from the
same native update, with only shared inputs: the game clock, the gameplay random
stream, script requests and saved flags, all of which run in lockstep.

`_ACTIVE_HERO_` is the exception: it resolves through the player manager's index
(`0x4a1759`, table `0x70b814`), which the native session setup points at each
machine's local player. Inside an online session (`0xa53058+0x420`) the controller
uses the first player slot holding a living hero instead; outside a session it keeps
`_ACTIVE_HERO_` as before.

Effective state is registered in the shared gameplay-capability record
([agreement](gameplay-agreement.md)) after installation: rule 3, revision 1,
`{installed, first_bit, width, FNV-1a-32(flag)}` from the applied saved-stage binding;
the fourth word is the 32-bit FNV-1a hash of the exact flag name, so peers bound
to the same bit range of different saved variables are refused rather than
reconstructing different phases after a post-shield load. Not
requested registers verified off `{0,0,0,0}`; requested but not installed (missing
binding, guard, query/effect support or transaction refusal) registers unverified,
which closes host/join for that process. Peers whose Master Mold state differs,
including one side off, are refused by the native agreement before admission.
Rule 4 (turrets) is described below. Failure to install a required socket exclusion refuses
startup.

`xml2_xml1_controllers_test` exercises selection counterexamples, transaction
rollback and the production socket gate with isolated providers and no sockets.
Live traces show the three independent floor attacks recurring at their native
ten-second deadlines, automatic core destruction, saved phase reconstruction and
ending progression. Further acceptance includes original-game visual comparisons,
and broader interruption/revisit cases.

## XML1 scan turrets

`Game.Xml1Turrets` defaults to off. The first-game builder enables it with one
`[Xml1Turrets]` line per placement (`Count`, `T1`..`T32`):
`zone|instance|px,py,pz,rx,ry,rz,mx,my,mz|visrange,minspeed,mintime,resetdelay,visextent,yawextent,pitchextent,turnrate,team|kind,recoil,count,spread,speed,accuracy,range,damage_lo,damage_hi,tracetime,radius,damagetype,damagemods|projectile|entfile|muzzlefx,muzzleaccfx,impactfx|firesound,rotatesound`,
all from the original placement, its turret weapon and the first game's value table (`m` is the
turret model's `Gun1` bolt, damagetype/damagemods are the PC bits of the weapon's names, the
effects are paths under `effects/`). An instance written `*name` is spawned by a zone script (the
Hive tanks' `spawn("tank1", "tank_turret_sp01", "turret1", ...)`): it is bound by name anywhere in
the zone and its orientation when bound is the rest basis.
Any malformed line, guard mismatch or refused hook keeps every turret an inert physent
(the 1.4.0 behaviour) with one log line.

The first game's `scanturretent` (default.xbe class registration `0x3571c0`, size
`0x380`, parent `scanent`) runs three self-rescheduling events. The controller keeps
their state per placement and runs them from the native event scheduler's dispatch:
the scheduler singleton `0x75d340`'s vtable `0x68de14` slot `+0xc` (`0x4b2d60`), which
the game update calls once per frame with the game time (`0x46f0e9`). One pointer is
replaced through the shared install transaction; the native dispatch always runs first.
No DLL event enters the native scheduler and nothing is saved: a zone load recreates the
scanner from the authored placement, as the original constructor does.

| Original (XBE) | Behaviour kept |
|---|---|
| `0xa8600` / `0xa83e0` | defaults visrange 600, minspeed 0, mintime 0.75, visextent 30, yaw/pitch extent 45, turnrate 20 deg/s, team hero; the one authored `resetdelay` fills both the track-loss timeout (+0x2d4) and the sweep pause (+0x2d0); unauthored (line value -1) they stay 10 and 3; `turndelay` is never read |
| `0xa9000` | acquisition: off unless enabled (XBE flag bit 19 = PC bit 20); scanning reschedules after 1.0 s, tracking after 0.1 s; up to four team-filtered candidates in the vision cone, nearest first, living, then the nearest one in line of sight; seen time accumulates the event interval; scanning -> tracking at `mintime`; tracking -> scanning after `resetdelay` unseen |
| `0xa8a90` / `0x120b00` | rotation every 0.01 s (1.0 s idle): sweep to rest yaw +- yawextent in the sweep direction, pause `resetdelay` at each end; tracking turns the shorter way to the target, pitch clamped to rest +- pitchextent and yaw to the nearer limit when outside the extents; arrival is a squared angle error under 0.001 |
| `0xa9a40` / `0xa9960` / `0xa99e0` | firing: polls every 1.0 s; while tracking and seen within the last second it arms a burst of 20 weapon fires after 1.0 s; each fire reschedules after the weapon's returned recoil (`0xbcba0` returns prototype +0x1b4) |
| `0xbb6f0` / `0x5a090` | a projectile weapon's fire spawns `count` projectiles fanned about the muzzle yaw: k from the fan centre, yaw - k x spread degrees, start + k along the right vector, speed x (1 + 0.1 k) |
| `0xbcc47..0xbcc99` | direct damage: one integer draw in trunc(min x 0.7f)..trunc(min x 1.3f) per weapon fire, from the native gameplay random stream |
| `0xb9e40` | muzzle: the weapon's `Gun<emitter+1>` bolt of the turret model carried by the turret's frame (a turret's one weapon is emitter 0, `Gun1`); aim (no character source, `0xba1d2..`): the bolt frame turned by a random yaw then pitch, each uniform in +-accuracy (the reader `0xbc3e3` stores degrees x 0.0174533) |
| `0xba420` / `0xba850` | bullet / flame: one instant ray per fire from 36 units behind the muzzle to `range` ahead (flame: swept with the weapon `radius` as extents); the first entity crossed takes the hit when it is an actor, by its own take-hit (`vt+0xa8(attack, 1, 1, 1)`) - no to-hit roll on this path (the roll `0x5c240` belongs to the hit service `0x5c6c2`, which a bullet never calls); a scan turret's fire within `tracetime` of its last trace (+0x364) traces nothing; muzzle, tracer and impact effects |
| `0xa8a90` (sound) | the turret's rotatesound loops at its position while it turns (volume 1, range 2 x visrange, inner visrange x 0.5) and stops on arrival; each weapon fire plays the weapon's firesound (`0xbcba0`) |
| `0xa9a80` | after every weapon fire: an AI danger notice at the aim point (team neutral, radius 70, recoil + 0.5 s) |
| `0xa91c0..0xa91f8` | candidates slower than `minspeed` are skipped; a hero filter skips heroes with character bit +0x334 bit 0 |
| `0x96ba0` | homing: a homing projectile of a scanner source homes on the scanner's target; each update its direction becomes the velocity direction plus f x the unit direction to the target's origin, f = (10 + age) x 0.05 capped at 1.5, at unchanged speed (nozhoming keeps it level). PC `projectileent` has no in-flight homing (only `guidedprojectileent`), so the controller steers the turret's own homing projectiles once per dispatch (velocity +0x120, setter `0x473640`) |

PC services: candidate query `0x4ab6d0` (actor class `0x70b83c`, filter mode 2 on the
team bit, sphere of visrange, distance order `0x683ed0`); line of sight with the native
sentry's test (`0x4b42c0..0x4b4361`: obstruction class `0x778a4c`, swept segment, direction
order `0x68202c`, clear when nothing is found), from the scanner's own position (+0x20, as the
original ray at `0xa9590..`) to the candidate's centre - from the turret's centre the
haarp_ext04 turret never saw a hero below it;
projectile service `0x450030` vt+0x28 (`0x4510a0`, one projectile per call: description
origin +0, direction +0xc, entity key +0x1c, speed +0x24, count +0x2c) with an attack from
`0x429860` owned by the turret's handle - its contact damage reaches a hero through the
native collision and damage path; projectile definitions from the weapon's entity file, registered once
(`0x564a80`/`0x564eb0`, definition manager `0x4625c0` vt+4). Placement binding uses the
authored position and name (`haarp_ext02` also has a waypoint named `tank_turret`).
Ray weapons: the PC beam's trace (`0x45237d..`: physent query, filters (1, 1) and (2, 0xe) - XML1's
(1, 1) and (2, 0xd), PC flag bits one higher - swept segment vt+0x74, contact order `0x68d684`,
contact record +0x30 point, +0x20 normal) without its hostility filter, the sentry line-of-sight
test from the muzzle to the contact (walls stop the ray), and the hit by the target's own take-hit
`vt+0xb0(attack, 1, 1, 1)` - the call the projectile contact `0x49c0d0` makes for an owner that is
not a character - with an attack from `0x429860` (turret handle, the draw, type 5, damage type at
+0x10, damagemods at +0x14) and its point +0x40, direction +0x4c and normal +0x58 written after
the constructor, which zeroes them (`0x4539a0`). Missiles carry the weapon's damage type and
damagemods too (the original builds one attack per fire for every kind, 0xbcc51 / 0xbcc8e). Effects: manager
`0x407750` (singleton `0x6f3c50`, vtable `0x68190c`) vt+0x1c resolves a path in the zone group
11, vt+0x34 plays it at a point along a direction; a rebound turret resolves again. AI notice: AI
manager `0x5252e0` vt+0x240 (`0x523c50`), neutral team 0x1c, as the harm notice `0x439391..`.
Sounds: sound system `0x592480` (vtable `0x69d1ac`) vt+0x38 resolves a name (none = `[0x69d058]`), vt+0x4c starts
a 3D loop, vt+0x74 stops it; the fire sound plays on the turret through entity vt+0x8c (the event
sound's plain play `0x4e8a70`). Hero filter bit: PC character +0x3d8 bit 0, the bit the take-hit `0x438a10` tests where the
original's `0x46270` tests +0x334 bit 0.

Not mapped: the XBE awake bit (+4 bit 15, scanning only while set unless tracking) is
treated as set - the original's bit 15 is the in-world flag (set and cleared through the
entity's virtual at `0x66d70`; clearing it takes the model out of the scene, `0x74ee0`), not
visibility (bit 14), so a turret scans while invisible as long as it is enabled; the turret's scan
animation (`0x76740`), the candidates' status-effect 10 exclusion (`0x2e280`) and the original line of
sight (a ray to visrange whose first entity must be the candidate) instead of the sentry test.
Beam weapons are not emitted (no turret uses one). The XML1 missile's `explodradius` is not an attribute either
engine reads (the original reads `exploderadius`), so its area damage is 0 in both; the damage
is the direct contact draw.

`xml2_xml1_controllers_test` also checks the turret rules (parsing, angles, sweep, acquisition,
burst cadence, fan, damage draw, homing). Live, on an own build with staged entry: the
haarp_ext02 turret swept, tracked, armed its burst at the tracking transition, fired 1.0 s
later and every 5.0 s after, killed a non-invulnerable hero with one homing salvo, was
destroyed by real attacks (deathscript ran) and stayed destroyed across Save, process restart
and Load; the haarp_ext04 turret stayed idle while its setup script kept it disabled and then
acquired and hit. Ray weapons, live with a staged zone load (enemy AI frozen, the hero walked in
with real input unless named): the Arbiter arb2_3 tripod swept +-45 deg at 20 deg/s with 2 s end
pauses, tracked, fired 1.0 s after tracking at the recoil cadence, took a non-invulnerable hero
80 -> 58 -> 26 -> 0 (also hitting a frozen soldier standing in the line of fire), left an
invulnerable hero unharmed, and was destroyed by real attacks; the four weapon_x wx1_3 tripods
(hero placed on their level, staged) acquired together and killed the hero within a second; the
Hive turret1 that tank1.py spawns on the stopped tank (the hero walked into the trigger, the real
script and conversation ran) bound by name, flamed the hero every 0.45 s (tracetime) for 6-11, and
took real jump-attack damage (500 -> 428, not destroyed within the test); the HAARP missiles leave
from the Gun1 bolt and still killed the hero with one homing salvo. With a seated party (Cyclops
player, three AI-controlled teammates, `seatParty`, staged): the AI teammates (+0x3d8 0x41/0x43)
passed the cone and range test but were skipped, the player (0x60) was taken; with the player
out of the cone and the frozen teammates in it the turret kept scanning for 40 s. All four Hive
turrets (tank1-4 triggers walked into, real scripts and conversations) were destroyed by
Cyclops' optic blasts; turret_death counted the objective to 4/4; turret1, destroyed before a
save, stayed gone after a cold restart and Load (no rebind, objective 1/4 kept, damaged model
on the tank). The arb2_2 and arb3_2 tripods bound only once the hero came near (they are out
of the world while far from the heroes; a turret whose region leaves the world when the active
hero changes is unbound and rebound later) and killed the hero.

Online play: every peer runs the controller from the same lockstep scheduler dispatch,
with game time and the gameplay random stream only. Rule 4 revision 2 registers
`{1, placements, FNV-1a-32 of every applied line in order, 0}`; a requested but
uninstalled controller registers unverified (host/join closed); a process that does
not request it keeps revision 1 `{0,0,0,0}`, as 1.4.0. A zone change or a game clock that
goes back (the online session restarts the clock, 0x61413d / 0x613f16) clears every binding
timer, so a peer that played earlier in the process binds as soon as a fresh one does.
Not yet checked with two instances (integrator): the peers must agree on (1) the hero filter,
i.e. that +0x3d8 bit 0 of every hero reads the same on both peers (a remote player's hero must
not read as AI-controlled on the other peer), and (2) when a turret's entity is in the world
(it is bound only then, and its scanner restarts at every binding), i.e. that leaving and
entering the world follows shared state (hero positions), not per-peer state such as the
local camera.


## Magneto and Blob (wave three, runtime acceptance pending)

Magneto binds `magneto` in `astroid_m/visit1/asteroid2_1`; Mystique in that zone retains its own generation-checked
identity. Rule 40 adds `magneto:1;` at bit 64 and `blob:1;` at bit 128. Dopple option bits and rule 3 are unchanged.
Only the existing dispatcher is detoured. Two additional observations join its startup transaction: Magneto hurt
powerups at `0x54f900` and Blob object metadata at `0x4989a8` (separate from LiftRule's `0x498991` clamp hook).
The metadata store is bounded by the installed World capacity and checks the complete generation-bearing handle.

Magneto preserves the XML1 priority order: pending shield return, pending balls off, pending radial, existing tiles,
environmental attacks, power smash, power attack, close melee and pursuit. Successful node lookup consumes a pending
request; missing nodes leave it pending. Phase requests change flags and intervals without changing existing attack
deadlines. `mag4` queues shield RETURN; repeated `magshield` requests double the current intervals again. Five tile
slots keep their initial target distances, with native motion stop and activation when motion ends or distance grows.
General attacks renew their deadline by five seconds; tile preparation/retries use .3/.2/.1 seconds. Native camera,
query order, activation and movement services remain authoritative. The environmental query sees the existing brain
target before the handler assigns a new one.

Shield protection comes from the authored `balls_return` animation event and `balls_off` removal. No helper-death
count is synthesized. The hurt adapter queries actual `def_damage` scale (kind 39, `0x550410`) before emitting the
owner's shield-hit FX, at most once per .25 seconds. It does not infer protection from phase requests or tile state.
The native danger notice was located: XML1 manager `+0x22c` maps to PC `+0x240`, `0x523c50 -> 0x5044f0`. It preserves
position, radius, .5-second life and null source, translates team none 26 to 28, and supplies zero for PC's two added
response-policy bytes. The effect of those two policy bytes on companions still needs live comparison.

Blob binds `blob` in `nyc/alison/nyc1_1_3`, `mansion/dr_mag2/mag_nyc4` and `astral/ast3/astral3_5`. Its NYC and astral
programs run one opcode per update, including pickup/throw only in NYC. Unavailable actions hold their opcode.
Opcode 7 flops below 170 and slams at or above 170. Melee approach uses 100/140 hysteresis and a .97 facing threshold;
10/5-second timers use strict expiry. Both dormant attack opcodes and the default/no-op cases are implemented.
The prop query excludes the owner and XML1 flag sets 1/4/28/29/27 (PC 1/4/30/31/29), rejects flag 17, keeps the native
center-metric result order, checks lift eligibility and the six-unit height limit, and retains the earlier candidate
on equal weight. `xml1heaviness` wins only while its converted byte agrees, matching LiftRule's contract. Without
that metadata, legacy converted bytes cannot distinguish XML1 heaviness 0/1 or 4/5; ordinary native eligibility stays
in force. Query-pool exhaustion safely produces no prop and the authored .2-second advancement.

Blob's companion service schedules living AI heroes strictly inside 500 at +.3 seconds for slam and +.6 for flop.
On strict expiry it requires alternate slot 4, sets input word 1 bit `0x40`, native jump parameter `brain+0x2dc = 550`,
and waits .4 seconds. Unavailable requests expire after one further second. Static inspection also found the native
PC companion branch at `0x51698b`; this controller keeps separate per-companion deadlines instead of its shared clock.
Blob tethers retain both endpoint emissions. Astral health thresholds, pillar activation, armour removal and growth
remain script-owned. Owner handle, brain identity, zone, default-target initialization, death and clock rollback reset
controller state. All deadlines and random choices use simulation time and the gameplay RNG.

The companion builder emits the list only for finished map/name/script contracts. Magneto ON scripts and the
`shieldup` relay omit only SPEC 36's inserted protection statements; authored counters, timing and the original
stage-four relay spelling remain. An OFF build retains the substitute and generic Blob AI. A compatible wave-three
DLL is required with ON content. If requested Magneto preparation or the shared transaction fails, startup is refused;
the recovery is a compatible DLL or an OFF rebuild with the SPEC 36 fallback. No new save fields are introduced.

Verification (no game or xemu): build `xml2_fix`, `xml2_boss_wave3_test`, `xml2_boss_wave3_bridge_test`,
`xml2_xml1_controllers_test` and `xml2_gameplay_agreement_test` in Release/Win32, below normal priority, with MSBuild
`/nr:false`. Run the wave-three test with owned PC and XBE paths to check every guarded service and all 60 allocated
program DWORDs. The bridge test executes our production observation bridges with invented actors/readers, checking
stack cleanup, registers, FPU preservation and metadata generations. Neither fixture executes a game.

Live acceptance remains due:

- Magneto: exact initial/mag2/mag3/mag4/mag5/magshield flag traces; repeated shield interval doubling; strict attack
  deadlines within one eligible AI update; .5/1/.1 waits; missing node and full query pool behavior.
- Magneto: zero damage only during actual authored shield life; 75/50/25 health crossings and all helper/counter
  events; `mag4` shield return; hurt FX at .25-second boundaries; no all-helpers-dead shortcut or permanent shield.
- Magneto: general versus tile selection; native camera gates; all five slots, retry deadlines, wobble, collision
  notification, activation target and floor lift/removal; native danger responses with the added policy bytes zero.
- Blob: exact NYC and astral opcode traces, failed pickup/different carried object/aim holds, and successful throws;
  below/at/above 100, 140, 170, 180, 400 and 500, and height +/-6 (position uncertainty at most one world unit).
- Blob: slam radii 60/120/200 at .2/.25/.3, flop range 100 at .3, hit/knockback behavior; companion jump deadlines,
  slot failure, 550 parameter, .4 wait, expiry and control transfer; all five astral armour/pillar/growth cycles.
- Both: reload, same-zone respawn, transition between Blob's supported zones, owner/brain replacement, clock reset,
  dead or missing targets/props, OFF encounters, and installation failure. A matching wave-three DLL is mandatory.
- Multiplayer: two peers with different local heroes/cameras/resolutions must agree on selected handles, opcode,
  phase, RNG draws, damage, hazard events and companion jumps. Explicitly check `_ACTIVE_HERO_` in authored homing
  scripts and the camera predicates. No lockstep or live fight equivalence is claimed by the static tests.
- At least two matched original/PC runs per encounter with the same party/level; all phases observable and fight
  duration within +/-30%. No game or emulator was launched for wave three.
