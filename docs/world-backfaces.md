# One-sided level collision for movement

`[Game] WorldBackfaces=xml1` makes every actor and entity movement sweep meet only the front faces of a level's
collision hull, as X-Men Legends I does. XMen2.exe's movement sweeps also meet the back faces, so in the first game's
levels (which the X-Men Legends I port loads unchanged) actors stop, slide or wedge against walls that the original
lets them walk through from behind. An absent key, an empty value, `xml2` or `off` leaves the game untouched and
registers nothing; any other value is logged, patches nothing and registers as unverified (rule 30), which closes
online play. The key is for a mod that carries the first game's zones.

## What happens without it

An actor moves by tracing its collision box (32 x 32 x 72 by default, the character's stats `size` times its scale)
through the world: the movement update `0x432130` calls the actor movement trace `0x42b6c0`, which reaches the common
trace helper `0x4a9650` through `0x4ab7f0`. The helper fills a pooled collider (vtable `0x68d6a4`) and runs its
sweep (`0x4aa250`) through the world collision object (`0x44d360`). That call tests the level's own hull first, then
every candidate entity.

The hull's BVH leaf testers (`0x5fd1e0`, `0x5fe8f0`, `0x5ffb50`, `0x5ffdf0`) filter each triangle by two flags in
their context: with the back flag clear a triangle whose normal points along the query direction (seen from behind)
is skipped; with the front flag clear the others are. X-Men Legends I's hull test always runs front on, back off
(`default.xbe` `0x12f310` and `0x12f50d`): a wall seen from behind is not there. XMen2.exe gave the collider two flag
bytes (`+0x30` front, `+0x31` back; reset to 1 / 0 at `0x4a9fe2`) and passes them to the hull test; the entity pass
of `0x44d360` forces front on, back off again, so props behave as in the first game. But the trace helper sets the
back flag before every sweep (`0x4a96c0` `push 1`, `0x4a96dc` collider `vt+0x88`); the original's helper
(`0xa0af0`) has no such call.

In haarp2_1 (HAARP, byte-identical to the first game's level) the officer's staging spot for the 520-unit test sits in
a pocket enclosed by walls whose front faces point into it. On the PC the officer meets the back of the wall strip
`y = 90, x 356..360` at about `(340, 91.3)`, is deflected, and below 60 fps wedges at the strip's free end
`(339.58, 91.87)`: three updates without movement, the stuck test aborts the move, and after four aborts the AI picks
a position node 500 units away. The original's officer walks through the same spot in a straight line and first
slides on the front-facing wall at `x ~ 365.9`.

## What the key does

One byte, after every byte it relies on is checked against the retail build:

| Address | Retail | With `xml1` |
| --- | --- | --- |
| `0x4a96c1` | `push 1` (`6a 01` at `0x4a96c0`): the helper asks the collider for back faces | `push 0`: front faces only |

The helper has two callers, `0x4ab7f0` (the actor movement trace and an entity trace) and `0x4ab890` (five more entity
traces); their twins in `default.xbe` (`0xa2b90`, `0xa2c30`) never ask for back faces, so all of them now follow the
first game. Traces that do not use the helper never set the flag, and entity collision is unchanged. A failed write
leaves the game as it was, with one log line; a failed rollback refuses startup. The test pipe's `status` reports `world back faces xml1`.

## Online play

Rule 30 of the [gameplay agreement](gameplay-agreement.md): revision 1, `{installed ? 1 : 0,0,0,0}`, registered only
when requested. Movement decides where every actor ends up on every peer, so both PCs must agree: an `xml1` PC and a
PC without the key refuse each other naming `30 WorldBackfaces`.

## Tests

`xml2_world_backfaces_test <retail XMen2.exe>` (no game): seeds the guarded retail bytes at their own addresses, runs
the retail helper `0x4a9650` against a recording collider before and after the patch (back-face flag 1 -> 0, the
same calls in the same order, the same sweep), checks the installer's refusals (unknown value, relocated image,
every guard), its one-byte footprint, the leaf filter's truth table and the rule-30 agreement entries.

## With [Limits] EntityCapacity (Legends Classic builds)

The entity-capacity transaction (installed at startup, before this rule) bridges the helper's first 7 bytes
(0x4a9650) to its DLL owner `entity_query::QueryApi_004a9650`, so the retail operand at 0x4a96c1 never runs there.
The owner passes `world_backfaces_state::trace_back_faces` to collider vt+0x88: 1, or 0 once `WorldBackfaces=xml1`
installed. The first guard therefore starts after the entry (0x4a9657). (Integration fix for 1.8.0-beta.7: as first
written, the rule would have failed its guard in every Legends Classic build - unverified, host/join closed - and
changed nothing even if it had installed.)
