# Model pools (`[Engine] ModelPools`)

Every model XMen2.exe shows - heroes, enemies, props, effect models, HUD heads - is a `CModel` handle from
`CSafeResMgrI<IModel, 692, 0x2c>` plus components from seven fixed pools. All of them live in one static object, the
model aggregate at `0x7bf6d0` (0x1b698 bytes, init guard `0x7dad68`, getter `0x575620`, constructor `0x5754c0`,
destructor `0x575560` through the atexit thunk `0x67e1a0`). Model creation (`0x575c90`, used by all ten model
creators) asks `0x5756e0` first and makes nothing when any pool it needs is full - silently: an effect or a prop is
missing, and with 1.8.0-beta.1 a team-combo popup lost its second head and the game drew through the null head.

| Pool | Item size | Retail | x4 | Live count (retail, aggregate-relative) |
|---|---|---|---|---|
| IModel handles | 0x2c | 692 | 4096 | +0x1abbc |
| CModelTransform | 12 | 692 | 4096 | +0x2c00 |
| CModelAttributes | 44 | 260 | 1024 | +0x67f4 |
| CModelShaderFx | 8 | 432 | 2048 | +0x80d0 |
| CModelIGB | 56 | 400 | 2048 | +0xe5d4 |
| CModelActor | 184 | 32 | 128 | +0x103b8 |
| CModelProceduralGeometry | 16 | 220 | 1024 | +0x115bc |
| CModelParticleCloud | 92 | 40 | 128 (see below) | +0x12858 |
| CModelMovie | 8 | 1 | 1 | +0x12928 |

`x2` is half of the x4 column. The movie slot never changes.

Particle clouds and their particle systems: each `CModelParticleCloud` takes one of the particle systems of a second
static manager (`0x7dad78`, 0x1af0 bytes, guard `0x7dc868`, getter `0x578cc0`, constructor `0x578b50`, atexit thunk
`0x67e1c0`). Its layout is `free list[N] | (dword) | free count | records[N] x 24 | bitmap | 40 emitter objects x 0x90`
(N = 40 retail; the emitter objects are indexed by emitter, not by system, and stay 40). The cloud's constructor
(`0x579bc0`) pops the free list and stores NULL when it is empty, and the cloud's vt+8 (`0x578df0`) then reads through
it: with 256 clouds and 40 systems a 28-enemy power fight crashed at `0x578df3`. So the manager moves into a block of the
DLL with as many systems as clouds: 44 more rows (its count, records, bitmap and emitter displacements, the records'
8-bit displacement in `lea ecx,[edx+edx*2+records/8]` at `0x579c04` - hence at most 252 systems, an even count - the
capacity compares of its records clear `0x578570`, the getter and atexit references, two unwind funclets), five more
imm8 caves and a bitmap-clear cave; the constructor's emitter-object loop keeps 40 (`mov ebx, 40` at `0x578c29`).

The actor component pool (128) is not the binding limit for enemies: the entity allocator admits 32 actor entities
(`[Limits] EntityCapacity`, actors stay 32 until its phase B), and each takes one `CModelActor`. HUD and menu actor
models come on top.

## Layout

Every pool is the same template (verified against each pool's constructor):

    items[N] | bitmap A (N bits) | ring[N] | ring end | write | read | free | bitmap B | live | ids[N] | mask | shift

and the pools sit back to back in the order of the table (the IModel manager - a vtable, then its pool - last), so a
bigger pool moves every field behind it. `model_pools_rules.hpp` computes the layout for any capacities; the native
test (`xml2_model_pools_test [XMen2.exe]`) rebuilds the retail one field for field.

## The patch

The aggregate moves into a zero-filled block of the DLL; the game's own constructor builds it at the first getter
call and its own destructor tears it down. `model_pools_sites.hpp` (generated with capstone from the retail exe) lists
every number of the model code that depends on the layout or a capacity - 707 rows in 108 functions (751 with the particle-system manager) (and the seven unwind funclets of the SEH-framed aggregate destructor 0x575560 among them): pool-relative,
manager-relative and aggregate-relative displacements, the eight absolute references (getter, inlined getter in the
model creator, atexit thunk, the IModel pool and its bitmap), capacities, last indexes, bitmap dword counts and the
bitmap byte counts of the per-zone pool reset (`0x5853c0`-`0x585510`, which the spec's sweep had missed, with its
`0x5852a0` walk). 31 of those numbers are imm8 (`cmp r, 0x20`, `cmp [m], 0x28`, `push 0x58`): the instruction and the
ones after it up to five bytes move into a cave that does the same with an imm32 and jumps back. The actor,
particle-cloud, attributes and procedural-geometry destructors clear bitmap A a dword at a time up to its retail size
(1, 2, 9 and 7 dwords); their tails move into caves that clear the whole bitmap. The zone walk and the aggregate
destructor run these destructors and nothing else clears bitmap A, so a retail-size tail would leave the bits of every
slot above it set across a zone change and the next walk would destroy those dead slots again. The constructors' one-
and two-dword bitmap clears are left alone: they run once, on the zero-filled block.

The procedural-geometry destructor walks its bitmap with `bitset<220>::findNext` (`0x403d40`), which it shares with
the FX-template destructor (`0x4053c4`): its 220 is both the bound and the end sentinel, and raising it in place would
break the FX templates. The pool gets a copy of the helper in the caves with its own capacity in those five numbers,
and only its call (`0x571724`) moves to the copy; the helper's bytes are guarded read only. (Without it, the raised
compare after the call read the sentinel 220 as a live slot: every zone change or exit with a live procedural-geometry
item destroyed slot 220 over and over.)

`xml2_model_pools_lifecycle_test` runs the game's own constructor, allocators, frees, pool destructors, zone walk and
aggregate destructor at x2 and x4 on fake items (no slot destroyed twice or while dead, bitmaps and rings after each
reset), with the plan without these two fixes as its negative control; `scripts/check_model_pools_text.py` decodes the
whole `.text` for references, branches into the overwritten spans and shared findNext helpers.

All of it is one patch transaction: the retail bytes of every written instruction are its guards; any mismatch, a
build already made by the game, or a failed write leaves the game's own pools with one log line.

## Co-op

A full pool refuses the model and with it what needs one: with the game's own pools full, 45 spawns and an effects wave
made nothing (actors stayed 5) where `x4` made every enemy. Peers with different pools can therefore simulate different
games under load, so a raise is **gameplay-agreement rule 28 (ModelPools)**, revision 1, values
`{installed ? 1 : 0, model handles, IGB models, actors}` as installed (`x4`: `{1,4096,2048,128}`, `x2`:
`{1,2048,1024,64}`). It registers only when the key asks for a raise: absent / `off` adds no entry, so the record is the
released one and a peer on the game's own pools behaves exactly as before. A requested raise that did not install (or a
value the rule doesn't know) registers unverified, which closes host/join instead of letting a stock-pool game into a
raised room. An `x4` peer and a peer without the rule (XML2 Fix 1.8.0-beta.1, or the key off) refuse each other at
admission: the raised PC names `28 ModelPools (not on the other PC)`, a 1.8.0-beta.2 PC with the key off
`28 ModelPools (disabled on this PC)`, and 1.8.0-beta.1, which predates the rule, `a rule this PC does not have`.
`x4` and `x2` refuse naming `ModelPools`.

`[Engine] ModelHandles = N` (one-pass limits wave 1) raises the IModel handles and their transforms beyond the
profile, from its count up to 16384; the two pools share `bitset<692>::findNext` (0x56fcc0) and always move together,
and every other pool keeps the profile's size. It exists for a raised IGB cache ([igb-cache.md](igb-cache.md)): each
IGB record's model nodes take about 3.5 handles, so 1024 records fill about 88 % of x4's 4096; Legends Classic asks
for 8192. The handles are part of rule 28's values (`{1,8192,2048,128}`), so an x4 peer without the key and one with
it refuse each other naming `28 ModelPools`. Without a raise (`off`) the key is logged and ignored; a value out of
range keeps the profile's count. `xml2_model_pools_test` checks the plan against the exe at 8192 and 16384 handles
and `xml2_model_pools_lifecycle_test` runs the game's constructor, allocators, frees, zone walk and destructor at x4
with 8192 handles.

The DLL's own default stays off (plain XML2 keeps the game's pools); Legends Classic 0.3.0-beta.2 and later builds write
`[Engine] ModelPools = x4`.
