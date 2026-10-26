# X-Men Legends 1 wall cling

`[Game] Xml1WallCling=xml1` gives the acrobat moveset (Beast, Nightcrawler, Psylocke) the two FightMove handlers
X-Men Legends 1 used for its wall cling. An absent key, empty, `xml2`, `0` or `off` leaves the game untouched; any
other value is logged and ignored (and closes host/join, as any failed gameplay rule does). Candidate: written from
static analysis only, not yet played.

## What is missing in XMen2.exe

Both games' `moveset_acrobat` carry the same two moves: `clingwalldecide` (handler `ch_clingwalldecide`, entered
with a second jump press during a jump; chains `move` -> `airjump`, `special` -> `clingwallbackflip`, `special2` ->
`jumpland`) and `clingwallbackflip` (handler `ch_clingwallbackflip`, animation `ea_cling_wall_backflip`, requires
acrobatics 2; chain `idle`). XMen2.exe registers neither handler name (its table, 0x4fd8d0 onwards, has 68 names),
so both moves run as `%default%` (0x4fd860): the decide move only offers the air jump, nothing ever issues
`special` / `special2`, and the back-flip, when reached, plays in place.

## The original handlers (default.xbe)

| Original | Behaviour kept |
|---|---|
| `0xd9740` (decide update) | With acrobatics at rank 2 or more and jump (input bit 6) held, the actor's movement trace (`0x3c720`) from its position along its facing x 24. A contact with the level or a `tileent`, whose normal is flat (abs(n.z) < 0.2) and faces the actor (facing . n < -0.5, both strict), turns the actor to face it (yaw = atan2(-n.y, -n.x), angles {0, 0, yaw}), moves it by the trace's shift, marks the input consumed (+0x24 bit 2) and takes the `special` chain. Otherwise, jump held: the `move` chain (air jump); otherwise, on the ground: `special2` (land); otherwise nothing. |
| `0xd9590` (back-flip entry) | velocity = facing x -60 (x, y) and 500 up; detached from the ground (null handle); the grounded bit (+4 bit 2) cleared and the flags published. |
| `0xd96e0` (back-flip update) | once grounded (ground entity, or +4 bit 2), start the move named `idle`. |

Neither entry runs the default entry (the move's start time). The decide entry's only write (bit 0 of +0x64 of its
third argument) is not reproduced: the PC object behind that argument is not identified.

## The same services in XMen2.exe

Matched by code shape: the movement trace `0x42b6c0` (the same code as `0x3c720`, five arguments: start, delta,
flag, shift out, contact out), the contact accessors `0x4a95c0` (the level), `0x4a9590` (entity), `0x4a9600`
(normal), the `tileent` cast `0x41e4d0` (class `0x778a4c`, registered as "tileent" at `0x67ae25`), angles to facing
`0x560990`, the action resolver `0x427c80`, the move's may-start slot vt+0xc, named move `0x41b120` + `0x427ca0`,
start move `0x42c340`, set velocity `0x473640`, ground handle `0x419f20` with the null handle `0x72988c`, the flags
publisher `0x4602c0`, next move at actor+0x3bc and ground entity at actor+0xbc. XMen2.exe's own `ch_move_jump`
update (`0x4efce0`) is XML1's `0xde4a0` in shape with exactly these offsets (input bit 6 -> action 4, grounded ->
`special`, next move +0x3bc, input+0x24 |= 0xc), so the decide's fallbacks follow it (the `move` chain is taken
only when its move may start). Talent ranks are read from CStats as `balance.cpp` reads might.

## The patch

One jump at the entry of the handler lookup `0x4fd860` (`push esi; mov esi,[esp+8]` -> `jmp`): the two names
answer the DLL's handler objects (the default handler's callbacks with entry and update replaced, and a destructor
that never frees); every other name runs the five displaced bytes and the game's lookup. `npc_semi_auto` owns the
parser's call site `0x4f6bf6`; its lookup falls through to this entry, so both coexist. With `[Limits]
EntityCapacity=1` the three contact accessors are the capacity owner's bridges: they are accepted only as retail
bytes or its published jump. Every byte relied on is guarded (`src/xml1_cling_rules.hpp`); a mismatch keeps the
default handler with one log line. `[Test] ClingTrace=1` logs every contact the decide tests and every back-flip.

Online: every peer runs the handlers from the same lockstep input and positions; gameplay rule 39, revision 1,
`{installed,0,0,0}`, registered only when requested ([gameplay agreement](gameplay-agreement.md)). Nothing is saved.

`xml2_xml1_cling_test` checks the rules (thresholds, strictness, yaw, launch, the decide's order) and every guard
against `XML2_FIX_TEST_EXE`.
