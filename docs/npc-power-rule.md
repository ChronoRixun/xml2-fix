# NPC power rule (`[Game] NpcPowerRule`)

```
[Game]
NpcPowerRule = xml1   ; computer-controlled characters use powers and melee combos as in X-Men Legends 1
```

Without the key, or with `xml2`, nothing is patched.

## Problem

Played on the X-Men Legends 1 port, the first game's enemies with powers (seen with the Brotherhood Marauders and
Revolutionaries at HAARP) use them far more often than on the Xbox, and a party gets stun- and knock-back-locked.
The data is the first game's; the engine decides differently.

## What the two games do

The design is shared. Each AI has four power slots, each with an aitype. The aitype selects a mask from the same
18-entry table and carries an aireusetime. One picker takes a random power whose reuse time has passed and whose
aitype range holds the target's distance: XML1 0x1081f0, XMen2.exe 0x50fe50. The same states call it. Three things
differ:

| | X-Men Legends 1 (default.xbe) | XMen2.exe |
|---|---|---|
| Who pays a power's cost | everyone except an AI-flagged character whose AI time (+0x550) is set and whose controller says no (0xcde95) | an AI-flagged character (actor+0x3d8 bit 0, every enemy) pays only when its controller says yes (0x4318e7). 0x503160 says yes once after a queued command (AI+0x2bc), so enemies never pay |
| Melee-range power decision re-check | 2.0 s, or 0.5 s for a character with a controller (0xee34a..0xee374) | 0.3 s for everyone (0x5188d8, 0x681ebc) |
| Energy before that decision | max / energy <= 2.0 unless AI flag bit 3 (0xee37a..0xee3ae) | none |

The melee combos differ in two more places (the brain's attack decision: XML1 0xeea60, XMen2.exe 0x518fd0):

| | X-Men Legends 1 | XMen2.exe |
|---|---|---|
| Combo level | set by the AI init (0xfd330): 6 (all six combos) for melee-type NPCs and party heroes, 2 for a ranged-type NPC without the leader bit, 3 for Toad and Pyro | the same choice (0x504a20; its top level is row 11 of 12), then overridden by npcstat `combolevel` (0x505011): 0 for every entry without it, and level 0 never starts a combo |
| Combos and weights | 6 combos, A / A S / A A A / S S / A S A S / A S A A A, weights 5 / 25 / 20 / 15 / 15 / 15 (0x452bac, 0x452c30, 0x452c18) | 11 combos, 12 named rows of weights (0x6dd4ac, 0x6dd4d8, 0x6dd508) |
| After starting a combo | the decision's tail sets the next attack time: now + 0.6 + rand x 1.2 s (meleetimeroffset / meleetimerrandomadd) and the crowd delay (0xef040 -> 0xef199) | returns at once (0x51993a, 0x519b04); only single attacks reach the tail (0x519840) |

So in the port the first game's NPCs (no `combolevel` in their data) never chained a hit: their melee was a stream of
single `attacklight1`s. With combos alone (a staged test that set the level), they chained but attacked again the
moment a combo ended (median gap 0.03 s against the original's 2.4-4.7 s).

XMen2.exe's actor+0x768 is XML1's +0x550: the same writers clear it, for example 0x5068a0 and 0x10b5c0. Its
actor+0x3d4 is XML1's +0x330: the same test gates the buff-self power in 0x512ed0 and 0x10ad70. Its AI+0x2b6 bit 3
is XML1's AI+0x28a bit 3: the timer bypass that both decisions read.

## The change

Every write below goes in one transaction (`xml1_hook_transaction`), with three bridges and XML1's six combos in one
block of the fix's own:

1. **0x4318e7 (9 bytes, jmp).** The bridge makes XML1's test. Without the AI flag, the character pays. With it, the
   character pays when -0.5 < actor+0x768 < 0.001. Otherwise it asks the controller at 0x4318f0, as before.
2. **0x5188d8 (6 bytes, call in place of `fadd [0x681ebc]`).** The bridge adds 2.0 when actor+0x3d4 == -1,
   otherwise 0.5.
3. **0x518920 (5 bytes, call in place of `call 0x50fe50`).** Unless AI+0x2b6 bit 3 is set, the bridge returns
   al = 0 (the picker's "nothing") when max (+0x314) > 2 x energy (getter 0x41d5d0). Otherwise it jumps to the
   picker with the caller's frame unchanged.
4. **0x505011 (3 bytes).** The AI init's `call [edx+0x44]` with npcstat `combolevel` becomes `add esp,4`: the level
   stays the one the init chose the first game's way (2, 3 or the top level 11).
5. **The combo tables (.rdata).** Slots 0..5 point at XML1's six combos (in the block) with their lengths; rows 2, 3
   and 11 - the levels the init picks - hold XML1's levels 2, 3 and 6 (the first 2, 3, 6 of 5 / 25 / 20 / 15 / 15 /
   15). Rows 0, 1 and 4..10 keep their weights and are reached only through `combolevel`, which no XML1 entry has.
6. **0x51993a and 0x519b04 (6 bytes each, jmp 0x519840).** After a combo start, the decision's tail runs as after a
   single attack: the next attack time and the crowd delay are set as in XML1.

Left as XMen2.exe has it: XML1 presses a combo's next button after the combo's own delay (0.2-0.6 s, 0x452b94);
XMen2.exe presses it as soon as the move allows. XML1's second combo table (brain mode 1) has no XMen2.exe brain.

All guards are checked before anything is written: the retail bytes of every site, the controller read at 0x5188c3
the energy getter's read, the combo exits' tail and the five table runs. A mismatch leaves the game as it is and writes one log line. A failed write is rolled
back. A failed rollback refuses startup.

Party heroes are affected by the same rules, as in XML1. An AI teammate decides every 0.5 s instead of 0.3 s and
needs half its energy for a melee-range power. Whether it pays still depends on its controller, unchanged, because
its AI time is set.

## Gameplay agreement

Online play is lockstep and the rule changes AI decisions and energy, so it is shared gameplay rule **16**
(`gameplay_capabilities::npc_power_rule_id`, [gameplay-agreement.md](gameplay-agreement.md)): revision 2 (the power
and combo rules; revision 1, never released, had the power rules only),
`{installed ? 1 : 0, 0, 0, 0}`; registered in an LC installation or when requested; absent or `xml2` is the verified
`{0,0,0,0}`; requested but not installed (a guard mismatch, protection, readback, an unknown value) is unverified and
closes host/join. A peer with the rule and one without never play together.

## Evidence

Staging, the same in both games: the zone loaded by script / console, the hero invulnerable and idle, a teleport onto
one of the zone's spawner points, the zone's own spawns. A read-only tracer logged every FightMove start of every
character (port actor+0x378, original actor+0x2f4; the original ran in xemu, its rates per game-clock minute).
"Chained/heavy" counts the second and later hits of a combo and the heavy, trip and stun attacks. Hero free: the share
of time the hero is not in a stun, knock-back, thrown, get-up or wall-slump move. Two runs per column unless noted;
small samples (one Marauder per run, 120-150 s).

HAARP ice tunnel (`haarp/int/icetunnel1`), per enemy per minute:

| | Original | 0.2.1 (1 run) | 0.2.2 | Power rules only | Power + combo rules |
|---|---|---|---|---|---|
| Revolutionary powers | 4.3-6.4 | 16.9 | 16.0-17.8 | 9.8-10.2 | 10.2-10.6 |
| Revolutionary stun arc (power_smash) | 1.8-4.4 | 15.0 | 11.8-16.0 | 5.4-6.3 | 5.1-5.7 |
| Revolutionary chained / heavy hits | 4.6-4.8 | 2.5 | 1.0-1.2 | 0.3-0.6 | 3.8-5.0 |
| Revolutionary pause between attacks (median) | 2.4-4.2 s | 2.1 s | 2.7 s | 3.2-3.5 s | 3.2-3.5 s |
| Marauder powers | 10.0-10.7 | (not engaged) | 19.6 | 16.7-17.7 | 5.6-17.6 |
| Marauder chained / heavy hits | 3.3-6.9 | (not engaged) | 0 | 0 | 2.4-5.2 |
| Hero free | 51-53 % | 6.5 % | 0-1.5 % | 18-20 % | 22-38 % |

HAARP interior (`haarp/int/haarp2_1`), GRSO soldiers (the first game's guns are melee-style attacks; the port's are
powers made by the builder, aitype beamanyrange):

| | Original | 0.2.2 (1 run) | Power + combo rules |
|---|---|---|---|
| HAARP Soldier shots + punches / min | 35.5-35.8 (all attacks) | 45.7 (34.8 powers + 10.9 melee) | 31.5-37.5 (20-24.5 powers + 11.5-13 melee) |
| HAARP Leader attacks / min | 44-46 (incl. 3.9-5.1 smash) | 39.5 (31.5 powers) | 31-35 (11-15 powers) |

The first game's AI decisions were also counted directly in xemu (breakpoints at default.xbe 0xee2f4 / 0xee3f1, 30
game seconds): the melee-range decision passed its timer 9-23 times per AI and picked a power 3-6 times, against 10-11
and 4-5 per Revolutionary with the rule (Frida on 0x518770 / 0x5188e7 / 0x518925).

Open: with both rules the Revolutionary still uses about twice the original's powers and the hero is free 22-38 % of
the time against 51-53 %. XML1 presses a combo's buttons on its own delays (0.2-0.6 s) where XMen2.exe presses them as
soon as the move allows; the remaining gap was not traced further.
