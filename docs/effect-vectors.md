# Effect vector pool

`[Limits] EffectVectors=4096` raises the effects' vector pool from 710 records to
4096. Values 711..16384 are accepted. An absent key or `710` preserves stock code
and the released agreement record; invalid values log a message and retain stock.
The status line reports `vectors used/capacity` through the published descriptor.

Each 24-byte record contains a minimum vector and a range (maximum minus minimum).
The allocator shares only the 84 presets, comparing both three-component squared
distances with the native tolerance. Equal non-preset requests still take separate
slots. Exhaustion returns vector 0, flattening a primitive's movement or offset.
The raised pool preserves these allocation, comparison, refusal and release rules.

The retail static owner is `0x706680`, getter `0x4165a0`, constructor `0x416400`,
destructor `0x416530`, and init guard `0x70b648`. The getter's first call constructs
a zero-filled DLL allocation with the game's own constructor. Its three object
references (two in the getter and the exit thunk `0x67d9e0`) are redirected. The
init guard stays at its retail address. All 84 presets remain reserved. The private
`findNext` at `0x4159b0` has exactly one caller, `0x415dc9`, and is patched in place.

| Field | Stock 710 | Raised 4096 |
| --- | ---: | ---: |
| Records, stride 24 | 0 | 0 |
| Constructed bitmap | 0x4290 | 0x18000 |
| Free ring, N+1 dwords | 0x42ec | 0x18200 |
| Write / read / free | 0x4e08 / 0x4e0c / 0x4e10 | 0x1c204 / 0x1c208 / 0x1c20c |
| Active bitmap | 0x4e14 | 0x1c210 |
| Live | 0x4e70 | 0x1c410 |
| Preset IDs | 0x4e74 | 0x1c414 |
| Presets defined | 0x4fc4 | 0x1c564 |
| Total bytes | 0x4fc8 | 0x1c568 |

`tools/w3_effect_pools_ledger.py --check --verify` reads the locally owned retail
executable (`XML2_FIX_TEST_EXE`). It decodes control flow from the owner methods,
tracks owner register aliases and getter results, classifies all metadata operands,
and checks an independent whole-text census. It also extracts constructor fields
before comparing them with the layout arithmetic. It generates
`src/effect_vector_sites.hpp`: 87 operand rows (including three object references)
and 81 guards. Unknown metadata operands fail closed. Seven word stores in
`0x416a30`, seven zero-extended reads in `0x417bb0`, and the parser's word store
confirm the 16-bit ID contract. The three readers at `0x4060d0`, `0x406130`, and
`0x406180` test the active bitmap through the raised layout. The getter has 21 callers.

One `patch_transaction` verifies every retail span and the unbuilt guard before
acquiring all code pages and writing. Allocation or protection failure retains
stock. A certified rollback frees backing; an uncertified rollback retains it and
refuses startup. The descriptor is published only after commit. Rule 57
`EffectVectors`, revision 1, advertises `{N,0,0,0}` only after installation; a failed
requested raise registers unverified. Peers with different capacities refuse by name.
The keys enlist agreement before the startup transaction, including installations
without LC or another gameplay-agreement feature.

`xml2_effect_pools_test.exe <retail-exe>` executes the native constructor,
allocator, free, reader, private `findNext`, and destructor on canaried blocks at
710 / 711 / 1024 / 4096 / 16384. It checks preset sharing, full-pool refusal, FIFO
reuse, bitmap tails, the lazy getter, every guarded-byte mutation, page failure,
rollback, conflicting guards, OFF bytes, descriptors, and agreement in both
directions. `refuse-vector` separately tests production failure and poisoned
agreement. No game is launched. Live gameplay and co-op are outside these native tests.
