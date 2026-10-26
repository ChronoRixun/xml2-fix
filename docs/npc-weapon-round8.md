# Weapon brain round 8: warm-up, continuous, pistol, line of sight and the empty query

Static reading of the owner's default.xbe and XMen2.exe (2026-10-24) for the
weapon families round 7 left as "new code needed", implemented inside rule 16
revision 10's weapon-brain transaction. No game or emulator was run for this
round; every live number is still to be measured (see "Acceptance runs").
Game data is not reproduced here; actors and weapons are named only as families.

## Input gate `0x42f60`, in full

For an actor with a weapon (`+0x3e0 != -1`) whose item category is not 2:

1. Power plus fire not both held: clear the warm-up deadline `+0x49c` if set,
   return false (native input continues).
2. Input locked (`+0x54c` bit 0): return true.
3. Current animation not idle, crouch, power1 or power7: request `idle`
   (`-1`, `-1.0`, flags 0) and set the recoil deadline `+0x498` to now + 0.25
   (`0x3ca9e0` holds `0x3e800000`; revisions 9/10 used 0.05). Return true.
4. now < `+0x498`: return true.
5. Warm-up (prototype `+8`) not zero: if `+0x49c` is zero, play the charge
   effect (actor vfunc `+0x84` with prototype `+0x34`), set `+0x49c` = now +
   warm-up and return true; while now < `+0x49c` return true.
6. Request `power_attack` (crouched: power7) with play flags 1 if the prototype is
   a continuous beam (`+0x2c`), else 9 and clear `+0x49c`. Return true.

So: discrete warm-up weapons cycle recoil + warm-up per shot (0.7 + 0.5 and
0.8 + 0.5 s, quantised to frames); continuous beams warm up once per hold and are
then requested every recoil (every update for the zero-recoil lightning gun,
every 0.03 s for the flamethrower) with flag 1; the semi-auto entry `0xe14d0`
re-arms `+0x498` at each entry. The fixture executes the owner's original gate
against the restored one over a 12 s timeline (with a 0.5 s release) for each
family at 30, 49 and 60 fps; decisions, flags, warm-up starts and both deadlines
agree bit for bit:

| Family | 30 fps | 49 fps | 60 fps |
|---|---|---|---|
| discrete 0.17 s | 6 frames | 9 | 11 |
| nullifier 0.7 + 0.5 | 36 | 60 | 72 |
| knockback 0.8 + 0.5 | 39-40 | 65 | 78-79 |
| lightning (0, warm-up 0.6 once) | 1 | 1 | 1 |
| flame (0.03, warm-up 1.0 once) | 1 | 2 | 2 |

The PC conversion places the charge effect inside the primary at the shot
(the builder's `charge_triggers`), so the restored warm-up keeps the timing but
the charge effect plays with the shot, not at the warm-up start.

The pistol's primary uses the default handler, whose entry does not arm
`+0x498`; only step 3 does. The restored gate therefore requests it on every
update in which the actor is neither input locked nor outside the firing
animations, exactly as the original would. Whether the move then restarts or
plays to its 0.5 s shot depends on the native request (PC `0x432030`: the current
move's priority test `+0x50`) and the move's input lock: `lockchaining` (move
flags bit 0, default true, PC move `+0x144`, original `+0x12c`) sets actor input
lock bit 0 at move start (original `0x407d8..0x407f4`, PC `0x431273`); when that
lock is released relative to the 0.5 s shot was not established statically. The
original's pistol cadence is the first acceptance capture.

## Line-of-sight drop (bullet)

`0xef3cd..0xef434`, between the target resolve and the attack-state checks: for a
bullet prototype, `0xfe550(&flag)` on the AI; false with the flag untouched
(target missing, or the ray within 600 units hit another entity) sets brain
`+0x20` = 0, `+0x1c` = -1 and AI state 0 (`0xfe330`). XML2 keeps its own version
of the test, `0x505250` (same contract and 600-unit limit; eye points from bones
and a 15-unit ray box instead of +60 and 1 unit), and the state setter `0x505030`.
The eighth transaction site, `0x519e3a` (after `0x518660`), runs it.

## Empty near-target query

The PC brain's query `0x50a250` (radius 80) and its found branch match the
original's `0x103850` and held-fire branch. When the query is empty, the PC
continues at `0x51a359` (clear actor bit 2, return); the original clears the same
bit and runs, for bullets, `0xef9bc` and otherwise `0xefc4a`. Both now run there.

Bullet `0xef9bc`: if now > `+0x20`: while `+0x1c` >= 0 hold `0x90`, subtract the
frame delta, zero movement (`input+8`, `input+0x14`) and the strafe bit; when it
goes negative pause until now + 1.0 and set the next burst to rand + 2.0. A current
move with `lockangles` ends the update. The aim point (original actor `+0x318`, PC
`+0x3ac`, written by the brain aim setter) moves toward the destination by
`1 - 0.15^dt` while firing (`0.25^dt` otherwise); a rapid-fire weapon's pause frame
lerps it 0.25 of the way from the actor (`0x23fc50`). The AI turns toward the aim
point when it is more than 10 units away on the ground plane (`0xfc5d0` = PC
`0x503180`).

Other types `0xefc4a`: revision 8's port (strafe bit `0x2000`, turning, the
burst/pause cycle with the source burst time, the victim notice and alert) plus
what it lacked: no turning while a started warm-up runs; on-axis continuous beams
sweep `0.015 * (+0x28)` rad per update; a spent burst does not pause until the
warm-up deadline is 0.4 s old; a warm-up ending within 0.4 s stops the strafe and
movement and clears AI `+0x244` (PC `+0x270`). The unused getter `0x102a30` is not
called.

Mapping used (original -> PC): AI state setter `0xfe330` -> `0x505030`, visibility
`0xfe550` -> `0x505250`, turn `0xfc5d0` -> `0x503180`, rotate `0xfc600` -> `0x5031b0`,
defer `0xfc110` -> `0x502580`, aim setter `0xede60` -> `0x517cf0` (`+0x318` ->
`+0x3ac`), move flag `+0x54` -> `+0x5c` (`lockangles`), move priority test `+0x48` ->
`+0x50`, animation getter `+0xc8` -> `+0xe8` (field `+0xac` -> `+0xbc`), AI field
`+0x244` -> `+0x270`. The movement vector the bullet branch copies (`0x5bc9d0`) has
no writer in the XBE and is taken as zero.

## Agreement

Rows are schema 3 (`warmup_bits` and `flags` words); the fingerprint covers all six
words. The ten-row discrete table that round 7 measured as `3767359A` now hashes
differently, so older revision 10 candidates and the new transaction refuse each
other by value; that is intended, since the transaction also changed the
transition delay and the empty-query continuation for those rows.

## Acceptance runs (not run)

Each family needs the original (emulator, its 49 Hz cap) and the port (PC-free,
FrameRate 60 and 49) in the round 6 staging style: the NPC spawned from its zone
spawner through the fix's script pipe, hero at full HP with a fixed camera, 30 s
windows at 120 / 300 / 520 units (275-unit range weapons: 120 / 200 / 260), EP 0
and full energy; counts of shot events, gate entry gaps, warm-up starts (charge
sound), melee entries and grenade/power entries; plus one window per family with
a pillar between NPC and hero for the line-of-sight drop and one with the hero
outside the near-target radius for the empty-query branches.

- Warm-up per shot (nullifier, knockback): gaps recoil + warm-up; charge
  timing relative to the shot (original: 0.5 s before; port: with the shot).
- Continuous (lightning, flame): one warm-up per hold, then a restart per recoil
  with flag 1; visible beam continuity; the flame sweep power and the 0.015 rad
  sweep on an empty query.
- Pistol: the original's cadence first (does the 0.5 s shot ever play under
  continuous requests?), then the port.
- Bullet line of sight on the MP5 and laser holders: the drop rate behind cover,
  and a regression check of the round 7 open-field windows (XML2's visibility test
  differs in eye height and ray box).
