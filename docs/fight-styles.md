# Fighting style registry

`[Limits] FightStyles=32` expands the retail engine's shared fighting/power-style registry. An absent key or `19` leaves the stock layout untouched. Values from 20 through 32 are accepted; invalid values leave it at 19 and produce a log message. The test pipe's `status` includes `styles used/capacity`.

The measured Legends Classic maximum is 22 distinct styles for a four-hero party. 32 leaves ten slots for additional scripted/temporary loads without crossing a bitmap word boundary. This is headroom, not a proof that every mod fits. Capacity above 32 needs another layout and clear-path audit and is rejected.

## Engine evidence

Target: the retail, unpacked XMen2.exe at image base `0x400000`, SHA-256 `146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b`. No executable or game-derived output is distributed.

Registration at `0x4ffb30` first looks up the filename. An existing entry succeeds without allocating a slot. For a new name, the check at `0x4ffb8a` reads the manager's count at `+0x1a4`; 19 returns NULL. Both package loads and stats-to-style setup use this registry. The issue is a count of distinct style files, not FightMove nodes or talent ranks.

The manager pointer is `0x78a800`. Its map starts at `+4`, its node pool at `+0x10`. The tree uses 16-byte records; style objects have stride `0xec8`. Generic tree operations retain their existing initial offsets. The following layout is derived from the constructor, allocation/free, lookup, clear, iteration and destruction paths and checked against the executable.

| Field, relative to manager | Stock 19 | Capacity 32 |
| --- | ---: | ---: |
| Tree nodes (start unchanged) | `0x10` | `0x10` |
| Free-index ring | `0x144` | `0x214` |
| Ring write / read / count | `0x194 / 0x198 / 0x19c` | `0x298 / 0x29c / 0x2a0` |
| Node bitmap | `0x1a0` | `0x2a4` |
| Used count | `0x1a4` | `0x2a8` |
| Style objects | `0x1a8` | `0x2ac` |
| Style bitmap | `0x11a80` | `0x1dbac` |
| Iterator index / map pointer | `0x11a84 / 0x11a88` | `0x1dbb0 / 0x1dbb4` |
| Following embedded manager | `0x11a8c` | `0x1dbb8` |
| Allocation size | `0x29a18` | `0x35b44` |

Each bitmap is still one dword. The two original clear paths therefore remain valid. The ring reserves N+1 dwords but wraps at N, with a separate count; the spare word is preserved. Style record contents and the following manager's internal layout are unchanged.

## Patches and guards

`src/limits_rules.hpp` lists all 72 operand writes with their instruction bytes, operand position, width, original value and layout field. Two additional guarded calls redirect the ring constructor (`0x4ff7c2`) and ring push (`0x4ffa52`) to equivalent helpers in the DLL. The original helpers' signed-byte displacements cannot represent the metadata offsets for 32 entries. Their original code remains untouched.

| Paths | Relevant addresses |
| --- | --- |
| Style-pool bitmap scan and destructor | `0x4fe9c0` |
| Node allocation and bitmap/count updates | `0x4feb10` |
| Free-ring initialization, node constructor | `0x4ff740`, `0x4ff7a0` |
| Individual removal and unload-all | `0x4ff7f0`, `0x4ff860` |
| Lookup, node free, style allocation | `0x4ff9b0`, `0x4ffa30`, `0x4ffa70` |
| Registration and capacity refusal | `0x4ffb30`, `0x4ffb8a` |
| Reset, map cleanup and manager destruction | `0x4ffc20`, `0x4ffc50`, `0x4ffc60` |
| Manager constructor and auxiliary getter | `0x4ffcc0`, `0x4ffd20` |
| First/next iteration | `0x4ffd30`, `0x4ffda0` |
| Allocation, singleton getter and shutdown | `0x4ffdf0`, `0x4ffe20`, `0x4ffe40` |
| Exception cleanup through map destructor | `0x6752d0` |

Seventeen additional guards cover dependent strides, calls, singleton access, the refusal branch, destructor clear and manager vtable entries. Every guard is checked before patching. The manager must not yet exist. All affected code pages are made writable before the first operand write, using the existing limits patch transaction. A guard, existing-manager or page-protection failure leaves this limit at stock. Other configured limits remain independent.

The game's allocator receives the new size and owns the manager through normal destruction and free; there is no alternate heap lifetime or permanent replacement singleton. The raise is installed before the first game allocation. Whole-code operand scans and raw-image searches found all references to the moved bitmap, iterator, auxiliary fields and manager size; the extra raw matches were branch/call encodings, not field accesses. The shared tree routines use unchanged initial offsets and reach node freeing through the map vtable. The exception cleanup path reaches the patched map destructor.

## Verification

The stock and raised code are tested against a locally supplied executable; public tests skip those checks when it is absent. Synthetic tests use no game content. They cover configuration, stock-layout identity, changed-byte boundaries, rejection after mutation of every guarded byte, ring wrap, and execution of the patched game constructor, node allocation/free and style destructor at capacities 19, 20, 22, 30, 31 and 32. Repeated release/reuse crosses the old limit and exercises bit 31. Canary regions detect writes beyond the node and style pools.

### Live controls and results (2026-10-03)

A fresh Legends Classic build was tested in a windowed 1280x720 harness with isolated saves and the normal ActorSlots/ResourceNames/ItemEnhancements settings. Setup used `setAutoSpend(1,1)`, `awardXPToPlayable(400000)`, `seatParty` and `loadMapKeepTeam`. The XP/party setup is staged; power execution is actual input, with energy spent, not a script damage/health shortcut.

A temporary wrapper around `0x4ffb30` recorded names, counts before/after and returned pointers. With stock 19, after the zone's unload/reload completed:

| Control | Observed registration |
| --- | --- |
| HAARP exterior, Wolverine/Cyclops/Storm/Jean | Jean accepted at 18 -> 19; all four power wheels and power use confirmed |
| Arbiter `arb2_2`, same party | Jean repeatedly returns NULL with 19 used |
| Arbiter, Jean/Cyclops/Storm/Wolverine | Jean accepted at 15 -> 16; Wolverine repeatedly returns NULL with 19 used |
| Raised 32, Arbiter, original party | Jean accepted at 19 -> 20 |
| Raised 32, Sewers `sewers3_1_2`, original party | Flight moveset and Jean accepted; Jean at 20 -> 21 |

Pre-unload lookups can still return old entries; those are not new registrations. The decisive controls are the requests after the registry has emptied and the destination's styles have loaded. This distinction is visible in the traces.

With the raise, all four heroes spent energy in both target zones and HAARP. Representative direct measurements after the Sewer reload were Wolverine 168 -> 143.28, Cyclops 162 -> 152, Storm 162 -> 152 and Jean 170 -> 160.28. HUD energy changes agreed. Actual zone reloads, saving in both target zones, an Arbiter save load and a fresh-process main-menu load of the Sewer save were exercised. The final DLL contains no registration probe. A final Arbiter reload, transition back to HAARP, four-hero power check there, return to the main menu and normal exit (code 0) also passed. All four heroes again spent energy after the final-DLL save loads; the party and intended zone were checked before accepting a result.

An intermediate minimized-window capture was stale and was discarded, along with attempts made before a load-success popup was dismissed. The final private driver checks actor identity/generation, the intended zone, no active menu/dialog, energy expenditure and distinct wheel frames. No screenshots, saves, game files, logs containing local paths or decompiler output are included in the repository.

The full Win32 Release suite and the focused `xml2_test.exe --fight-style-rules` tests pass with the local executable. These checks establish the tested parties/zones and pool boundary behavior; they do not establish every campaign path, mod combination, multiplayer session or an extended soak. Any dependency outside the modeled package union still needs capacity validation.

## Builder integration

Add `FightStyles: '32'` to `tools/xml1build/fix_ini.py`'s `LIMITS` only when the bundled XML2 Fix supports it. The 1.3.0 release does not; XML2 Fix 1.3.1 is the first release that does, so require 1.3.1 or later. Merely writing an unknown key to an older DLL does not raise the engine limit. Updating generated output also requires the builder's normal content-version policy.

For each finished combat zone, count the union of normalized distinct style filenames from permanent packages, the zone, relevant NPC/character packages, stats fightstyle/powerstyle/moveset requirements, recursive style-package dependencies and known scripted loads. Evaluate every allowed four-hero party and supported costume combination, respecting genuinely enforced parties. Shared movesets, already-preloaded hero styles and package self-references count once. Reject a union larger than the configured and supported capacity, warn at equality, and report a witness party and contributions. Unmodeled dynamic loads remain a stated uncertainty; a direct-package count below 32 cannot prove they fit. Absent a supported raise, validate against 19.
