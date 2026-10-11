# Entity capacity (Phase A)

`[Limits] EntityCapacity=1` raises the engine's entity domain from 32 actor / 224 generic / 256 World
handles to **32 actor / 768 generic / 1024 World handles** with contiguous ten-bit dense indices. Legends
Classic emits it by default for port builds (SPEC 85); it requires `[Save] Format=lc`. Missing or `0`
keeps the retail layout. The design and its reviewed ledger are in
[entity-capacity-phase-a-plan.md](entity-capacity-phase-a-plan.md); this page is the operator's view.

Why: the stock generic arena refuses every prop, projectile or trigger past the 224th live generic entity,
without a word. The 0.2.0 census counted 943 such refusals across 116 zones (the HAARP interior freeze gun
never gets a slot; the Magneto arena sits at 224/224). Actors stay at 32: the primary controller arena and
the team records are a separate, later phase.

## What the install does

Target: retail XMen2.exe, image base `0x400000`, SHA-1 `7d95cdb4a9a599b982147a3389ea7aff211cb82b`.
Everything is prepared before a single byte is written, and written as **one transaction**
(`patch_transaction.cpp`): the shared gameplay agreement's sites, the LC save seam, `[Limits] MoveTriggers`
and this install contribute their expected bytes, writes and publication callbacks; the expectations are
unioned (overlapping guards must agree), every write must lie inside a guard, every touched page is
acquired once, the writes are applied, flushed and read back, the exact protections restored, and only
then does each contributor publish. Any failure rolls the bytes back to the expectation and verifies the
rollback; a rollback that cannot be certified refuses startup rather than run a mixed layout.

The reviewed ledger (`entity_capacity_sites.hpp`, generated from the plan's round6 `patch-spec.json`):

| Part | Sites | Shape |
| --- | --- | --- |
| Allocator | 47 literal operand edits in 0x461420..0x4618e0, 4 relocations | The native CEntityAllocator code runs on a prepared 0x189218-byte backing (generic arena 768 x 0x7d4 at +0x109a0, ring/bitmap/counts moved); `AllocatorGet` (0x461910) hands the backing out and runs the native constructor, `AllocatorShutdown` (0x4613e0) runs the native teardown, keeps the backing and retires the MoveTriggers timers |
| World | getter 0x4c7f20 | A DLL object (`entity_world.cpp`, 0x24b50 bytes) with the native 26-slot vtable semantics at 1024: contiguous tokens `(generation << 10) | index`, generations 1..0x3fff minted then the slot retires (never wraps), reset destroys without publishing the ring, allocation refuses on an empty free ring |
| Grid | getter 0x465470 | A DLL object (`entity_grid.cpp`, 0x403574 bytes): 128 blocks of 16 cells, each cell an ordered list of 1024 u16 indices, 1024 extents, two 128-byte cached sets |
| Queries | getter 0x4ab550, 25 API bridges | Three CStaticQuery (stride 0x544, 128-byte bitmap, 1024-entry collision map) and three CStaticOrder (1024 pairs); the 50-record trace pool stays native |
| Caller frames | 12 CCollide methods, CCollide slot 3 (0x44ea40), navigation 0x491210/0x493330, named query 0x4cd350 | Whole-frame replacements whose 32-byte stack sets become 128-byte sets; CPathGrid keeps its layout (owner-keyed scratch) |
| Native saves | 190 literal operand edits, 6 adapters | Zone records grow to 0x19b88 (4096 states, 1024 ordinals/dynamic/smart), the manager to 0x80c24, the stream to 0x200000 (layout **E10A2**); the CMap allocation at 0x487344 claims a prepared backing, construction at 0x487362 is wrapped (a null or wrong result stops the process: never published), release at 0x4876c0 returns the claim, the unwind at 0x673cb8 poisons it |
| Online | GCAP protocol 2, layout rule 1 revision 3 `{32,768,1024,10}` | Peers with another layout (or a release-1.4 protocol-1 record) are refused at admission |

535 union guards, 341 disjoint ranges (3,695 bytes), 31 region hashes (SHA-256 over the owner method bodies
and the native receive/lifecycle dependencies) and the 83 native owner vtable slots are checked before any
write; a mismatch leaves the executable stock and logs the first failing guard.

## Saves

The expanded native layout exists only as LC files. A Phase A save carries header `required_features`
bit 0 (`ENTITY_LAYOUT_CONTIGUOUS10`); a retail-layout build refuses such a file with "needs the
entity-capacity build" (never strips the marker). A legacy (feature-0) file loaded by a Phase A build has
its known typed entity-ID fields (the 12 registered zone-variable keys of the builder's
`Data/lcsave/typed-entity-ids.txt`) translated from the retail eight-bit layout to ten-bit at the
LC-to-native boundary: `(g, s) -> (g << 10) | s`, null when the value is not a positive retail token or
its generation exceeds 0x3fff; the log reports translated and nulled counts. The registry is verified
against the installed `Scripts` tree (fingerprint over every file); a mod that ships scripts, or a changed
tree, leaves it unverified and legacy loads are refused with the reason, while Phase A saves still load.
No other saved integer is touched. Durable script references across a reload remain a separate, later
fix (the retail engine never promised them either).

## Telemetry and the test pipe

`status` adds `entity capacity raised 32/768/1024; world live L free F retired R maxgen G refusals N
reset-time attempts A; save claim <state> ...`. `entity status` prints the same; `entity retire <slot>`
force-retires a free World slot (the co-op fixture of plan P3.4). Every allocation refusal is logged with
the requested size and the three occupancies, and allocation attempts during a World reset are counted
(the census and HAARP gates require zero).

## Failure dispositions

- Requested but refused before preparation (no LC, owners already built, not the retail executable):
  stock capacities, online admission poisoned (the layout entry is registered unverified).
- A guard, conflict or protection failure in the transaction: nothing written, stock capacities, online
  poisoned; the LC seam (same transaction) is not installed, so the game refuses to start with the usual
  LC notice.
- Save-manager claim failure (wrong size, null or foreign constructor result, exception): the process
  stops with a notice; nothing is published at 0x487373.
- A dynamic-entity factory returning null during reconstruction: the load is not confirmed and campaign
  writes stay blocked (`save core: reconstruction failure latched`).
- A native campaign transfer chunk that is too long, out of place, short before the end, or arriving
  without a buffer: dropped and logged; nothing is copied past the payload.

## Limits and what remains

Actors stay 32 (Phase B). Generation retirement can eventually exhaust slots in a very long or very hot
session (a slot released 0x3fff times retires); the status line reports the maximum generation minted.
The 50-record collision trace pool is unchanged; denser rooms can hit it more often. In-game acceptance
(HAARP interior freeze gun, Magneto arena, the densest census zones, save/cold load, two-instance co-op)
is recorded in the release's test notes.
