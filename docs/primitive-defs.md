# Primitive definition pool

`[Limits] PrimitiveDefs=2048` raises the primitive-definition heap pool from 816
records to 2048. Values 817..8192 are accepted. An absent key or `816` preserves
stock code and contributes no agreement rule. Invalid values log a message and
retain stock. Status reports `prim defs used/capacity` through the descriptor.

The singleton pointer is `0x70b64c`, getter `0x418860`, constructor `0x418720`,
and teardown `0x4189f0` through `0x418850`. The native lazy getter still allocates
from heap pool 14; the DLL changes the allocation size and metadata operands in
place. The 0x118-byte record stride stays native. Full-pool admission at
`0x4188a0` still refuses an extra primitive. An effect stores pointers to its
primitive definitions, rather than generation handles. Primitives per effect
remain the separate ten-entry limit unless W3-7 is installed.

| Field | Stock 816 | Raised 2048 |
| --- | ---: | ---: |
| Records, stride 0x118 | 0 | 0 |
| Constructed bitmap | 0x37c80 | 0x8c000 |
| Free ring, N+1 dwords | 0x37ce8 | 0x8c100 |
| Write / read / free | 0x389ac / 0x389b0 / 0x389b4 | 0x8e104 / 0x8e108 / 0x8e10c |
| Active bitmap | 0x389b8 | 0x8e110 |
| Live | 0x38a20 | 0x8e210 |
| Generation IDs | 0x38a24 | 0x8e214 |
| Mask / shift | 0x396e4 / 0x396e8 | 0x90214 / 0x90218 |
| Total bytes | 0x396ec | 0x9021c |

The constructor's seed changes from 815 to N-1. Its native loop computes mask
0x7ff and shift 11 at 2048 (stock 0x3ff / 10). The pointer-release routine
`0x4187d0` reads the IDs, mask and shift and advances the generation. These extra
consumers are included alongside allocation, free, clear and construction.
`findNext` at `0x4183a0` is private: its sole caller is `0x4184c7`.

`tools/w3_effect_pools_ledger.py --check --verify` reads the locally owned retail
executable (`XML2_FIX_TEST_EXE`) and generates `src/primitive_def_sites.hpp`:
74 operand rows and 37 guards. It follows method owner aliases and the getter's
results, rejects unclassified owner metadata operands, and independently scans the
whole text for missing metadata consumers. The field model is extracted from the
native constructor's bitmap clears, ring initialization and generation tail,
then compared field for field with stock arithmetic and emitted at raised/max sizes.
All edits are imm32 or disp32, including the REP STOSD counts and mask seed.

Installation requires a null singleton and every retail ledger span. One
`patch_transaction` makes all pages writable before its first write and publishes
the descriptor only after verified commit. A requested failure invalidates online
agreement; an uncertified rollback refuses startup. Rule 56 `PrimitiveDefs`,
revision 1, carries `{N,0,0,0}` on success. Mismatches are refused in both
directions by key name. Neither saves nor release identity change.
The keys enlist agreement before the startup transaction, so the same contract
also works in standalone installations without LC.

`xml2_effect_pools_test.exe <retail-exe>` covers TM1 ledger, TM2 layouts, TM3 OFF,
TM4 native pool constructor/admission/allocator/free/pointer-release/findNext/teardown
at 816 / 817 / 2048 / 8192, TM7 refusals and rollback, and TM8 agreement. Invented
record construction/destruction services count every visit, while the native pool
code manages allocation, generation and iteration. Every pool and effect object
has canaries. The N+1 admission must leave every pool byte unchanged. FIFO reuse,
generation advance and bitmap tails are checked. `refuse-primitive` separately
checks production failure and poisoned agreement. No game is launched.
