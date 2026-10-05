# The first game's rule for breaking objects

`[Game] BreakRule=xml1` makes an object break only to an attack that would have broken it in X-Men Legends I. An absent key or `xml2` leaves the game untouched; any other value is logged and ignored. The key is for a mod that carries the first game's objects and attacks (the X-Men Legends I port sets it); XML2 as it ships has no use for it.

## The two games' rules

Both games give an object a `structure` and an attack a damage level, and refuse a hit whose level is below the structure.

| | X-Men Legends I | X-Men Legends II |
| --- | --- | --- |
| Structure | 0 to 10; 10 is never broken | clamped to 0 to 2; 2 is never broken |
| A melee hit's level | the attack's authored level + 0 / 3 / 6 / 8 for Might 0 / 1 / 2 / 3 + the `damageLevel` affecter, at most 9 | the attack's authored level + the `might_structure` affecter + the `damageLevel` affecter, at most 1 |
| A plain punch | 1 | 1 |

In XML2 a punch therefore breaks everything that can be broken at all. A port of the first game has to choose between keeping its sturdier objects unbreakable (structure 2) and letting a punch break them (structure 1); the port's builder maps the first game's 2 to 9 onto 1, which opens the routes that need a wall broken and loses the difference between a punch and a power.

Neither number can simply be widened. A character takes a hit only when the hit's level is above the character's own structure byte, and about thirty places test the structure byte for 2 (targeting, AI, the character's hit test), so both bytes stay exactly as the game has them.

## What the key does

Three hooks, in `XMen2.exe` only, after every byte they rely on is checked against the retail build (image base `0x400000`):

| Address | What is there | What the fix does |
| --- | --- | --- |
| `0x498970` | the object has read its `structure` and clamps it | asks the same reader for `xml1structure` and remembers the number by the object's handle; then the game's clamp runs as before |
| `0x685a98` | slot `0x34` of the combat object's vtable: the melee level function `0x44f770` | notes the attack's authored level, calls the game's function and returns its result unchanged; remembers the first game's level for that attacker and attack |
| `0x49805b` | the last test of the object's damage gate | when the game is about to let the hit through, refuses it if the first game's level is below `xml1structure`; the refusal is the game's own refused branch |

The feature only ever adds a refusal at that gate. A hit the game refuses (an invulnerable object, structure byte 2, a level below the structure byte) stays refused, and the game's exceptions (exactly 1,000,000 damage, the direct damage type) and its effects for a refused hit are the game's.

**The object's number.** `xml1structure="N"` (0 to 10) is an attribute of the entity definition, next to `structure`. The game reads entity attributes by name and never lists them, so nothing else sees it. The fix uses it only while the object's structure byte is what the port's conversion gives for that number (0-1: 0, 2-9: 1, 10: 2): if a script changes the byte later, the object goes back to the game's rule rather than an outdated number. An object without the attribute follows the game's rule.

**The attack's number.** The game stores an attack's authored `DamageLevel` unclamped and copies it into the hit; only the melee sweep's cap at 1 loses it. In a build that carries the first game's attacks, the byte the sweep is about to cap is the first game's authored level. The fix reads it there, adds the first game's share for Might (from the lift value the `might_heaviness` affecter sets, which is the port's Might number: the first game uses one Might number for lifting and for breaking) and the `damageLevel` affecter, and caps the sum at 9. A hit that never went through the melee sweep (a projectile, a beam, a blast) still carries its authored level, and that is what it is judged by.

The level applies to whoever attacks. The first game's enemy data gives many soldiers the `might` talent at rank 1, so their melee is level 4 there and here.

## What does not change

- The hit's level byte and every structure byte: what a character, the level-scaled effects, targeting and AI read is what it was. Ordinary attacks do the damage to enemies they did before.
- Saves: nothing the fix keeps is saved, and the attribute is zone data read again whenever an object spawns. A save made with the key loads without it and the other way round.
- Co-op on one PC: the rule is per attacker and per object. Online play adds nothing to any packet; both players need the same build and the same ini (not tested online).
- The heaviest objects: the first game lets Might rank 3 lift its heaviness 4. XML2 never lifts its top heaviness, which the rest of the engine also treats as immovable; that is a separate limit and is not part of this key.

## Checking it

`xml2-fix.log` says whether the rule is on and, when a guard fails, which one. The test pipe's `status` ends with `break rule xml1; object hits judged N; refused N; without xml1structure N` (`xml2` without the key, `unavailable` when a guard refused). `[Debug] LogBreakRule=1` logs every melee level worked out and every hit judged, with the attacker, the object, both numbers and the verdict.

The rules (the level sum, the pairing, the decision, what is remembered, the guards and both bridges run for real) are in `src/break_rule_rules.hpp` and are checked by `xml2_test` without the game; with a copy of `XMen2.exe` at hand it also checks every guard, the three writes and the game's own level function. New in 1.3.2.
