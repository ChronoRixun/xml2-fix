# One-sided level collision for movement

`[Game] WorldBackfaces=xml1` makes actor and entity movement sweeps meet only the front faces of a level's
collision hull, as X-Men Legends I does - only in the zones `[WorldBackfaces]` lists (the mod's first-game levels;
the list is required: without a valid one nothing is patched and the request is unverified). XMen2.exe's movement sweeps also meet the back faces, so in the first game's
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

```ini
[Game]
WorldBackfaces = xml1
[WorldBackfaces]          ; required (revision 2): only these zones
ZoneCount = 198
Z1 = arbiter/a_int/arb2_1
...
```

Six bytes, after every byte it relies on is checked against the retail build:

| Address | Retail | With `xml1` |
| --- | --- | --- |
| `0x4a96c0` | `push 1; fstp dword [esp+0x10]` (`6a 01 d9 5c 24 10`): the helper asks the collider for back faces | `jmp` to a bridge + `nop`: the bridge does the store, then pushes the current zone's flag (0 = front faces only in a listed zone, 1 = XMen2.exe's own elsewhere) and returns to `0x4a96c6` |

**Which zones (revision 2, owner decision 2026-10-25).** A port install still carries XMen2.exe's own maps (in the
Legends Classic 0.7.0a candidate 151 map IGBs: the tutorial, XML2's act1-5, briefings, Danger Room `dr/`, versus `svs/`
and `menu/main_back2`); the first game's rule is for the first game's levels only, so the builder lists the zones it
converted (198 in 0.7.0a, including XML1's Danger Room `arena/` maps and `menu/main_back`) and every other zone keeps
the two-sided sweep. The current zone is the zone manager's path (static `0x72a578`, vtable `0x68878c`, `+0x1e0`, 0x40
bytes, the `vt+0x5c` getter `0x483f30`; guarded with its constructor `0x4849ba`), named when its load is asked for; it is
compared case-insensitively with `\` read as `/`, and the flag is cached until the path changes. Each change is logged
(`world back faces: zone 'haarp/haarp2_1' - one-sided (an X-Men Legends 1 level)`). Before a zone manager exists the
game's own 1 is used. A list entry is 1-63 characters of `a-z 0-9 _ - .` and `/` between non-empty parts; `ZoneCount`
is 1..512 in plain decimal (no sign, space or leading zero). A missing list or count, an invalid count, and an empty,
short, duplicate or malformed list patch nothing and are unverified (closing host/join): the DLL itself enforces
"listed zones only" - there is no every-zone mode (review round 2: a count of -1 had fallen back to every zone). The
builder refuses a DLL that does not read the list (`[WorldBackfaces] ZoneCount`), i.e. the first, unscoped beta 7
candidate. Every peer of a lockstep session loads
the same zones, so every peer picks the same flag; the list itself is in the agreement (rule 30's detail).

Static check of 0.7.0a (read only): no XML1-side content (the converted zones' scripts, conversations and dialogs,
`Scripts/x1`, the menus, the Danger Room table) names an XML2-origin zone; XMen2.exe's end-of-game
`loadZone('act5/egypt/egypt6')` is replaced by `PostgameScript`; XML2's console loadpoint scripts
(`Scripts/loadpoints/*.py`) are the only other references. So no XML2-origin map is on a player's path in a Legends
Classic build - the scoping keeps them XML2's should one become reachable (a `--frontend xml2` build uses XML2's Danger
Room maps, for example).

The helper has two callers, `0x4ab7f0` (the actor movement trace and an entity trace) and `0x4ab890` (five more entity
traces); their twins in `default.xbe` (`0xa2b90`, `0xa2c30`) never ask for back faces, so all of them now follow the
first game. Traces that do not use the helper never set the flag, and entity collision is unchanged. A failed write
leaves the game as it was, with one log line; a failed rollback refuses startup. The test pipe's `status` reports `world back faces xml1`.

## Online play

Rule 30 of the [gameplay agreement](gameplay-agreement.md): revision 2, `{installed ? 1 : 0, scoped ? 1 : 0, zones
listed, 0}` and, with a list, the list's SHA-256 as the rule's detail (`WBZ1`, the zone count as u32 LE, then every
sorted zone as its length byte and normalized path; `world_backfaces_rules::canonical_bytes`); registered only when
requested. Movement decides where every actor ends up on every peer, so both PCs must agree: an `xml1` PC and a PC
without the key, or with another zone list, refuse each other naming `30 WorldBackfaces`. Revision 1 (unscoped, the
private 0.7.0a candidate) is refused by revision.

## Tests

`xml2_world_backfaces_test <retail XMen2.exe>` (no game): seeds the guarded retail bytes at their own addresses, runs
the retail helper `0x4a9650` against a recording collider before and after the patch (back-face flag 1 -> 0, the
same calls in the same order, the same sweep), with a zone list and a fake zone manager at `0x72a578` (a listed zone
0, an XML2 zone, a name that only starts like a listed one and no zone manager 1, the entity-capacity owner's
`world_backfaces_state::current()` the same), checks the installer's refusals (unknown value, relocated image, every
guard, empty / short / duplicate / malformed / oversized lists), its six-byte footprint, the leaf filter's truth table
and the rule-30 agreement entries (lists, digests, revision 1 refused). `xml2_entity_query_test <retail XMen2.exe>`
runs the entity-capacity owner `QueryApi_004a9650` against the retail helper with both flag values (the retail run
with `0x4a96c1` = 0 against the owner passing 0) and checks that the two values change the query state.

## With [Limits] EntityCapacity (Legends Classic builds)

The entity-capacity transaction (installed at startup, before this rule) bridges the helper's first 7 bytes
(0x4a9650) to its DLL owner `entity_query::QueryApi_004a9650`, so the retail operand at 0x4a96c1 never runs there.
The owner passes `world_backfaces_state::current()` to collider vt+0x88: 1, or once `WorldBackfaces=xml1`
installed the same zone flag the bridge pushes. The first guard therefore starts after the entry (0x4a9657). (Integration fix for 1.8.0-beta.7: as first
written, the rule would have failed its guard in every Legends Classic build - unverified, host/join closed - and
changed nothing even if it had installed.)

## Live checks

For the game agent (the native tests run the helper against a recording collider, not a level):

1. The log names each zone's choice on entry: an XML1 level `one-sided (an X-Men Legends 1 level)` (every zone of
   the campaign, the Danger Room arenas `arena/*` and `menu/main_back`); nothing in a Legends Classic build should log
   `two-sided` during play.
2. HAARP's 520 pocket (haarp2_1): the officer walks through to its staging spot at 30 and 60 fps (no wedge at
   (339.58, 91.87), no stuck aborts).
3. The per-zone walk (heroes and enemies, 30 and 60 fps): no falling through floors, no walking out of the level
   through walls approached from behind, doors and lifts still stop the party; play-test every room, follow up any
   "could not cross".
4. Co-op: both PCs log the same `online: rule 30 detail sha256`; a PC with another build's list is refused naming
   `30 WorldBackfaces`.
