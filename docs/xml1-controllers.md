# XML1 Master Mold controller

`Game.Xml1MasterMold` defaults to off. The first-game builder enables it only with
its generated, validated saved-stage binding. No turret scanner or weapon
controller ships in this build.

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
Rule 4 (turrets), revision 1, is always verified `{0,0,0,0}`: no turret controller
exists in this build. Failure to install a required socket exclusion refuses
startup.

`xml2_xml1_controllers_test` exercises selection counterexamples, transaction
rollback and the production socket gate with isolated providers and no sockets.
Live traces show the three independent floor attacks recurring at their native
ten-second deadlines, automatic core destruction, saved phase reconstruction and
ending progression. Further acceptance includes original-game visual comparisons,
and broader interruption/revisit cases.
