# Effect curve pool

`[Limits] EffectCurves=3600` enlarges the retail engine's pool of effect animation curves. An absent key or `900` leaves the stock pool untouched. Values from 901 through 16384 are accepted; anything else leaves it at 900 and produces a log message. The test pipe's `status` includes `curves used/capacity`.

## What goes wrong at 900

Every loaded effect definition is a list of primitive definitions (sprites, particle clouds, trails), and each of those holds 14 animation curves: size, transparency, rotation and so on over the primitive's life. The curves of every loaded definition live in one pool of 900 records. When all 900 are in use the allocator returns curve 0 and says nothing; curve 0 is a constant 0, so every curve of a definition loaded after that point is flat zero and its particle clouds have no size. Which effects lose depends on what was loaded last: the zone's own effects when the heroes were already in the party, the heroes' power effects when they load with the zone.

Measured in an X-Men Legends I port build (legends-classic issue 68): a one-hero party needs between 552 and 848 curves depending on the zone; each further hero adds between 13 and 70. A four-hero party in the second area of the first level needs 943.

## Engine evidence

Target: the retail, unpacked XMen2.exe at image base `0x400000`, SHA-1 `7d95cdb4a9a599b982147a3389ea7aff211cb82b`. No executable or game-derived output is distributed.

The pool is a static object at `0x6fe4b8`, `0x81c0` bytes, built by its getter `0x416560` at the first call (init guard: the dword right after the object, `0x706678`; destructor `0x4163d0` registered with `atexit`).

| Field, relative to the object | Stock 900 | Capacity 3600 |
| --- | ---: | ---: |
| Records, 32 bytes each (eight floats) | `0x0` | `0x0` |
| "Built" bitmap | `0x7080` | `0x1c200` |
| Ring of N+1 free ids | `0x70f4` | `0x1c3c4` |
| Ring write / read / count | `0x7f08 / 0x7f0c / 0x7f10` | `0x1fc08 / 0x1fc0c / 0x1fc10` |
| "In use" bitmap | `0x7f14` | `0x1fc14` |
| Ids in use | `0x7f88` | `0x1fdd8` |
| The 140 preset ids (dwords) | `0x7f8c` | `0x1fddc` |
| Presets defined | `0x81bc` | `0x2000c` |
| Size | `0x81c0` | `0x20010` |

The ring reserves N+1 dwords and wraps at N with a separate count, like the engine's other fixed pools.

### Every function that touches the object

Found by data flow from the object's address, not by pattern alone: the image holds the address `0x6fe4b8` five times and no other pointer into the object; everything else receives it from the getter, and every one of the getter's 44 call sites was followed.

| Code | Role | Capacity-dependent operands |
| --- | --- | ---: |
| `0x416560` | Getter: builds once, returns the object | 2 references |
| `0x416782` in `0x416600` | Preset loader (reads the preset curve file at start-up): an inlined copy of the getter, then defines a preset | 2 references |
| `0x67d9d0` | `atexit` thunk for the destructor | 1 reference |
| `0x4162b0` | Constructor: clears both bitmaps, fills the ring, takes ids 0..139 as presets (each a constant 0 between its clamps) | 18 |
| `0x416040` | Ring fill | 12 |
| `0x415ec0` | Define the next preset (records 1..139) | 2 |
| `0x416120` | Allocator: shares a curve with an equal preset (ids 0..139 only), refuses with 0 when all are in use, else pops an id and copies the record | 4 |
| `0x415de0` | Pop a free id | 10 |
| `0x415f20` | Free an id | 13 |
| `0x4163d0` | Destructor: frees the 140 presets, then the pool clear | 1 |
| `0x415ce0` | Pool clear | 6 |
| `0x415910` | `bitset<900>::findNext`; its only caller is the pool clear | 5 |
| `0x405fd0`, `0x406040`, `0x406090` | Readers: copy or evaluate a curve by id | 3 |
| `0x409380` (ten places) | The primitive update: ten curves by id | 10 |
| `0x417bb0` | The primitive definition's destructor: releases its 14 ids unless they are presets | 14 |
| `0x416a30`, `0x4171e0` | The primitive definition's constructor (14 allocations) and the curve parser of the effect file reader (1): call the allocator, hold no offsets | 0 |
| `0x419462`, `0x4196e8` | Call the getter only to have the pool built | 0 |

That is 98 operands (displacements past the records, `900`, `899`, the bitmaps' dword count `29`) and 5 references. A reader tests the id's "in use" bit and reads record 0 when it is clear; it has no bound check against the capacity. The helpers the allocator calls (`0x415440` compares, `0x415c60` copies a record) work on one record and carry nothing that depends on the capacity. There is no reset or zone-unload path of the pool itself: a zone change destroys the zone's effect definitions, and each primitive definition's destructor releases its ids.

A second pool of the same shape sits next to it (`0x706680`, 710 records of 24 bytes, getter `0x4165a0`): the effects' vectors. It is not touched; its counter read 572 of 710 where the curves needed 943.

### Curve ids and their width

A primitive definition holds its 14 curve ids as 16-bit words at `+0x54 +0x8c +0xd4 +0xd6 +0xd8 +0xda +0xdc +0xde +0xe0 +0xe2 +0xe4 +0xe6 +0x100 +0x102` (the word at `+0x106` is a vector id of the other pool). Every store is `mov word ptr [..], ax` (14 in the constructor, one in the curve parser). Every read zero-extends: `movzx` in the primitive update, the destructor and the callers of the three readers, which pass the id on as a dword. Nothing compares an id with a constant, and no other structure receives one: ids reach the pool only through the getter's call sites above. The allocator returns the id in `eax` and computes the record as `id << 5` from the object's start.

So the width allows 65536 curves. The key stops at 16384, four and a half times the port's request; a higher ceiling needs nothing but the constant. Curve ids are run-time handles: no save and no network message carries one, so the raise changes nothing a save or another player sees.

## Patches and guards

`src/limits_rules.hpp` lists the 98 operand writes with their instruction bytes, operand position, original value and layout field, and the 5 references. The pool moves into a zero-filled block of the DLL's memory; the game's own constructor builds it at the first call of the getter and its own destructor tears it down at exit, as with the actor table. `bitset<900>::findNext` is patched in place (one caller). The init guard stays where it is. The 140 presets stay 140: their own compares (`0x8b`, `0x8c`) are not touched.

Eleven guards cover what the raise relies on: the getter and its inlined copy as a whole, the `atexit` thunk, the pool clear's call of `findNext`, the allocator's preset bound, refusal and record stride, the constructor's and the destructor's 140, the preset definer's bound, the primitive definition destructor's preset compare, and the 16-bit store of an id. Every site, reference and guard is compared with the retail bytes before anything is written, and the pool must not have been built yet. All affected code pages are made writable before the first write, in the existing limits patch transaction. A mismatch, a pool already built, an allocation failure or a page-protection failure leaves the pool at 900. Other configured limits remain independent.

## The other option: sharing equal curves

The allocator shares a curve only with the 140 presets. With the four-hero party above, the 900 records held 392 distinct curves; one curve was there 126 times. Extending the sharing to every curve in use would roughly halve the need without moving anything. It is not implemented, because it changes behaviour the raise leaves alone:

- A shared record needs a reference count: 14 releases per primitive definition would free a record other definitions still use. The count needs storage beside the pool and hooks in the allocator and in free.
- Sharing makes every allocation a search of all curves in use (a compare of eight floats each, with the engine's tolerance), where the stock allocator compares 140. Effect definitions are built while a zone loads, hundreds at a time.
- The engine's compare has a tolerance, so "equal" is not transitive: which curve a definition gets would depend on load order.
- The raise is the stock code at a different size, checked by running that code. Sharing is new code in the allocation path of every effect.

It remains possible on top of the raise, should a mod ever need more curves than the key gives.

## Verification

The stock and raised code are tested against a locally supplied executable; public tests skip those checks when it is absent. Synthetic tests use no game content. They cover the key, the layouts from 900 to 16384, stock-layout identity, changed-byte boundaries, refusal after mutation of every byte of every site, reference and guard, the whole-image facts of the ledger (five references and no more, each helper's callers), and execution of the patched game constructor, allocator, free, `findNext` and destructor on canaried blocks at capacities 900, 901, 1024, 3600 and 16384: every id handed out once up to the capacity, the refusal at the capacity and not before, first-freed-first-reused, nothing written past the object.

### Live controls and results (2026-10-05)

A fresh X-Men Legends I port build (legends-classic main) was tested in a windowed 1280x720 harness with its own test pipe and save folder and the port's usual `[Limits]`, with this DLL. Counters and every loaded effect definition's curves were read from the game's memory; the pool's address and capacity were taken from the patched code, so the same probe reads both layouts.

Staged, and named as such: the party came from `NewGameTeam` or `seatParty`, zones were entered with `loadMapKeepTeam`, the level's fire script was started directly, the leader was placed with `setPos` under a fixed camera for the fire views, and heroes were levelled with `setAutoSpend` and `awardXPToPlayable` before the power check. Real input: menu and conversation keys, hero selection, the power wheel, the save and load menus.

| Run | Key | Pool at entry | Result |
| --- | --- | ---: | --- |
| Four heroes seated at New Game, then the level's second area | none | 900/900 | Negative control: both fire definitions hold curve 0 in 4 of 5 and 3 of 5 size curves; the bench fire is a glow (about 20 flame-coloured pixels in the view) |
| Two heroes, same route | none | 812/900 | Reference: every definition intact; fires drawn |
| Four heroes, same route | 3600 | 943/3600 | Fires drawn (about 3000 flame-coloured pixels in the same view); 180 curve ids at or past 900 in use; no definition holds an id that is not in use |
| One hero at New Game, four seated with the zone load | none | 900/900 | Negative control of the other order: the fires are drawn, five power effect definitions of the heroes hold refused curves instead |
| Same | 3600 | 943/3600 | All definitions equal to the run above with the key; each hero selected and the power wheel used with real input, effects drawn |

With the key, each of the 121 distinct effect definitions loaded has the curves it has in a run where the pool was not full (99 against the two-hero run, the rest against the four-hero run without the key, where they had loaded before the pool filled), and the two load orders give identical curves. The 43 refused curves of the first negative control are real curves with the key (the pool needs exactly 43 more than 900).

Zone changes, with four heroes and the key: 4, 6 and 15 loads in three sessions, among them the two heaviest zones of the port (1047 and 1034 curves in use); the free ring wrapped in the 15-load session (highest id 3599, then reuse). A save and a load from the game's own menus: the pool fell to 705 as the zone unloaded and returned to 945. A normal quit returned exit code 0 each time.

The old static object, with the key: after start-up, the seven whole pages inside it (`0x6ff000` to `0x706000`, 86% of the object) were made no-access from outside the process and the rest of it (its first `0xb48` and last `0x678` bytes, which hold the ring positions, the counts and the preset list) was filled with a canary byte. The game ran every session above like that; the canary bytes were intact and the pages still no-access at each zone and at the end. The pages' protection before that was still copy-on-write: nothing had written to them since the image was mapped. Positive control: the same no-access pages without the key stopped the game at once.

Not covered: co-op, long play sessions (hours), every zone with every party. Whether the game's quit path runs its `atexit` list was not established in game; the destructor is exercised by the unit tests on the patched code.
