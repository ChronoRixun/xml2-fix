# Weapon brain investigation

The withdrawn recoil-picker candidate did not restore the original weapon AI.
In its zero-shot observation the officer closed from approximately 308 to 64
units in 1.6 seconds and entered melee. No admission callback was observed in
that measurement window. Therefore that particular zero cannot be attributed
to a recoil denial: the missing ranged decision path is upstream of the filter.
Earlier release-profile observations alternated between one single shot and one
seven-shot burst followed by melee. Those overlapping runs were diagnostics,
not a matched causal rate comparison.

The original initializer at `0xfd330` selects a weapon brain independently of
the NPC's ordinary AI class, excluding absent weapons and melee-category weapons.
It reads the range from the weapon prototype (`0xfd3ff`) and constructs the
weapon brain (`0xee060`). The officer has class zero and range 550. The converted
PC actor instead had the basic brain and range 90.

PC `0x518090` retains the corresponding ranged constructor and state layout.
Its primary method, `0x519e00`, retains common target/navigation/close-combat
logic, but lacks the original weapon-specific branches after the near-target search.
The original non-bullet branch starts at `0xefc4a`; its held fire input is
consumed by the separate gate at `0x42f60`, and handler entry `0xe14d0` arms
the actor's shared recoil deadline. Handler chain callback `0xe1530` is also
different from the default handler. A generic power-slot cooldown is insufficient.

The PC parser accepts `genericranged`, `genericrangednostraffe`, `rangedenemy`
and `attackrange`. These cannot express the removed weapon input and firing
branches. Changing the ordinary AI class also loses the original independent
weapon selection. Compatibility metadata and a guarded engine path are needed;
normal builder output must remain unchanged until acceptance succeeds.

## Complete source holder audit

There are 28 NPC definitions with weapons: 20 select weapon brains, eight have
melee weapons and do not. The separate turret controller and style-named boss
weapon events do not establish an NPC weapon-brain binding. No map declaration
in the audited source overrides the officer's equipped weapon.

| Weapon family | NPC definitions | Range | Distinct path |
|---|---:|---:|---|
| MP5 | 4 | 550 | Bullet aiming, rapid fire |
| Laser | 3 | 550 | Bullet aiming |
| Super laser | 2 | 550 | Bullet aiming |
| Freeze gun | 1 | 550 | Discrete projectile, recoil 0.9 |
| Nullifier | 2 | 550 | Warm-up 0.5 |
| Lightning | 2 | 550 | Continuous, warm-up 0.6 |
| Flamethrower | 3 | 275 | Continuous, warm-up 1 |
| Knockback gun | 2 | 550 | Warm-up 0.5 |
| Pistol | 1 | 550 | Different primary handler |
| Melee gloves and batons | 8 | 64 or 85 | Excluded by original initializer |

The original default burst duration is 1 second when omitted (`0xb9c45`).
The non-bullet brain has its own firing/pause state; recoil is not that state.
Bullet aiming, charge effects and continuous firing need separate acceptance.
An officer result must not be generalized to those nineteen other definitions,
or to the unarmed enemy cohort from the original attack-frequency investigation.

## Agreement

Rule 16 already describes the restored NPC power, combo and decision behavior.
The recoil candidate extended that rule to revision 6 with an installed bit and
a source-metadata fingerprint; it did not allocate a new independent rule ID.
Revision 5 belonged to an earlier withdrawn role experiment. Restoring the
weapon brain changes revision-6 semantics, so the new candidate uses revision
8 of the same rule and fingerprints recoil, range, burst duration and leader flag. This
rejects old candidates even when actor names and recoil happen to match.
OFF retains the released rule revision/values and installs no weapon hooks.

Live acceptance and final scope are recorded in the companion candidate notes;
the static evidence alone does not establish shooting parity.

The first brain candidate (revision 7) filtered the picker but retained the
converter-added gun capabilities in the AI cache. Revision 8 removes those
capabilities at refresh, using the native resolved-action table, and recomputes
the aggregate mask while preserving authored powers. The native random picker
is no longer hooked. Source-authored gun AI attributes require a separate
contract and are rejected by the exporter.
