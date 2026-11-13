# The timed-event pool (`[Limits] TimedEvents`)

## What it is

Every actor update, entity think, physics step and "moved" notice of the world simulation is one record of the
engine's timed-event manager (static object `0x75d340`, vtable `0x68de14`, getter `0x4b2fc0`; body = object + 4):

- a 24-byte record `{owner handle, event*, 16 payload bytes}` taken from a free ring (alloc `0x4b2c10`, store
  `0x4b2b80`, free `0x4b2ea0`);
- an 8-byte `(time, record index)` pair in a binary heap ordered by time only (push `0x4b2c90`, pop `0x4b2de0`).

The world update pumps the manager once a frame (`0x4b2d60` -> `0x4b2d70`): while the heap's first time is before the
game time it pops the pair, fires the record (`0x4199f0`) and frees it. An event that should repeat re-arms itself
from its own fire. The game clock, menus, sound and hero switching do not go through this queue.

## The retail limit

Retail holds 1125 records and 1125 heap pairs and checks neither bound:

- the record alloc reads `ring[read]` whether or not the ring is empty, so the 1126th pending event gets the index of
  a record that is still in use: that record's event is overwritten and never fires;
- the heap push writes `pair[count]`; `pair[1125]` ends on the heap's own count, so the new event's record index
  becomes the heap count and every pair behind it is cut off the queue.

For about one lap of the ring afterwards every alloc hands out a record that is still in use. Each lost actor update
or think is a chain that never re-arms, so the world stops piece by piece while the clock keeps running. A queue in
that state reads: record count above both in-use bitmaps, heap count below the record count (down to 0), live
actors whose update is booked but has no record.

Retail has 256 World entities, about 4.4 records each. With `[Limits] EntityCapacity=1` the World holds 1024.

## The raise

`[Limits] TimedEvents=N` (1125..65536, rounded up to 32; `0` or `1125` = retail). Without the key the pool is 8192
whenever the entity capacity is raised, and untouched otherwise (plain XML2: nothing is read, written or hooked).

The module allocates a larger object of the same shape and edits the 75 operands of the manager's own code that hold
the capacity or the offset of a member behind the record array (`src/event_pool_sites.hpp`; each row carries the whole
retail instruction as its guard), including the getter's three uses of the object's address. The manager's code,
the heap order and therefore the firing order are retail's; only the bounds move. It installs after `xml1_turrets`
and `join_in_progress`, which check the manager's retail bytes and pump slot at their own install, and before the
game constructs the manager (first getter call). Those two modules read the layout and the object's address from
`event_pool_rules::installed_layout` / `installed_object`.

Online: the firing order of a given add/pump history is unchanged (`xml2_event_pool_test order-retail` and
`order-raised` print the same digest), record indices are not part of any agreed state, and the canonical queue
rebuild of a late join uses the installed layout. A peer on an older Fix differs where its own pool overflows, so the
capacity is gameplay agreement rule 65 (TimedEvents, revision 1, `{installed capacity,0,0,0}`): every peer of a session
needs the same pool. Plain XML2 and a pool kept at 1125 on purpose add no entry (they play with 1.11.0-beta.8 and older);
the default raise registers `{8192,0,0,0}`, so beta.8 and beta.9 Legends Classic PCs refuse each other in both
directions; a raise that did not install registers unverified and closes host/join (`event_pool_rules::agreement_for`).

## The watchdog (diagnostic only)

Chained on the manager's reset / add / pump slots whenever the module installs:

- `timed events <zone>: peak N of CAP (retail pool 1125 ...)` at every zone change;
- `timed events: QUEUE BROKEN ...` the first time, per zone, that after a pump the record count, heap count and free
  ring count disagree (every 32nd pump also the two in-use bitmaps);
- `timed events: the queue has been EMPTY ...` when the pump has found the heap empty for 2 game seconds after it
  held entries, with no zone change in between (zones named `menu/...` excepted);
- `timed events: pool FULL ...` when an add meets a full pool: the event is dropped instead of overwriting a live
  record (with 8192 records this needs eight pending events for every possible entity).

## Tests

`xml2_event_pool_test` (rules), and with a local retail image: `defect` (the unmodified manager: 1126 pending events
leave heap 1 / ring -1, one pump later an empty queue with 1124 records in use), `raised` (production install: guard
refusal, 8192 pending events fire once each in time order, the 8193rd is dropped, both watchdog lines), `rebuild`
(the late-join queue rebuild's own steps - retail pool init, alloc, store, push - on the raised pool), `order-retail`
/ `order-raised` (same firing-order digest), `retail-holds` (the raised expectations on the unmodified manager: fails
by design - it is what the raise fixes).

Site census: disassemble `0x4b28f0..0x4b3003` and `0x67dd30`, keep every immediate or displacement equal to one of
`0x465, 0x464, 0x24 (mov ecx only), 0x6978, 0x6a08, 0x7ba0, 0x7ba4, 0x7ba8, 0x7bac, 0x7c3c, 0x7c40, 0x9f6c, 0x232c,
0x119c, 0x11a0, 0x75d340, 0x75d344`. The only callers outside that range reach the manager through its getter and
vtable (`0x41a766` add, `0x46f0d9` pump, `0x4699e4` and `0x4845ea` reset, the exit thunk `0x67dd30`).
