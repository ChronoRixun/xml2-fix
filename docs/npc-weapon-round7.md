# Weapon brain round 7: the other weapon NPC families

Static reading of the owner's default.xbe (2026-10-24), extending the officer
work of round 6 (docs/npc-weapon-round6.md) to the nineteen other source weapon
NPCs. Game data is not reproduced here; actor and weapon names are given only
as families.

## One weapon brain, four weapon types

The original builds the same weapon brain for every weapon NPC. The weapon
prototype (vtable `0x3d4834`) exposes type (`+0`; bullet 0, flame 1,
projectile 2, beam 3 from the parser table `0x44fc44`), recoil (`+4`),
warm-up (`+8`), burst time (`+0x10`), accuracy (`+0x14`) and three flags in
byte `+0x1d4`: charger (`+0x28`), continuous beam (`+0x2c`) and rapid fire
(`+0x30`).

The brain update `0xef390`:

- bullet weapons only: at the top of every update a line-of-sight test to
  the AI target (`0xef3ff..0xef434`, `0xfe550`, eye height +60) resets the
  burst timers and drops the AI to state 0 when it fails;
- every type: after the attack-state checks, the near-target query
  `0x103850`. When it returns an entity the held-fire branch
  `0xef8df..0xef981` runs (face, hold power+fire `0x90` on both input words,
  aim) - the branch the officer runs in every measured window;
- when the query returns nothing: bullets run `0xef9bc..0xefc3e` (fire held
  while brain `+0x1c` stays non-negative, then a 1.0 s pause and a new burst
  of 2-3 s; rapid fire only adds one call `0x23fc50` at the pause), other
  types run `0xefc4a` (the burst branch revision 8 ported).

The input gate `0x42f60` is weapon generic: with fire held it waits for the
recoil deadline `actor+0x498` (armed by the semi-auto entry `0xe14d0` at
entry + recoil), then for weapons with a warm-up it starts the warm-up once
(actor vfunc `+0x84` with the prototype's warm-up effect, deadline
`actor+0x49c`) and waits for it, and then requests `power_attack` with play
flags 1 (continuous) or 9 (otherwise; this also clears the warm-up deadline
so every discrete shot warms up again). The flag only reaches the animation
play call (`0x403c0` -> `0x3e170`); the move restarts either way. Every
discrete gun primary fires at time 0, so each restart is one shot.

## What the revision 10 brain covers

Revision 10 restores the held-fire branch at the PC's unresolved continuation
and the gate without warm-up or continuous handling. That is exactly the
original for discrete bullet, beam and projectile weapons without warm-up:
the officer and the nine bullet NPCs (four MP5 holders, five laser or super
laser holders). They need only source rows (the builder exporter now admits
bullet weapons; rapid fire changes nothing on this path). The rows keep
schema 2; the fingerprint covers every row, so a peer with a different table
is refused by value.

Not covered (new code needed):

| Family | NPCs | Missing |
|---|---|---|
| Nullifier, knockback | 4 | warm-up before every shot (deadline `actor+0x49c`, warm-up effect) |
| Lightning | 2 | zero recoil, warm-up once, continuous restarts with play flag 1 |
| Flame | 3 | as lightning, plus range 275 and the authored constant-beam move |
| Pistol | 1 | default-handler primary (shot at 0.5 s): no semi-auto entry arms the recoil deadline, so the gate's admission differs; needs its own reading and a measurement |

Not restored for any type, the officer included: the bullet line-of-sight
drop and both branches for an empty near-target query.

## MP5 soldier on the port (2026-10-24)

Staging as round 6 (same zone and line, 30 s windows, FrameRate 60); the soldier
comes from its zone spawner (`actSilent` on the spawner name). Another agent's
game ran during most windows, so these are busy-PC numbers. The original was
not run in this round (the emulator was in use); its column is the prediction
from the shared held-fire branch and the 0.16 s gate (8 frames at 49 Hz).

| Cell | Original (predicted) | Released | Revision 10 + rows |
|---|---|---|---|
| 120 / EP 0 | ~175, 0.163 s | 14 shots, 10 melee, closes to 77 | 167, entry gaps 0.160-0.240 (median 0.167), 0 melee |
| 300 / EP 0 | ~184, 0.163 s | 2, 13 melee | 177, median 0.167, 0 melee |
| 520 / EP 0 | ~175 | 0, 14 melee | 74: approach stalled at ~501 until +15.8 s (repositioning issue, not the weapon) |
| any / full | 0, grenade held | 1-9 shots, 13-16 melee | 0, grenade held (at ~496 after the 520 stall) |

Second windows (120/300 with full energy first; 520 in its own session, the
only PC-free one): 120 / EP 0 172 shots (gaps 0.165-0.168), 300 / EP 0 178,
520 / EP 0 171 (closes to 343), full energy 0 shots with the grenade held at
308 / 300 / 369.

The native near-target query found its entity on every update, so the native
held-fire branch ran and the gate requested `power_attack` (flags 9) once per
recoil; no gap was shorter than the 0.16 s deadline.

## Proposed next code (not implemented)

1. Warm-up (nullifier, knockback): a sixth row word (warm-up bits) in a new
   schema, a per-actor warm-up deadline next to the recoil deadline, armed
   when fire is first admitted and cleared after every discrete shot and on
   release, as `0x42f60` does. The PC conversion places the weapon's charge
   effect inside the primary move (`charge_triggers`), so the warm-up start
   needs its own effect request before the move, or the conversion must move
   the charge out of the primary; measure both against the original.
2. Continuous (lightning, flame): a continuous flag in the row, zero recoil
   admitted only with it, play flags 1, and the beam start/update/stop of the
   converted move; flame also needs range 275 and the constant-beam sweep.
3. Pistol: read the default handler's entry/chain against `0x42f60` first.
