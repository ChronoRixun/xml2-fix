# Settling objects without crushes

`[Game] PlacementCrush=xml1` stops one object from crushing another while it is being settled on the floor, which is
how a zone places its objects when it loads. An absent key or `xml2` leaves the game untouched; any other value is
logged and ignored. The key is for a mod that carries the first game's zones (the X-Men Legends I port sets it).

## What happens without it

A physical object's "touched by another object" function (`0x498ef0`, slot `0xac` of the object classes whose damage
function is `0x497e70`) lets the heavier of two touching objects, or the larger one, deal the other a hit of 1,000,000
damage. The victim breaks, or, where the hit is refused (with `BreakRule=xml1` most of the first game's objects refuse
it), the hit's effects still play: the default hit sound `common/melee/hit_normal`.

Touches also happen while an object is settled on the floor. That is the settle event (functor vtable `0x6873a4`, its
call `0x4729f0` = the entity's slot `0x98` with mode 3, the floor routine `0x474080`), which runs for every object when
a zone loads. The first game's X-Mansion dormitories author a desk chair tucked under a roll-top desk. On every first
entry the desk crushes the chair while it settles, the chair is knocked into the desk, the desk's impact (`0x499100`) is
a second hit, and the player hears two hits from an empty room (issue #79 of the port).

The original Xbox game plays no hit sound on the same entry: a recording of it, matched against the port's own copy of
the hit sample, stays at noise level, while the same matcher finds the sample on the original's real punches.

## What the key does

Two changes in `XMen2.exe`, after every byte they rely on is checked against the retail build:

| Address | What is there | What the fix does |
| --- | --- | --- |
| `0x6873a4` | the settle event functor's call, `0x4729f0` | the fix's own call: the same entity slot `0x98` with mode 3 and its answer handed back, with a count of settles in progress |
| `0x498ef0` | the touch function's first instruction | while a settle is in progress the touch does what the game's own early-out does (nothing); otherwise the game's function runs unchanged |

Touches outside a settle, during play, keep the game's rule. Measured in the port's harness, on fresh first entries to
the dormitories: 2 hit sounds on 3 of 3 entries without the change, 0 on 3 of 3 with it, and 13 touches refused per
entry. The test pipe's `status` reports `placement crush xml1; touches refused while settling N`.

On any other build nothing is patched and `xml2-fix.log` says why. A failed write leaves the game as it was, with one
log line; a failed rollback refuses startup.
