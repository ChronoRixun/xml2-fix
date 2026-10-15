# First-game lock-on effects

The builder supplies `[Game] Xml1LockOnEffects=beam,surface,target`, with three
extensionless paths relative to `effects/`. Missing, empty or `0` leaves the PC
handlers untouched. An invalid triple refuses the entire install. The feature
requires a companion containing this key; its release version is assigned at
integration.

The Xbox `ch_lockon` update at `0xddf80` emits common effect 19 along the scan,
20 at a collision, and 21 on an accepted target. `ch_lockedon` at `0xddeb0`
continues effect 21 while the target is alive. The PC equivalents retain the
geometry and targeting but omit all four emission paths. Dormant effect
triggers in the authored powerstyles retain the three resource paths. Making
those triggers ordinary timed effects would lose the scan geometry and target
attachment.

Four guarded bridges restore the emissions:

| PC site | Observation |
| --- | --- |
| `0x4ef13f` | Failed trace: head origin and original scan direction |
| `0x4ef1fd` | Hit: native head origin, computed beam basis, collision point and normal |
| `0x4ef242` | Accepted target: target position and collision normal |
| `0x4eef29` | Retained live target: target position and computed orientation |

The bridges preserve registers and replay displaced instructions. With only
`Xml1LockOnEffects`, targeting stays native. Effects use the existing
loader/emitter and cosmetic random stream 0; the visual callbacks do not draw
from gameplay stream 1.

`[Game] Xml1LockOnAim=1` additionally changes the head query argument at
`0x4eefb1` from approximate to animated, in the same transaction. The PC
helper `0x568e90` uses a fixed 56-unit head translation when that argument is
true; its false branch queries the actual skeleton. XML1 `0x12cad0` always
queries the skeleton. The approximation leaves pitch and roll zero, so the
retained scan points down rather than following the animated head. This is
why restoring only the emissions did not restore natural acquisition.

The builder enables both keys. Animated aiming affects target selection and
requires a **new gameplay rule ID assigned by the integration owner**. None
is assigned here: `reject_unregistered_rule("Xml1LockOnAim")` makes online
admission fail closed whenever aiming is requested, including a refused
install. Offline play remains available. Rule IDs, protocol and save format
are unchanged. This is an integration blocker, not a release-ready online fix.
Resource handles are loaded once and their native generation is checked before
each emission. Definitions invalidated by native teardown are reloaded; the
engine may retain others across a zone change. This also avoids incrementing
the loader's 16-bit use counter every frame. Both stock and published raised
query accessors are supported; unknown accessor hooks refuse installation.

All dependencies are checked before a single transaction writes the four
visual sites and, when requested, the aiming argument. The transaction verifies
readback and rolls back on failure. Uncertain
rollback or page protection restoration refuses startup. Otherwise a refused
install retains stock behavior and logs one explanation.

`[Test] LockOnEffectsTrace=1` logs each restored emission, handle, owner,
position and direction. It is off by default. The native test executes all
four bridges with synthetic frames, tests contract rejection and every-site
rollback, and optionally checks all signatures against a locally supplied
retail executable:

```
xml2_lockon_effects_test.exe <local-XMen2.exe>
```

Live coverage distinguishes ordinary AI scan, acquisition, missile, beam and
gas emissions from staged locked-target and swipe diagnostics. A complete
ordinary fight and online agreement remain separate acceptance checks.
