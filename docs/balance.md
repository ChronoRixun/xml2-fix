# [Balance]: X-Men Legends I's combat numbers

`[Balance]` in xml2-fix.ini restores, key by key, the parts of X-Men Legends I's combat arithmetic that XMen2.exe
does differently on the same character data. It is for a mod that carries the first game's characters (the X-Men
Legends I port, whose builder writes the section); each key is on its own, a key that isn't set leaves that part of
the game exactly as it is, and a key whose retail bytes aren't there is refused with a line in `xml2-fix.log`. The
rules, every byte relied on and every byte written are in `src/balance_rules.hpp`, checked by `xml2_test`
(`[Balance] (X-Men Legends 1's combat numbers)`).

Addresses are the retail, unpacked XMen2.exe at image base `0x400000` (SHA-256
`146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b`) and X-Men Legends I's `default.xbe` (Xbox, 2004)
as loaded at `0x10000`. No executable or game-derived output is distributed.

## What the two games do

| quantity | X-Men Legends I (default.xbe) | this engine (XMen2.exe) | key |
| --- | --- | --- | --- |
| Max health | 25 + 5 level + **10** body (`0xafa90`), x the entry's health scale (0.8 unless set) and the player count for a character not on the hero team, rounded up | the same with **3** (`0x4b8850`; the float at `0x6d9770`, read at `0x4b89f7`) | `EnemyHealthPerBody`, `HeroHealthPerBody` |
| Stat bonus on a hit | roll + trunc(**0.1** x stat x roll); mind for a mental or telekinetic attack, strength otherwise; **none on a power attack** (`0x5cca0`) | roll + ceil(**0.05** x strength x roll) (`0x4b7940`) or ceil(**0.01** x mind x roll) (`0x4b7970`), only when the attack's `DamageScale` is `normal` (`0x453a60`) | `StrengthBonus`, `MindBonus` |
| Might | the talent `might` at rank 1 / 2 / 3 multiplies every attack by 1.05 / 1.10 / 1.15 (`0x5cd5c`) | `might` only lifts | `MightBonus` |
| Power attacks | `PowerAttack="true"` in the style: no stat bonus, never rolled to hit (`0x5c3c0`) | the same switch under another name: `DamageScale="none"` (0) gets no stat bonus (`0x453b54`) and its misses don't count (`0x45333f`) - but it is still rolled, and a miss still loses its damage (`0x44f725`) and is blocked (`0x45355c`); `NpcPowerRule=xml1` (rule 16 revision 10) makes it never rolled, as XML1; the parser (`0x44ec00`, table `0x6d6dec`: none 0, normal 1, difficulty 2, "" 3) reads any other name as `normal` | `PowerAttacks` |
| Melee hit chance | (30 + (speed + level of the attacker) - (speed + level of the defender)) x 0.025, at least 10 % (`0x5c40c`); mind instead of speed for a mental attack | 3 A / (A + D) x attacker level / (both levels), A = 5 speed - 35, D = 3 speed, clamped 5 %..95 % (`0x44f450`); its overflow above 95 % feeds critical hits | `HitChance`, `HitChanceBase`, `HitChancePerPoint` |
| Rounding | the roll is a whole number (`0x1233c0`); the stat bonus and the damage affecters' bonus (their multiplier M and what they add, A, might's factor in M) are both worked out on it and rounded down: roll + trunc(0.1 stat x roll) + trunc(roll x (M - 1) + A) (`0x5cda2`-`0x5ce02`) | the roll is a float; hit = roll + ceil(stat bonus), then + ceil(hit x (M - 1) + A) (`0x453b81`, `0x453be2`) - on a 4-5 point punch with strength 3 and might 2, 7-8 against XML1's 5-6 | `Rounding` |

Scaled heroes: `HeroStatScale` is what the mod multiplied its heroes' four stats by (the port: 4; a level-up gives 4
points here and 1 there), `NativeStatHeroes` the herostat entries it didn't. A character is a scaled hero when its
stats came from the herostat - the object at CStats+0xc0 has the vtable `0x685294` (written at `0x44a436`; an
npcstat character's is `0x68511c`, `0x44983a`) - and its name (CStats+0x150) isn't in the list. Such a hero gets
`StrengthBonus` / `MindBonus` per `HeroStatScale` stored points (or `HeroStrengthBonus` / `HeroMindBonus` per stored
point), its speed or mind divided by the scale in the hit chance, and `HeroHealthPerBody` (the game's 3 without the
key) instead of `EnemyHealthPerBody`. An allied NPC and an enemy copy of a hero are npcstat characters: unscaled.

## The sites

| key | site | retail bytes | change |
| --- | --- | --- | --- |
| `EnemyHealthPerBody` / `HeroHealthPerBody` | `0x4b89f7`, `fld dword [0x6d9770]` in the Normal / Easy branch of the max-health function | `d9 05 70 97 6d 00` | `call` to a stub + `nop`. The stub (`pushfd; pushad; push esi; call; fld dword [eax]; popad; popfd; ret`) loads the float for this CStats (esi) - the heroes' or the others' - and leaves every register and the flags as they were (the `je` at `0x4b8a09` uses the `test edi, edi` at `0x4b89e0`). The function's other read of the float, `0x4b8912`, is Hard's enemy formula, 25 + 5 (level - 50) + 3 (level - 45): a level term, left alone |
| `StrengthBonus` | `0x453b6e`, `call 0x4b7940` | `e8 cd 3d 06 00` | a function of the same shape (`__thiscall`, `ret 0xc`) that calls the game's own - so its stat affecters stay - and answers 1 + points x the new bonus |
| `MindBonus` | `0x453b67`, `call 0x4b7970`; `0x5e0afa`, the stats screen's call | `e8 04 3e 06 00`; `e8 71 6e ed ff` | the same for mind; the screen's line shows what the hits do |
| `MightBonus` / `Rounding` | `0x453bc7`, the damage function's second read of the attacker's damage affecters (`call 0x550410`, kind 0x39, mode 1 = the multiplier) | `e8 44 c8 0f 00` | a function that calls the game's own and multiplies its answer by 1 + bonus x min(rank, 3). The rank is read as the game reads one (`0x4b9090`, `0x4bbda9`): CStats+0x20 holds 8-byte entries, the talent's id in the first word, its rank in the low 6 bits of the third byte, the count at CStats+0xbc; the id's talent comes from the registry (`0x4c05b0`, vt+0x14 = `0x4bdbe0`) and its name is the `name` attribute of the data node the talent keeps at +8, through the game's attribute lookup (`0x5649c0`). (The talent's own vt+0x14 string, what `0x4b9090` compares, is empty for every talent of the port's: it is not the name.) The first read (`0x453bb6`, mode 0: what the affecters add) is the game's own |
| `PowerAttacks` | `0x4dc10f`, the parser's one call | `e8 ec 2a f7 ff` | a parser that reads `x1power` as `none` and hands every other name to the game's. A mod writes `DamageScale="x1power"` where XML1 had `PowerAttack="true"`; without the key that is `normal`, as before. An attack whose chain sets no `DamageScale` at all has 0 already (the definition starts zeroed, `0x4dc5db`, and a child copies its parent's, `0x4dce80`) |
| `HitChance` | `0x44f684`, the two pushes before the roll's random draw | `6a 01 68 00 00 80 3f` | `jmp` to a stub + a 2-byte `nop`. The stub stores XML1's chance for the attacker (ebx), the defender (edi) and the attack (esi) over the game's at `[esp+0x20]`, does the two pushes and goes on at `0x44f68b`. Everything before it stays the game's: which attack types are rolled (1, 2, 3 - punch, kick, throw - and anything above 8), the always-hit cases (a defender with no current target, `0x44f557`; attacks flagged at +0x35 bit 2), and the game's own chance and its overflow above 95 %, which feed its critical hits |

With `Rounding=xml1` the strength and mind functions read the roll off the damage function's frame (`[esp+0x20]`,
0x30 bytes above their own return address) and answer the multiplier whose rounded-up bonus is XML1's rounded-down
one (half a point under the whole number, so the game's float steps can't land on the wrong side); the affecter
function does the same with the roll, the hit (`[esp+0x24]`) and what the affecters add (`[esp+0x10]`), the frame
being the one the `out` pointer it gets lies in. Both bonuses are worked out on the roll rounded to a whole number,
in XML1's order of operations and in more than float precision, as its x87 does it: a strength of 4 on a roll of 5
adds 2, as there. `Rounding=xml1` sets up all three functions even without `StrengthBonus` / `MindBonus` /
`MightBonus` (then with the game's own 0.05 / 0.01 and no might).

What stays the game's own, on purpose: the roll being a float unless `DamageRolls=xml1` (XML1's is a whole number: the same mean, slightly
different ends of a range); critical hits (x2 here, x3 there, on different chances); resistances; the stats screen's
attack and defense ratings (XML2's display; with `HitChance=xml1` they no longer decide the roll); Easy's halving and
Hard's formulas; an npcstat `specific_health`.

## Melee weapons and integer damage draws

`MeleeWeapons` is a mod-generated list of `name:type:bonus:damage-bits` records,
separated by `|`. For example, `invented_guard:1:2:128` gives that stats entry a
fixed +2 on punch attacks (kind 1) with physical damage (128). At most 16 unique
names are accepted, each 1-63 lowercase identifier characters; attack kinds are
1-3, bonuses 0-32000, and damage bits physical 128, energy 32768 or electricity
131072. A malformed list is refused in full and logged. Missing/empty means no
weapon contribution. This list is intended for source weapon data, not a balance
multiplier or a built-in enemy-name table.

The guarded call at `0x4dc37b` stores the construction draw at attack +4, then
adds the matching profile's bonus and replaces the damage-type bits (+0x10),
before any contact/to-hit roll or stat and Might bonuses. Attack kind at +0xc
must match. The stub preserves registers and flags and replays the displaced
attacker-stats read; it leaves the x87 stack as the original store did. It runs
once when an attack is built, so a later miss can clear damage normally. Shared
definitions and unarmed actors stay unchanged. Matching power attacks also get
the weapon bonus, as in the first game (`0x5aa61`/`0x5aa74`/`0x5aab0`). The first
game's weapon parser stores the truncated **minimum** of its `damage` value at
prototype +0x1c6 (`0xbc2ef..0xbc30d`, getter `0xb9cf0`), not another random roll.

`DamageRolls=xml1` redirects the construction tail jump at `0x453a51` and the
receive-side random damage call at `0x453add` to the engine's inclusive integer
RNG (`0x5601e0`), returning float to the existing callers. It preserves the RNG
stream argument and truncates the bounds. This covers powers as well as punches.
Hit-chance draws and averaging branches stay unchanged. Nonfinite/out-of-range
bounds outside +/-32000 fall back to the stock float RNG. Missing or `xml2`
leaves both sites untouched. This key is independent of `Rounding`, which
controls bonuses. Rounding a continuous draw would underweight endpoints in
wide damage ranges, so the wrapper delegates to the native integer RNG.

Together with the existing first-game bonus coefficients, a 4/5 base punch and
a +2 weapon become rolls 6/7, then 7/9 damage for Strength 3 and Might rank 2.
The first-game base damage is integer; data-only shifts of XML2's continuous
range cannot reproduce this distribution. Style variants can also require new
registered talents, which a mod must budget before choosing that approach.

The isolated `xml2_test --balance-rules` command runs the balance and melee
synthetic tests without virtual-pad/registry tests. `XML2_FIX_TEST_EXE` can name
a read-only unpacked executable fixture for guarded-site verification. No game
fixture is distributed. Tests cover valid/refused profiles, actor and attack-kind
scoping, damage types, integer-RNG delegation, disabled features and actual
execution of the generated construction stub.

## The receive side and the cap (`ReceiveRounding`, `DamageCap`; gameplay rule 41)

`Rounding=xml1` covers the bonuses of the damage function (`0x453a60`). Two more of the first game's integer steps
sit elsewhere, and each has a key of its own:

| key | X-Men Legends I | this engine | change |
| --- | --- | --- | --- |
| `ReceiveRounding = xml1` | the damage receive (`0x45260`) keeps the hit's damage as an integer (+0x94) and converts after each stage, `0x45517`-`0x45558`: d = trunc(max(damage - defense, 0)); damage = trunc(d x scale) (`_ftol` `0x1a41f0` -> `0x3439c4`: round toward zero, the low 32 bits) | the receive (`0x437490`) keeps a float (+0x9c) through both stages, `0x437a54`-`0x437a7c`: damage = max(damage - defense, 0) x scale. 10 with a flat defense of 0.5 and a scale of 0.5 (an officer-aura-style fraction) is 4 there and 4.75 here | `0x437a54` (`fld dword [esi+0x9c]`) and `0x437a75` (`fmul dword [esp+0x10]; mov ecx,[esi]`) become calls (+ nop) to two stubs: the first loads the damage and truncates it (XML1's field is an integer), the second truncates the clamped difference, multiplies by the scale and truncates again, then replays `mov ecx,[esi]`. Each truncation is x87 `fistp` under a round-toward-zero control word, keeping the low 32 bits as `_ftol`'s `eax`; registers, flags, the control word and the x87 depth are kept |
| `DamageCap = xml1` | a hit's damage is capped at **32,000** (`0x5ce0d`: `cmp eax,0x7d00; jle; mov eax,0x7d00`) | at **1,000,000** (`0x453bfb`: `fcom dword [0x685bf4]` ... `fld dword [0x685bf4]`) | the two operands (`0x453bfd`, `0x453c0c`) point at a 32000.0f of the fix's; the constant at `0x685bf4` stays (other code reads it). Only a greater value is replaced, as in both games |

Absent, empty or `xml2` writes nothing (the bytes stay the game's); any other value is logged and leaves the part
as it is, but makes rule 41 unverified, which closes host and join. The guards are the receive run
`0x437a54`-`0x437a81`, the cap run `0x453bfb`-`0x453c0f` and the constant `0x685bf4`; no other module writes them
(`[Balance]`'s own damage-function sites end at `0x453bcb`; rule 16's receive site starts at `0x437a9d`).

Lockstep peers compute every hit, so either key registers **gameplay rule 41** (`ReceiveRounding/DamageCap`,
revision 1, `{receive installed ? 1 : 0, cap installed ? 32000 : 0, 0, 0}`), requested only: absent adds no entry and
the released record is unchanged. A PC with either key and one without refuse each other naming `41 ReceiveRounding/DamageCap`.

Tests: `xml2_balance_parity_test <XMen2.exe> <default.xbe>` runs both games' code side by side. It reads XML1's
receive run, its `_ftol` pair and its cap run from the owner's `default.xbe` (checked by SHA-256) and XMen2.exe's
receive and cap runs from the owner's exe (checked against the guards), relocates only their constants and calls,
and executes them: the reviewer's example (10, 0.5, 0.5: XML1 4, retail 4.75, the rule 4), then every whole damage
0..600 and five larger ones over ten defenses and twelve scales, in both the 24-bit precision the D3D8 device sets and
53-bit. The rule equals XML1 on every case (72,180 of 72,180 in each precision) where retail differs on about 90 %,
and a fractional damage is XML1's for its truncation. The cap equals XML1's on whole numbers from -40,000 to 8.4
million. Registers, the x87 depth and the control word are checked after every call.

`scripts/prove_balance_install.py` proves the keys install at run time: DllMain runs against the retail image in the
synthetic host (`tools/union_host`, no game) with the port's own `[Balance]`, and every byte changed in XMen2.exe is
read back. The ten `[Balance]` sites are written, the strength (`0x453b6e`), mind (`0x453b67`, `0x5e0afa`) and
might/rounding (`0x453bc7`) calls land inside `dinput.dll`, and the log names 0.1 / 0.1 / 0.05 / xml1; `Rounding=xml1`
alone writes the same four calls and nothing else of `[Balance]`; `[Balance]` absent writes none; the two parity keys
add exactly their four sites and one agreement rule; and against an older build's DLL the same ini writes the same
addresses (rule 16's revision 14 (items 19-22) sites apart).

## Saves

Nothing is stored: max health, every damage number and every hit chance are computed when needed. Stats, levels and
XP stay as they are in the data and in a save. The one stored number the keys touch is each actor's current health
in a saved zone record, an absolute: an enemy saved at 29 of 29 comes back at 29 of 40 once `EnemyHealthPerBody=10`
is on, and one saved at 40 comes back capped at 29 when it is off, until that zone is rebuilt. A hero's saved
health is re-applied clamped to the current maximum (`HeroHealthPerBody` changes it; the others don't).

## Co-op

Every machine computes health and damage from its own ini, so all peers need the same `[Balance]`. The port's
builder gives a balance build its own `[Online] GameVersion` (`X1.1`), so lobbies of builds with and without the
keys never mix. `ReceiveRounding` and `DamageCap` are also gameplay rule 41 (above), so a difference in them is
refused by name.

## Logging

One line per key at start-up (`balance: health 10 per body point (the game's own: 3), scaled heroes 3 per stored
point`, ...), `balance: [Balance] HitChance=fast isn't xml1 or xml2 - the game's own roll stays` for a value that
isn't one, `balance: 0x004B8850 isn't the retail code (max health ...) - the game's own health stays` on another
build. The test pipe's `status` ends with `balance health=10/3 str=0.1/0.025 mind=0.1/0.025 might=0.05 power=xml1
hit=xml1(30,0.025) rounding=xml1` (unscaled / scaled heroes), or `balance off`.
