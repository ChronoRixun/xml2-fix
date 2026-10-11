# XML1 Master Mold controller and turret experiment

`Game.Xml1MasterMold` and `Test.NativeProjectileProbe` default to off. The first-game
builder enables Master Mold only with its generated, validated saved-stage binding.
The projectile diagnostic remains a development opt-in. The turret scanner and
weapon controllers are not implemented.

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

Until integration with the shared effective gameplay agreement, requesting
either controller or the projectile diagnostic closes host/join sockets for that
process, even outside LC mode. Failure to install that exclusion refuses startup.
Later INI edits cannot reopen the process. Shared rule IDs 3 (Master Mold) and 4
(turrets) are reserved upstream; future integration must register verified off,
verified installed, or unverified failure after installation, then finalize the
common record. This branch does not claim multiplayer authority or agreement.

`xml2_xml1_controllers_test` exercises selection counterexamples, transaction
rollback and the production socket gate with isolated providers and no sockets.
Live traces show the three independent floor attacks recurring at their native
ten-second deadlines, automatic core destruction, saved phase reconstruction and
ending progression. Further acceptance includes original-game visual comparisons,
broader interruption/revisit cases and the scanner/weapon controllers. The
projectile diagnostic alone is not a turret restoration.
