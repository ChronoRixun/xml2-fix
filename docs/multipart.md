# Breakable parts (`[Game] Xml1Multipart`)

X-Men Legends I gives some characters breakable parts: a stats `<Multipart>` row with its own health, a death
effect, a looping effect and a link to the attacks that part carries (Juggernaut's helmet, the Sentinels' arms).
XMen2.exe kept the part bookkeeping but cut everything else: its parser (`0x48cec0`) reads only `NonMenuOnly`,
`hideSkin`/`showSkin` and `hideSkin2`/`showSkin2`, and every spawned character is drawn in the "broken" look
(slot 3, `0x48cbc0`, hides `hideSkin` and shows `showSkin`). `[Game] Xml1Multipart=1` puts the first game's rules
back. Absent or `0`: nothing is patched. The X-Men Legends I port (builder `fix_ini`) sets it.

## The first game's rules (XBE)

| What | XBE | Here |
|---|---|---|
| Manager | getter `0x89fc0`, vtable `0x3cf6e4` (14 slots), 13 records of `0x188` | XMen2.exe's manager: getter `0x48d1f0`, vtable `0x689864` (6 slots), 13 records of `0xb8` |
| Parser | `0x89500`: `bone`, `health`, `effectOnDeath`, `linkToPower`, `tolerance`, `loopEffect`, `effectBone`, `hideSkin`(2), `showSkin`(2); a part needs `health`; a fifth part is refused | the native parser, then the XML1 attributes read from the same node (`0x5649c0`); `health > 0` marks an XML1 part (XMen2.exe's own `health="0"` rows stay native). `startHide`, `healthMul` are read by neither game; `tolerance` is parsed by XML1 and never used |
| Spawn look | `0x33cd0` -> slot 5 (`0x89420` -> `0x88a40`): every part's `hideSkin` shown, `showSkin` hidden | after the native slot 3: the same for XML1 parts (`0x48c700` swapped) |
| Damage | `0x43fa0` (the deflect stage) ends with slot 4 (`0x899c0`): the hit's damage goes to the **current** part (record `+4`, 1-based, starts at 1); no hit location. A part already at or below 0 moves the current part on. Taken below 1: broken only if `0x1233c0(0, 2)` is 0 (one in three), else spent unbroken | the CALL of the deflect stage `0x435440` at `0x438b0f`; deflected hits (damage zeroed there) are skipped as in XML1; the roll is XMen2.exe's integer RNG `0x5601e0` on the damage stream (1) |
| Break | `0x890e0`: `effectOnDeath` and `loopEffect` on `effectBone`, `hideSkin`/`hideSkin2` hidden, `showSkin`/`showSkin2` shown (with `_outline`), the part's power bits disabled, current part moved on (`0x88760`) | effects through spawnEffectBone's own path (CFxManager vt+0x1c / `0x575da0` / `0x41d710` / vt+0x34); segments through `0x48c700` |
| Power link | `linkToPower` names (`strstr`, `0x88bd0`) -> bits 1 `meleeAttack`, 2 `meleeSmash`, 4 `powerAttack`, 8 `powerSmash`, 0x10 `powerMove`, 0x20 `powerAction`; the AI attack handler (`0xeea60`, vt+0x20 = `0xee5c0` -> slot 7 `0x89440`) does not press a disabled attack's buttons (`0x452c78`: 0x10, 0x20, 0x80+0x10, 0x80+0x20, 0x80+0x40, 0x80+0x100) | the six AI brain update slots (`0x694c60`, `0x695418` = `0x517370`; `0x6953f0` = `0x51d610`; `0x695440` = `0x51d880`; `0x695388`, `0x695468` = `0x51cad0`) clear those buttons from the brain's output after the native update |
| Persistence | no save path touches the manager: a respawned or reloaded character has whole parts | the same (state lives with the clone); no save-format change |

Not reproduced: the XBE re-spawns a part's `loopEffect` each time it ends (`0x88aa0`, from the actor update
`0x3ae60`), at the *current* part's `bone`. Here it is spawned once at the break; whether XMen2.exe's effect keeps
looping on its own is an in-game question (see the port's report). `destroyMultipartPiece` (script, `0x9ce30` ->
slot 13 `0x88fa0`) has no XMen2.exe function; the builder emulates it with `setSegmentVisible` +
`spawnEffectBone`, which does not touch this module's part state.

## Online

Part damage, the break roll and the disabled attacks change the lockstep simulation, so shared gameplay rule 6
carries `{installed, 1, 0, 0}` (see [gameplay-agreement.md](gameplay-agreement.md)).

## Install

One `xml1_hook_transaction`: four manager vtable slots, one CALL operand, six brain vtable slots, after 15 guards
(called functions and the manager vtable). A guard mismatch or a refused transaction leaves the native behaviour
with one log line; a failed rollback refuses startup. `[Test] MultipartTrace=1` logs every spawned XML1 part owner,
every part hit (damage, current part, part health, the roll) and every refused attack.

## Checked in game (port build, windowed harness, real melee input; party and XP staged)

- Without the key (1.4.0): Juggernaut (Muir, `JuggernautAct3`) fights bare-headed and the Sentinels (mansion
  front yard) have no arms from the moment they land - XMen2.exe's spawn look; nothing ever breaks.
- With the key: Juggernaut wears the helmet; hits drain it (400), a spent helmet without the roll stays on (seen
  four times), and on a 0 roll it flies off (`explode/JuggernautHelmet`) and his head shows. The Sentinels land
  with both arms; arms break on real hits (`explode/smexp1` at the shoulder, the arm gone), and the trace shows
  the brains' power-smash (0xA0) and power-action (0x180) presses refused for a Sentinel without its right arm.
