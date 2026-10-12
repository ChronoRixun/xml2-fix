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
from the Gun1 bolt and still killed the hero with one homing salvo. `xml2_xml1_controllers_test` exercises selection counterexamples, transaction
rollback and the production socket gate with isolated providers and no sockets.
Live traces show the three independent floor attacks recurring at their native
ten-second deadlines, automatic core destruction, saved phase reconstruction and
ending progression. Further acceptance includes original-game visual comparisons,
and broader interruption/revisit cases.

## XML1 scan turrets

`Game.Xml1Turrets` defaults to off. The first-game builder enables it with one
`[Xml1Turrets]` line per placement (`Count`, `T1`..`T32`):
`zone|instance|px,py,pz,rx,ry,rz,mx,my,mz|visrange,minspeed,mintime,resetdelay,visextent,yawextent,pitchextent,turnrate,team|kind,recoil,count,spread,speed,accuracy,range,damage_lo,damage_hi,tracetime,radius,damagetype,damagemods|projectile|entfile|muzzlefx,muzzleaccfx,impactfx`,
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
| `0xa8600` / `0xa83e0` | defaults visrange 600, minspeed 0, mintime 0.75, visextent 30, yaw/pitch extent 45, turnrate 20 deg/s, team hero; the one authored `resetdelay` fills both the track-loss timeout (+0x2d4) and the sweep pause (+0x2d0); `turndelay` is never read |
| `0xa9000` | acquisition: off unless enabled (XBE flag bit 19 = PC bit 20); scanning reschedules after 1.0 s, tracking after 0.1 s; up to four team-filtered candidates in the vision cone, nearest first, living, then the nearest one in line of sight; seen time accumulates the event interval; scanning -> tracking at `mintime`; tracking -> scanning after `resetdelay` unseen |
| `0xa8a90` / `0x120b00` | rotation every 0.01 s (1.0 s idle): sweep to rest yaw +- yawextent in the sweep direction, pause `resetdelay` at each end; tracking turns the shorter way to the target, pitch clamped to rest +- pitchextent and yaw to the nearer limit when outside the extents; arrival is a squared angle error under 0.001 |
| `0xa9a40` / `0xa9960` / `0xa99e0` | firing: polls every 1.0 s; while tracking and seen within the last second it arms a burst of 20 weapon fires after 1.0 s; each fire reschedules after the weapon's returned recoil (`0xbcba0` returns prototype +0x1b4) |
| `0xbb6f0` / `0x5a090` | a projectile weapon's fire spawns `count` projectiles fanned about the muzzle yaw: k from the fan centre, yaw - k x spread degrees, start + k along the right vector, speed x (1 + 0.1 k) |
| `0xbcc47..0xbcc99` | direct damage: one integer draw in trunc(min x 0.7f)..trunc(min x 1.3f) per weapon fire, from the native gameplay random stream |
| `0xb9e40` | muzzle: the weapon's `Gun<emitter+1>` bolt of the turret model carried by the turret's frame (a turret's one weapon is emitter 0, `Gun1`); aim (no character source, `0xba1d2..`): the bolt frame turned by a random yaw then pitch, each uniform in +-accuracy (the reader `0xbc3e3` stores degrees x 0.0174533) |
| `0xba420` / `0xba850` | bullet / flame: one instant ray per fire from 36 units behind the muzzle to `range` ahead (flame: swept with the weapon `radius` as extents); the first entity crossed takes the hit when it is an actor, by its own take-hit (`vt+0xa8(attack, 1, 1, 1)`) - no to-hit roll on this path (the roll `0x5c240` belongs to the hit service `0x5c6c2`, which a bullet never calls); a scan turret's fire within `tracetime` of its last trace (+0x364) traces nothing; muzzle, tracer and impact effects |
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
+0x10, damagemods at +0x14, point +0x40, direction +0x4c, normal +0x58). Effects: manager
`0x407750` (singleton `0x6f3c50`, vtable `0x68190c`) vt+0x1c resolves a path in the zone group
11, vt+0x34 plays it at a point along a direction; a rebound turret resolves again. AI notice: AI
manager `0x5252e0` vt+0x240 (`0x523c50`), neutral team 0x1c, as the harm notice `0x439391..`.
Hero filter bit: PC character +0x3d8 bit 0, the bit the take-hit `0x438a10` tests where the
original's `0x46270` tests +0x334 bit 0.

Not mapped: the XBE awake bit (+4 bit 15, scanning only while set unless tracking) is
treated as set; scan/rotate loop sounds, the fire sound and the turret's scan animation
(`0x76740`), the candidates' status-effect 10 exclusion (`0x2e280`) and the original line of
sight (a ray to visrange whose first entity must be the candidate) instead of the sentry test.
Beam weapons are not emitted (no turret uses one). The XML1 missile's `explodradius` is not an attribute either
engine reads (the original reads `exploderadius`), so its area damage is 0 in both; the damage
is the direct contact draw.

Online play: every peer runs the controller from the same lockstep scheduler dispatch,
with game time and the gameplay random stream only. Rule 4 revision 2 registers
`{1, placements, FNV-1a-32 of every applied line in order, 0}`; a requested but
uninstalled controller registers unverified (host/join closed); a process that does
not request it keeps revision 1 `{0,0,0,0}`, as 1.4.0.
