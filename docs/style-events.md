# Style event pool

`[Limits] StyleEvents=4096` expands the 1,064-event pool embedded in the fighting-style manager and in the two loaders' temporary lookup objects. An absent key or `1064` keeps stock capacity; accepted raises are 1065..8192. The style key is independent in configuration: event-only requests work at 19 styles, and style-only requests keep 1064 events. Both keys share one layout and atomic transaction, including every event owner's allocation. Failure of either request writes neither layout and marks both agreement rules unverified.

The retail singleton is `[0x78a800]`. The stock manager is `0x29a18` bytes: the event auxiliary starts at `+0x11a8c` and occupies `0x17f8c`. The constructor is `0x4f78e0`; allocator `0x4faea0` checks full capacity at `0x4faeb9`. Refusal returns zero and the trigger is omitted. Increasing capacity preserves this behavior at the new boundary.

The event constructor and its accessors are shared. Patching their operands while leaving a temporary owner at `0x1a890` caused the main-menu crash at `0x4f78f4`. The complete ownership closure in the retail image is:

| Owner / allocation path | Size operand | Constructor / event call | Event base | Retail size | Size at 64 styles / 4096 events |
| --- | --- | --- | --- | --- | --- |
| `CCombatStyles`, allocator `0x5605e0`, heap `0xe` | `0x4ffdf4` | `0x4ffcc0` / `0x4ffd0b` | `+0x11a8c` retail, `+0x3b740` at 64 styles | `0x29a18` | `0x97b5c` |
| Temporary lookup, loader `0x4f7ef0`, allocator `0x5606e0`, heap `0xf`, tag `0x41` | `0x4f7f2f` | `0x4f7ea0` / `0x4f7edb` | `+0x2904` | `0x1a890` | `0x5ed20` |
| Temporary lookup, style parser `0x5010b0`, allocator `0x5606e0`, heap `0xf`, tag `0x40` | `0x5010ed` | `0x4f7ea0` / `0x4f7edb` | `+0x2904` | `0x1a890` | `0x5ed20` |

The fix grows **every owner** consistently, the alternative allowed by the repair brief. The temporary objects end exactly at `0x2904 + event_size(E)`; their prefix, vtable `0x691cfc`, other pools, event-base offsets, and cleanup calls stay fixed. This keeps one event representation across the shared constructor, parser, resolver, release and destructor, including their exception paths. It avoids an instance-dependent split and copied exception-handling code. At 4096 each temporary grows by `0x44490` bytes (279,696); it is freed when its loader returns. Temporary admission also uses E. The `0x5606e0` wrapper passes the requested size unchanged to `0x5605e0` on heap `0xf`; the heap setup at `0x55e65b..0x55e662` creates it with zero pre-size, as for heap `0xe`. No game heap is allocated by installation itself.

The only direct constructor calls are `0x4f7edb` and `0x4ffd0b`. The temporary constructor's only callers are `0x4f7f42` and `0x501100`. A whole-text scan verifies these sets, both size pushes, all five `+0x2904` references, and the absence of stored pointers to either constructor. No standalone, stack, static, or further embedded owner was found. The lifecycle and access paths are:

| Routine | Direct callers / owner routes |
| --- | --- |
| `0x4f78e0` construct | temporary constructor `0x4f7edb`; manager `0x4ffd0b` |
| `0x4f75e0` initialize free ring | constructor `0x4f791d` |
| `0x4f61c0` destroy constructed records / clear bitmap | temporary cleanup `0x4f8208`, `0x5011ef`; manager destructor `0x4ffc8b` |
| `0x4f58d0` bitmap scan | teardown `0x4f6244` |
| `0x4f8e50` validate handle | resolver `0x4faacd`; checked release `0x4fbd4d` |
| `0x4f8fd0` pop free ring | allocation `0x4faede` |
| `0x4faac0` resolve handle | `0x4f64e7`, `0x4f6533`, `0x4f6579`, `0x4f6aec`, `0x4f7dcb`, `0x4f7e26`, `0x50138d`, `0x501614` |
| `0x4faaf0` destroy / return slot | generation wrappers `0x4facd9`, `0x4fad31` |
| `0x4facd0` release slot index / bump generation | allocation recovery `0x4fb055` |
| `0x4fad20` release handle / bump generation | checked release `0x4fbd60` |
| `0x4fbd40` checked release | `0x4f643c`, `0x4f69a6`, `0x4f6ac3`, `0x4f8065` |
| `0x4faea0` allocate event | manager copy `0x4f7e0c`; common event parser `0x501367` |
| `0x4ffd20` manager pool getter, vtable slot `+0x28` | manager consumers use virtual dispatch; temporary pool passed by `0x5017fe` through `0x501630` to the common parser; lookup uses `0x50160e` |

There is no separate reset that requires another object layout: teardown clears constructed records; construction rebuilds the ring and generations. The two temporary cleanup routes then destroy their prefix pool at `+0x1110` and free the owner through `0x560700`. At E=1064 neither new allocation operand is written, preserving the retail event path and the exact legacy style-only plan.

The auxiliary has E records of `0x54` bytes, a constructed bitmap of `ceil(E/32)` dwords, a free-index ring of E+1 dwords, write/read/free counts, a second bitmap of `ceil(E/32)` dwords, a live count, E dword generation IDs, and dynamic mask/shift fields. The ring reserves E+1 entries but wraps at E. The constructor's seed is E-1; it derives the next all-ones mask and its bit count. At E=4096 these are `0xfff` and 12. Non-power-of-two values (including 1065) use the same retail algorithm and do not assume a power-of-two capacity.

| Auxiliary field | E=1064 | E=4096 |
| --- | ---: | ---: |
| Constructed bitmap | `0x15d20` | `0x54000` |
| Free ring | `0x15da8` | `0x54200` |
| Write / read / free | `0x16e4c / 0x16e50 / 0x16e54` | `0x58204 / 0x58208 / 0x5820c` |
| Used bitmap / live | `0x16e58 / 0x16ee0` | `0x58210 / 0x58410` |
| Generation IDs | `0x16ee4` | `0x58414` |
| Mask / shift | `0x17f84 / 0x17f88` | `0x5c414 / 0x5c418` |
| Event part size | `0x17f8c` | `0x5c41c` |

At styles/events 64/4096, the auxiliary starts at manager `+0x3b740`; the total game-heap allocation is `0x97b5c` bytes. No alternate singleton or heap lifetime is introduced. Retail construction, registration, iteration, event generation changes and destruction use the relocated fields.

`tools/w3_style_manager_ledger.py` generates 72 style and 85 event operand rows (83 shared-code operands and both temporary allocation sizes). The event ledger includes generation updates at `0x4facd0` and `0x4fad20`, along with allocation, resolution and teardown. Its whole-text scan checks references to every event field; manager-pointer provenance classifies all three auxiliary entry displacements as auxiliary-via-manager (already included among the 72 style rows) and rejects unclassified accesses. Only the three `mov ecx,34` bitmap-clear counts are capacity operands. The stack write at `0x4fb5ef` is exception-unwind state 34 between states 33 and 35, and remains retail; changing it to the raised bitmap word count would index the wrong unwind state if subtype construction throws. The generated header is checked in; never edit it manually.

Install guards include every ledger row, the four style bitmap clear spans, both refusal-count spans and a null singleton. Event raises also guard the complete temporary constructor, both allocation-to-constructor sequences, and the invariant exception-unwind state at `0x4fb5ef`. The transaction publishes the status descriptor only after verified commit. The test pipe reports `events used/capacity` and event/style capacity refusals through that descriptor; the refusal counter includes temporary pools too. Legacy style-only installs through 32 retain their exact byte plan and show unavailable refusal counters (`-`).

Agreement rule 53 StyleEvents uses revision 1 and `{E,0,0,0}`, present only for a non-stock request or shared-install failure. Rule 35 FightStyles is revision 2 above 32 styles. Both rules affect which combat data exists; their digest differences refuse host/join both ways, with the key named on this DLL even when locally disabled.

Build `xml2_style_manager_host`, set `XML2_FIX_TEST_EXE`, then run the host and `python tools/test_w3_style_manager.py`. The host exercises the retail routines with synthetic content services at all 21 combinations of styles 19/32/33/63/64/65/127 and events 1064/1065/4096, plus 127/8192. It checks E+1 refusal, distinct handles per generation, dynamic-mask resolution, stale-handle rejection, multiword teardown, isolation, canaries and atomic install refusals. It additionally runs all three actual allocation sequences and complete owner constructors at E=1064/1065/2048/4096/8192, with each requested allocation ending immediately at a 2 MiB no-access tail and a prefix canary. The reserved tail covers the maximum raised footprint, so displaced writes cannot jump over a single guard page into unrelated committed memory. Each owner fills, resolves, releases and refills its pool, then tears down live events. Negative controls run in child copies of the fixture, restore each old temporary size at 4096, and require a vectored exception observer to confirm a constructor write into the protected tail before exiting with sentinel `0xe04f78f4`; the existing style-clear control double-destroys slot 32. No game or engine entry point is launched.

Static research found dword event handles, dynamic-mask resolution at `0x4faadd`, and no narrowed handle or hardcoded mask/shift consumer. This is not a complete serialization proof. Campaign transitions, saves and multiplayer with raised event capacity still require live validation; agreement protects mismatched peers, and does not establish serialization correctness.
