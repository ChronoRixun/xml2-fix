# NPC power rule (`[Game] NpcPowerRule`)

```
[Game]
NpcPowerRule = xml1   ; computer-controlled characters pay for powers and decide on them as in X-Men Legends 1
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

XMen2.exe's actor+0x768 is XML1's +0x550: the same writers clear it, for example 0x5068a0 and 0x10b5c0. Its
actor+0x3d4 is XML1's +0x330: the same test gates the buff-self power in 0x512ed0 and 0x10ad70. Its AI+0x2b6 bit 3
is XML1's AI+0x28a bit 3: the timer bypass that both decisions read.

## The change

Three sites, written in one transaction (`xml1_hook_transaction`), with three bridges in one block of the fix's own:

1. **0x4318e7 (9 bytes, jmp).** The bridge makes XML1's test. Without the AI flag, the character pays. With it, the
   character pays when -0.5 < actor+0x768 < 0.001. Otherwise it asks the controller at 0x4318f0, as before.
2. **0x5188d8 (6 bytes, call in place of `fadd [0x681ebc]`).** The bridge adds 2.0 when actor+0x3d4 == -1,
   otherwise 0.5.
3. **0x518920 (5 bytes, call in place of `call 0x50fe50`).** Unless AI+0x2b6 bit 3 is set, the bridge returns
   al = 0 (the picker's "nothing") when max (+0x314) > 2 x energy (getter 0x41d5d0). Otherwise it jumps to the
   picker with the caller's frame unchanged.

All guards are checked before anything is written: the retail bytes of every site, the controller read at 0x5188c3
and the energy getter's read. A mismatch leaves the game as it is and writes one log line. A failed write is rolled
back. A failed rollback refuses startup.

Party heroes are affected by the same rules, as in XML1. An AI teammate decides every 0.5 s instead of 0.3 s and
needs half its energy for a melee-range power. Whether it pays still depends on its controller, unchanged, because
its AI time is set.

## Gameplay agreement

Online play is lockstep and the rule changes AI decisions and energy, so it is shared gameplay rule **16**
(`gameplay_capabilities::npc_power_rule_id`, [gameplay-agreement.md](gameplay-agreement.md)): revision 1,
`{installed ? 1 : 0, 0, 0, 0}`; registered in an LC installation or when requested; absent or `xml2` is the verified
`{0,0,0,0}`; requested but not installed (a guard mismatch, protection, readback, an unknown value) is unverified and
closes host/join. A peer with the rule and one without never play together.

## Evidence

Measured in the HAARP ice tunnel. The hero was invulnerable and idle, after a staged teleport onto a spawner point.
FightMove starts were sampled per enemy with a read-only tracer: port actor+0x378, original actor+0x2f4. The original
ran in xemu.

| Measurement | X-Men Legends 1 (Xbox) | 0.2.2 | 0.2.2 + `NpcPowerRule=xml1` |
|---|---|---|---|
| Hero free of stun / knock-back / get-up | 51-53 % of the time | 0-1.5 % | 18-20 % |
| Revolutionary power_smash (stun arc) | ~4.4 / min | ~12 / min | ~5.4 / min |
| Revolutionary energy | spent, refills ~15 / s | always full | spent, refills ~15 / s, powers only at >= half |
| Marauder powers | 10-11 / min | ~19 / min | 17-18 / min |

The Marauder's power_smash costs nothing in XML1's data, so its energy rarely matters. Its remaining gap, and the
remaining stun-lock gap, come from the port's enemies starting more attacks overall. That is not covered by this
rule.
