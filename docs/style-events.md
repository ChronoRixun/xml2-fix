# Style event pool

`[Limits] StyleEvents=4096` expands the 1,064-event pool embedded in the fighting-style manager. An absent key or `1064` keeps stock capacity; accepted raises are 1065..8192. The style key is independent in configuration: event-only requests work at 19 styles, and style-only requests keep 1064 events. Both keys share one layout, allocation and atomic transaction. Failure of either request writes neither layout and marks both agreement rules unverified.

The retail singleton is `[0x78a800]`. The stock manager is `0x29a18` bytes: the event auxiliary starts at `+0x11a8c` and occupies `0x17f8c`. The constructor is `0x4f78e0`; allocator `0x4faea0` checks full capacity at `0x4faeb9`. Refusal returns zero and the trigger is omitted. Increasing capacity preserves this behavior at the new boundary.

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

`tools/w3_style_manager_ledger.py` generates 72 style and 84 event operand rows. The event ledger includes generation updates at `0x4facd0` and `0x4fad20`, along with allocation, resolution and teardown. Its whole-text scan checks references to every event field; manager-pointer provenance classifies all three auxiliary entry displacements as auxiliary-via-manager (already included among the 72 style rows) and rejects unclassified accesses. The generated header is checked in; never edit it manually.

Install guards include every ledger row, the four style bitmap clear spans, both refusal-count spans and a null singleton. The transaction publishes the status descriptor only after verified commit. The test pipe reports `events used/capacity` and event/style capacity refusals through that descriptor. Legacy style-only installs through 32 retain their exact byte plan and show unavailable refusal counters (`-`).

Agreement rule 53 StyleEvents uses revision 1 and `{E,0,0,0}`, present only for a non-stock request or shared-install failure. Rule 35 FightStyles is revision 2 above 32 styles. Both rules affect which combat data exists; their digest differences refuse host/join both ways, with the key named on this DLL even when locally disabled.

Build `xml2_style_manager_host`, set `XML2_FIX_TEST_EXE`, then run the host and `python tools/test_w3_style_manager.py`. The host exercises the retail routines with synthetic content services at all 21 combinations of styles 19/32/33/63/64/65/127 and events 1064/1065/4096, plus 127/8192. It checks E+1 refusal, distinct handles per generation, dynamic-mask resolution, stale-handle rejection, multiword teardown, isolation, canaries and atomic install refusals. The negative control retains the retail style clear and double-destroys slot 32. No game or engine is launched.

Static research found dword event handles, dynamic-mask resolution at `0x4faadd`, and no narrowed handle or hardcoded mask/shift consumer. This is not a complete serialization proof. Campaign transitions, saves and multiplayer with raised event capacity still require live validation; agreement protects mismatched peers, and does not establish serialization correctness.
