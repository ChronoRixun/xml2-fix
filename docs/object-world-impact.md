# Ordinary object impacts against the world

`[Game] ObjectWorldImpact=xml1` restores the first game's zero ordinary impact
damage when the collision target does not resolve to a physical entity. An absent
key or `xml2` preserves native behavior. Gameplay rule 17 revision 1 registers only when requested, with effective values `{installed ? 1 : 0, 0, 0, 0}` and the installer verification result. OFF leaves the released digest unchanged; a failed request poisons agreement.

This covers every no-physical-target landing impact. XML1 uses coefficient-table entry zero for all of them, including ordinary world/floor collisions.

The first game's impact formula at `0x912b0` starts the weight-difference index at
zero and leaves it there for a non-physical target. Its coefficient table starts
with zero. PC `0x497900` follows the same classification but its table starts with
25. Both speed paths multiply by 1/480 before the coefficient. Consequently even
a small landing can damage an object on PC. A measured surviving punch reduced
a ten-health prop to five, followed by a separate 7.76-damage floor impact.

The guarded bridge at `0x497977` changes only the no-physical-target branch. It
consumes the existing FPU input and returns zero with the native callee cleanup.
A physical target replays the displaced heaviness load and resumes at `0x497983`.
The physical-target coefficients, authored throw-hit path, gravity, impulse,
health, damage thresholds, and save representation are unchanged.

Installation uses signature checks and the existing write/readback/rollback
transaction. A safe refusal leaves stock code; an unverifiable rollback refuses
startup. The test pipe reports the effective state. `xml2_world_impact_test`
executes both bridge branches repeatedly, checks FPU/stack behavior, guard
corruption, partial-write rollback, and failed-install agreement refusal. Pass a private
retail executable path to also check every native guard without launching it.

This does not correct an object already authored beneath its floor. The companion
builder change selects the verified shallow-overlap placements for the native
load-time floor sweep. Both changes are needed: placement alone can exchange a
fall-through for an immediate impact death.
