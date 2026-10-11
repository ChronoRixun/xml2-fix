# Experimental XML1 controllers

`Game.Xml1MasterMold` and `Test.NativeProjectileProbe` default to off. The builder
does not enable this work yet. The encounter and turret restoration are incomplete;
these switches are development opt-ins, not a released gameplay capability.

The Master Mold prototype runs through the existing default-AI update vtable
entry at `0x694994`. It consumes the existing pattern requests and drives native
shield/blaster nodes. The two-second shield wait and four-second core attack
window use game time, with a nine-second retry deadline. A generation-checked
core handle survives ordinary AI target clearing; the beam bridge corrects the
direction after the native muzzle calculation. Collision, damage, structure
requirements and death scripts remain native.

The authored spawn script's `setDefaultTarget` call resets transient controller
state before that script reissues the saved phase request. No DLL-owned event is
left in the native scheduler, and no new save field is introduced. A staged
combat fixture has exercised both health thresholds, native core deaths and a
Save Game -> process restart -> Load Game cycle with the shield and broken-core
state preserved. These checks do not certify an unassisted playthrough.

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
starting combat.

Until integration with the shared effective gameplay agreement, requesting
either controller or the projectile diagnostic closes host/join sockets for that
process, even outside LC mode. Failure to install that exclusion refuses startup.
Later INI edits cannot reopen the process. Shared rule IDs 3 (Master Mold) and 4
(turrets) are reserved upstream; future integration must register verified off,
verified installed, or unverified failure after installation, then finalize the
common record. This branch does not claim multiplayer authority or agreement.

`xml2_xml1_controllers_test` exercises selection counterexamples, transaction
rollback and the production socket gate with isolated providers and no sockets.
Remaining gates include full puzzle/fight/story completion, original party-attack
timing, original-game geometry comparisons, interruption/revisit behavior, and
the scanner/weapon controllers. The projectile diagnostic alone is not a turret
restoration.
