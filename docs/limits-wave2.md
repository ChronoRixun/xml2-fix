# One-pass limits, wave 2: the save-bound raises (design, revision 2)

Status: design, revision 2, 2026-10-25 (Fable lead; owner-approved brief `task-fable-save-plan.md`). No code. Revision
1 is kept as `limits-wave2.v1.md`; the eight CONFIRMED findings of the GPT Astra review (report
`20261025-165428-gpt-astra-high-81ac`) are answered in section 12, each by number. The one-page summary is
`DECISIONS.md` beside this file.

Scope: the rows of gap-audit PLAN 2.5 whose capacity is written into a save (E2, E3, E3b, E3c, E7, E9, E14, E15),
landing in **one** native save-layout change, "W2", with the owner's one-pass rule (every save-bound and engine limit
raised together, with headroom for mods). Wave 1 (`limits/wave1`) shipped the runtime-only raises. E4 (live actors)
is a separate design (`agent-work/e4-actors-design`); section 9 says how it composes with W2.

Every address is retail XMen2.exe, SHA-1 `7d95cdb4a9a599b982147a3389ea7aff211cb82b`, image base 0x400000. Bytes
quoted in this revision were re-read from `D:/Projects/xml1-port/research/characters/ghidra/XMen2.exe` on 2026-10-25
(`agent-work/limits-wave2/w2_verify.py` and `w2_verify2.py`, read-only, pefile + capstone). "SPEC-old" is `agent-work/save-step0/SPEC-old-save.md`
revision 2.1; "SPEC-new" is `agent-work/save-step1/SPEC-new-save.md` revision 2.3; "layout.hpp" is
`src/lcsave/layout.hpp`; "rules.hpp" is `src/save_format_rules.hpp`.

Evidence labels: **V** re-derived from the executable or the source in this pass; **S** taken from a spec table whose
bytes `verify_step0.py` checks (PASS 1268 / FAIL 0 on 2026-10-25, Astra) or Astra's 52-instruction check; **RE** an
open check routed in section 11; **U** unverified.

---

## 1. What changes and what does not

| Layer | Changes in W2 | Does not change |
|---|---|---|
| LC container (`.lcsave`, format 1) | nothing in the wire layout: no new section, no new field width, no major bump. `required_features` bit 1 is **used** (section 7). The `ZONE` section may carry 8 records (its count was never limited) | header, section table, every section's field layout, the catalogue, `KEYS`, the converter's input (retail `.save`) |
| Native stream (what `CGame` vt+0x208 / vt+0x20c read and write, the 0x200000 payload) | the structures of section 3: item head, instance head, mission snapshot, zone record, zone-record count, talent IDs | the stream order (SPEC-old 1.3), the tags, the payload size 0x200000 and the wrapper 0x200078 (section 3.8), the string table 0x5808, the fog 0x34c, the profile 0x228, the script variables 0x840 |
| Engine (XMen2.exe, patched by the DLL at start-up) | the per-row pools and widths of section 4, all in one transaction | the actor structures (E4), the IGB cache and model pools (wave 1 / ModelPools), everything outside the ledger |
| Online | the 2 MiB native image is a W2 image on both PCs; a new agreement rule (section 8) refuses other layouts by name | the GCAP protocol 4 framing, the 108-byte record, FastTransfer, rule 1 |

The LC file is keyed by catalogue ID and its readers' ceilings are above every target (limits-check "Save format");
that still holds. What is new in revision 2 is that **every** native structure the stream carries is specified
(revision 1 listed only the index widths), the eight zone records live natively (not behind a DLL swap), and the
layout is selected by identity.

---

## 2. Targets (frozen)

| Row | Retail | E10A2 (today) | **W2** | Sentinel / "none" at W2 | Why this number |
|---|---|---|---|---|---|
| E2 item definitions | 160 (byte index) | 160 | **1024** (u16 index) | index >= live count = no definition (as today); the random picker's and the by-name lookup's "not found" becomes 0xffff | PLAN E2: 88 + 116 XML1 loot + shops + mods; 1024 = 3.7x MUA 2016's ~275 |
| E3 enhancement records | 375 (fix: 512, ceiling 1024) | 512 | **4096** (u16, append-only) | own-enhancement slot "none" 0x00ff -> **0xffff**; record 255 becomes usable | PLAN E3 |
| E3b attached enhancements | 375 | 375 | **4096** | handle = index + generation (as today) | the full loot table hits this first |
| E3c item instances / world-item slots | 256 / 150 | 256 / 150 | **1024 / 1024** | entity slot u8 0xff -> **u16 0xffff**; zone-record slot u16 (section 3.5) | PLAN E3c |
| E7 talents | pool 100 x 3; hero ID = (stats index + 1) x 100 + position; shared 0..98 (cap 99) | same | pool **256**; **multiplier M = 200**, shared 0..198 (cap 199); M is a descriptor constant 101..220 | - | G11 census sizes it (section 4.7); 220 is the 16-bit ceiling for 297 stats slots (297 x 220 + 195 < 65536) |
| E9 zone memory | 5 records, LRU eviction | 5 | **8 records natively**, LRU eviction kept | - | D13: XML1 keeps 7 with no eviction; 8 with eviction is the agreed deliberate difference |
| E14 objectives | 299 registered (byte index), 75 per act, 25 files | same | **1024 / 127 per act / 64 files** (R4) | file index u8 0xff unchanged (64 fits) | PLAN E14 |
| E15 zone record caps | states 768, ordinals 256, dynamic 300, smart 768, i16 positions | 4096 / 1024 / 1024 / 1024, i16 | **states 4096 (incl. the unused record 0, 4095 usable), ordinals 4096, dynamic 2048, smart 2048, f32 positions**; `CSmartEntMgr` 2048 | state record 0 stays unused (count starts at 1, 0x46656b) | SPEC-new R6; R16: XML1 = 600 records / 599 usable per zone, so 4095 usable is 6.8x |
| E8 character definitions | 21 | 21 | **21** (unchanged) | - | owner decision 7 |

"Includes the sentinel": the state count follows XML1's convention (600 records, 599 usable): **4096 state records
including record 0**. Ordinals are 1-based with slot (ordinal - 1): all 4096 slots usable. Dynamic and smart lists:
2048 usable each. Instances: 1024 pool indices 0..1023. World slots 0..1023. Definitions 0..1023. Enhancement records
0..4095 (65535 reserved as "none"). Objectives 0..1023.

---

## 3. The W2 native layout: a frozen contract

`lcsave::NativeLayout` (layout.hpp) gets a third descriptor, `w2_layout`, and the fields below. `make_native_layout`
takes the state record size and the record count as parameters (today both are constants: `sizeof(OldStateRecord)`,
`native_manager_records = 5`). rules.hpp's `native_profile` gets the same fields and a `w2_profile`; the two are
cross-checked by `static_assert` field for field (as `retail_profile` / `e10a2_profile` are today).

### 3.1 Descriptor fields (new or changed)

| Field | RETAIL | E10A2 | W2 |
|---|---|---|---|
| `id` | "RETAIL" | "E10A2" | **"W2"** |
| `feature_bits` (what the writer may set) | 0 | 1 | **3** (bit 0 always, bit 1 conditional: section 7) |
| `index_bits` | 8 | 10 | 10 |
| `definitions`, `definition_index_bytes` | 160, 1 | 160, 1 | **1024, 2** |
| `def_count_bytes` | 1 | 1 | **2** |
| `enhancement_records`, `attached_enhancements` | 375 (fix 512), 375 | 512, 375 | **4096, 4096** |
| `instances`, `world_slots`, `world_slot_bytes` | 256, 150, 1 (payload byte) | same | **1024, 1024, 2 (own field)** |
| `objectives`, `objectives_per_act`, `objective_files` | 300 (299 registrable), 75, 25 | same | **1024, 127, 64** |
| `snapshot_size` | 0x26c | 0x26c | **0x540** |
| `state_record_size`, `position` | 0x18, i16 | 0x18, i16 | **0x20, f32** |
| `states / ordinals / dynamic / smart` | 768 / 256 / 300 / 768 | 4096 / 1024 / 1024 / 1024 | **4096 / 4096 / 2048 / 2048** |
| `record_count` | 5 | 5 | **8** |
| `talent_multiplier`, `talent_pool` | 100, 100 | 100, 100 | **200, 256** |
| `payload`, `usable_end`, `wrapper` | 0x2fc00 / 0x2fc00 / 0x2fc78 | 0x200000 / 0x200000 / 0x200078 | **0x200000 / 0x200000 / 0x200078** (unchanged, section 3.8) |

### 3.2 Item head (SPEC-old 1.4; `OldItemHead`)

| Offset (W2) | Width | Field | Retail |
|---|---|---|---|
| 0 | **u16** | definition count (live count; the loader reads it into a local and never uses it, SPEC-old 1.4) | u8 |
| 2 | **1024 x {u8 quantity, u8 flag}** = 0x800 | definition state pairs by index; pairs past the live count are 0 | 160 x 2 |
| 0x802 | u32 | tech bits | 0x141 |
| 0x806 | u32 | health items | 0x145 |
| 0x80a | u32 | energy items | 0x149 |
| 0x80e | 6 x u32 | key counts | 0x14d |
| 0x826 | u32 | carried count N1, then N1 instances | 0x165 |
| .. | u32 | world count N2, then N2 x {u32 slot, instance} | |

Head size 0x826 (retail 0x165). The pairs stay {u8, u8}: the engine's quantity accessor (definition vt+0x40 / vt+0x58)
is a byte today and the LC `DEFSTATE` already widens the quantity to u32 (SPEC-new 2.5), so a wider native pair would
only be meaningful with a wider definition field; **not in W2** (noted as a later, break-free change: the LC field is
already wide).

### 3.3 Instance head (SPEC-old 1.5; `OldInstanceHead`)

| Offset (W2) | Width | Field | Retail |
|---|---|---|---|
| 0 | **u16** | definition index | u8 |
| 2 | u8 | name A (+0x2c), 0xff none | 1 |
| 3 | u8 | name B (+0x2d), 0xff none | 2 |
| 4 | u8 | special (+0x2f), 0xff none | 3 |
| 5 | u8 | location (+0x2e) | 4 |
| 6 | 3 x (u32 count + n x {u16 record, u32 value}) | prefix, own, suffix lists (unchanged element) | 5 |

Head 6 bytes (retail 5). The three name/special bytes stay u8 (SPEC-new R8: audit only; their tables are 32 / 4 / 128
and the LC file stores IDs, so a later widening is break-free).

### 3.4 Mission snapshot (SPEC-old 1.9; `OldSnapshot`) - three copies: two stack frames and `[MAP: MISSION STATE]`

| Offset (W2) | Width | Field | Retail |
|---|---|---|---|
| 0x000 | char[0x40] | zone name | same |
| 0x040 | 24 x f32 | seat poses | same |
| 0x0a0 | u32 | pad | same |
| 0x0a4 | 4 x char[0x20] | seat names | same |
| 0x124 | u8 | pad | same |
| 0x125 | **1024 x u8** | objective state by index (bits 0-5 counter, 6 flag, 7 disabled at start) | 300 + the never-written 301st byte |
| 0x525 | 3 x u8 | pad (the retail "301st byte" and u16 pad fold into this) | 0x251 u8 + 0x252 u16 |
| 0x528 | 3 x f32 | anchor position | 0x254 |
| 0x534 | 3 x f32 | anchor orientation | 0x260 |

Snapshot **0x540** (retail 0x26c); the side-mission stack block becomes 2 x 0x540 + u32 pad + u32 count = **0xa88**
(retail 0x4e0). SPEC-new 4.4 already budgeted 3 x 1,344.

### 3.5 Entity state record (SPEC-old 1.13; `OldStateRecord`)

| Offset (W2) | Width | Field | Retail |
|---|---|---|---|
| 0x00 | u32 | definition-name handle | same |
| 0x04 | u32 | instance-name handle | same |
| 0x08 | 8 x u8 | class payload, **unchanged**: the cursor helpers 0x465580 / 0x4655d0 keep their 8-byte bound (`cmp ecx, 8` at 0x465597 / 0x4655e7, V) and every class serializer keeps its bytes | same |
| 0x10 | u8 | flags A (bit 0 = owns a world-item slot) | same |
| 0x11 | u8 | flags B | same |
| 0x12 | **u16** | **world-item slot, 0xffff = none** (new field; valid when flags A bit 0) | (positions started here) |
| 0x14 | **3 x f32** | position | 0x12: 3 x i16 |
| 0x20 | | record size **0x20** | 0x18 |

Why the slot is a new field and not a wider payload byte: `CInventoryEntity` writes its slot as one byte at payload +0
(record +8) and the i16 quantity at +9..+0xa after `CPhysicalEntity`'s serializer ran (0x479a70: `call 0x496930`, then
`call 0x465570`, then a 1-byte write of entity+0x320, V); bytes +0xb / +0xc are `CActionEntity`'s conditional class
bytes (SPEC-old 1.13), so a 4-byte {u16 slot, i16 qty} does not fit the 3 free payload bytes. In W2 the payload keeps
{+8: 0, +9..+0xa: i16 quantity} and the u16 slot has its own field at +0x12; the payload helpers are not patched.
The LC `STATE.payload` kind 2 ({u32 handle, i32 quantity, 5 bytes}) already abstracts both layouts (SPEC-new 2.12).

Positions: the packer 0x4656c0 (round + clamp to +/-32767) and the reader 0x4654f0 (`movsx ecx, word [esi+0x12]`, V)
are replaced for W2 by DLL functions that store and load the three floats at +0x14 unchanged (bit-exact). The LC
`STATE.position` is already 3 x f32.

### 3.6 Zone record (SPEC-old 1.13; computed by `make_native_layout` with state size 0x20)

| Offset (W2) | Field | E10A2 | Retail |
|---|---|---|---|
| 0x00000 | zone-name handle | 0 | 0 |
| 0x00004 | 4096 x 0x20 state records | 4096 x 0x18 | 768 x 0x18 |
| 0x20004 | pad | 0x18004 | 0x4804 |
| 0x20008 | state count | 0x18008 | 0x4808 |
| 0x2000c | 4096 x u16 ordinal map (slot = ordinal - 1) | 1024 at 0x1800c | 256 at 0x480c |
| 0x2200c | pad | | 0x4a0c |
| 0x22010 | 2048 x u16 dynamic list | 1024 at 0x18810 | 300 at 0x4a10 |
| 0x23010 | pad | | 0x4c68 |
| 0x23014 | dynamic count | 0x19014 | 0x4c6c |
| 0x23018 | 2048 x u16 smart list | 1024 at 0x19018 | 768 at 0x4c70 |
| 0x24018 | pad | | 0x5270 |
| 0x2401c | smart count | 0x1981c | 0x5274 |
| 0x24020 | fog 0x34c | 0x19820 | 0x5278 |
| 0x2436c | 5 x u32 conversation | 0x19b6c | 0x55c4 |
| 0x24380 | time stamp f32 | 0x19b80 | 0x55d8 |
| 0x24384 | flag u8 + 3 pad | 0x19b84 | 0x55dc |
| **0x24388** | record size (148,360 bytes) | 0x19b88 | 0x55e0 |

Ordinal map 4096: the native ordinal writer addresses `[record + ordinal*2 + (map - 2)]` (rules.hpp
`ordinal_write_displacement`), so the W2 displacement is 0x2000a. The zone loader's own stack arrays of 1,024 placed
entities (SPEC-old 1.13 "Ordinals") are a separate runtime ceiling: an ordinal above 1024 cannot occur until that
loader is widened, so the 4096 map is headroom, not a claim. **Census item:** the largest XML1 zone's placement count
(builder data) decides whether the loader arrays are raised in this pass (section 11, C3).

### 3.7 Manager (`CMap`+0x3b0 allocation; SPEC-old 2.8)

| Offset (W2) | Field | E10A2 | Retail |
|---|---|---|---|
| 0 | **8** records x 0x24388 | 5 x 0x19b88 | 5 x 0x55e0 |
| 0x121c40 | pad | 0x80bd8 | 0x1ad60 |
| 0x121c44 | record count | 0x80bdc | 0x1ad64 |
| 0x121c48 | mission snapshot (**0x540**) | 0x80be0 (0x26c) | 0x1ad68 |
| 0x122188 | flag | 0x80e4c | 0x1afd4 |
| 0x12218c | newest time stamp | 0x80e50 | 0x1afd8 |
| **0x122190** | manager size (1,188,240 bytes) | 0x80c24 | 0x1afdc |

The manager is already a DLL-prepared backing (Phase A claims the allocation at 0x487344 and wraps construction at
0x487362; `entity-capacity.md`). The 43 container-offset sites, the record-size sites and the allocation push are the
rows of SPEC-old 2.8 and 2.7 that Phase A's ledger already regenerates from the layout; W2 regenerates them again with
`record_count = 8` and the W2 record size. Three imm8 sites change value only: 0x46699c `cmp eax, 5` -> 8 (eviction
trigger), 0x487461 / 0x487685 `push 5` -> 8 (constructor / destructor iteration); 0x4669b0 `lea edx, [esi + 0xabb8]`
(record 1's time stamp = record size + time-stamp offset) is regenerated (W2: 0x24388 + 0x24380 = 0x48708).

### 3.8 The bounded stream (answers Astra 6)

Computed from the W2 structures (SPEC-new 4.4 method, every count at its W2 ceiling):

| Part | Formula | Bytes |
|---|---|---|
| Tags | 13 x 52 | 676 |
| Item head | 2 + 1024 x 2 + 12 + 24 | 2,086 |
| Item instances (bounded by the 1024 pool) | 1024 x (6 + 12 + 4) | 22,528 |
| Attached enhancements (bounded by the 4096 pool) | 4096 x 6 | 24,576 |
| Hero blocks without equipment | 21 x 303 | 6,363 |
| Unlocked list, restore flag, 22 restore records | 600 + 1 + 22 x 213 | 5,287 |
| Game block, Danger Room, Review, profile, script variables, town centres | 220 + 1,633 + 128 + 552 + 2,112 + 4 | 4,649 |
| Three mission snapshots + stack tail | 3 x 0x540 + 8 | 4,040 |
| Zone string table | 0x5808 | 22,536 |
| Zone records | 4 + 8 x 0x24388 | 1,186,884 |
| **Total** | | **1,279,625 = 0x138689, 61.0 % of 0x200000** |

So the payload stays 0x200000, the wrapper 0x200078, the 10,486 transfer chunks and FastTransfer unchanged. Revision
1's "0x280000" was an arithmetic error (4096 x 0x20 x 5 = 0xa0000); the payload is **not** grown. The manager backing
(1.19 MB, DLL memory) and the stream wrapper (2 MiB, allocated by the game at the four sites of SPEC-old 2.10) are
different allocations; only the former changes size.

### 3.9 Layout identity (answers Astra 5)

`save_format::use_native_profile` selects the lcsave descriptor by **id**, not by `usable_end`
(`save_format.cpp:2056` today: `chosen.usable_end == e10a2_profile.usable_end ? e10a2 : retail`, which would pick E10A2
for W2 because both use 0x200000). The entity-capacity install publishes `{profile, lcsave::native_layout(profile.name)}`
and `use_native_profile` refuses (startup veto, as the LC hook failures do) when the lcsave descriptor is null or its
structural fields differ from the profile's. `lc_known_required_features` becomes `native_layout->feature_bits`
(RETAIL 0, E10A2 1, W2 3).

Supported combinations (anything else is refused before any write):

| `[Save] Format` | `[Limits] EntityCapacity` | `[Limits] SaveLayout` | Layout | Agreement |
|---|---|---|---|---|
| absent / off | any | any | the game's own files, **stock bytes**, no layout rule | released record |
| lc | absent / 0 | absent | RETAIL | rule 1 rev 1 |
| lc | 1 | absent | E10A2 | rule 1 rev 3 |
| lc | 1 | **w2** | **W2** | rule 1 rev 3 (or rev 4 with E4 stage 2) + **rule 45 NativeSaveLayout** (registered only for W2) |
| lc | absent / 0 | w2 | refused: "SaveLayout=w2 needs EntityCapacity=1" (W2 tokens are contiguous) | unverified, online closed |

One key selects the whole layout and **every** engine raise of section 4; the raises are not individually selectable,
because the stream is one contract (the owner's one-pass rule, in code). `[Limits] ItemEnhancements` under W2 must be
absent or 4096 (W2 sets it; another value is refused with a log line).

**Atomic publication.** The W2 install is one contribution to the entity-capacity startup transaction
(`patch_transaction`): every site guarded on retail bytes, every replaced function on a region hash, everything
prepared before the first write; on commit the publication callbacks run together: (1) the lcsave layout pointer and
`lc_known_required_features`, (2) the converter's demand caps (`NativeLoadDemand` compared against the descriptor, not
the literals 256 / 375 / 150), (3) the save menu's feature-name table (section 7), (4) rule 45 registration. A failed
guard or write rolls the bytes back to retail, publishes nothing, registers rule 45 unverified (online closed) and the
LC seam is not installed (the game refuses to start with the LC notice, as a Phase A failure does today). **OFF
(no key) = stock bytes**, checked by the whole-exe text gate (section 10, T4).

---

## 4. Per-row engine changes

Each row names its ledger (SPEC-old section, instruction count), the kinds of edit, and what is **RE** (open). The
site tables themselves are SPEC-old's; this page does not repeat 900 rows.

### 4.1 E2 item definitions 160 -> 1024 (SPEC-old 2.2: 164 instructions, 10 assumption groups; S)

Kinds: (a) **relocate** the inline definition array (manager+4, 160 x 0x3c) and the state pairs (manager+0x785c) to the
manager's end, as `item_layout_for` already moves the enhancement pool: allocation 0x480a25 grows, the 18 `imul`
strides and 6 loops keep 0x3c, constructor / destructor iterators (0x480725-0x48073a, 0x48098f-0x4809a2, 0x673aa4) run
the same constructors on the new storage; the cap 0x480430 (`cmp [esp+0x1c], 0xa0`, imm32, V) -> 1024 in place;
(b) **widen** the count byte +0x2588 (31 sites) - **RE-1:** is +0x2589 free (object layout: +0x258c is the XML handle,
so +0x2589..+0x258b are 3 bytes; U)? If free, the count becomes u16 in place and the 31 byte accesses become word
accesses (most are `movzx`/`mov` byte forms -> word forms of the same length or one byte longer -> caves where longer);
(c) **widen the index end to end**: 14 accessor thunks compare `al` -> `ax`; 0x47c250's result; the 3 external callers
of vt+0x18; the picker 0x47b590's byte loop counter and byte result (`inc al` 0x47b74e; cave: the loop becomes a DLL
function); the by-name lookup 0x47e320's byte result (`or al, 0xff` 0x47e389 -> `or eax, -1`, V) and its 13 callers
(each must consume eax, not al: 8 of them are the `call [edx+0x78]` sites listed as "works unchanged once vt+0x78
returns a full-width index" - **RE-2:** confirm each of the 13 does not truncate); the loader's `inc cl` 0x480574; the
Danger Room reward byte (stored `mov [edi+8], al` 0x4d47da, V; read 0x4c8fe1 / 0x4ca138) - **RE-3:** is reward
record +9 free for a u16? else a DLL side table keyed by reward record; the global 0x6d83bc (9 sites) -> a DLL u32
with the 9 sites re-aimed; (d) **stream**: `OldItemHead` / `OldInstanceHead` per section 3 (0x47c52c / 0x47f81f write
and read the 0x140 state block -> count-sized W2 block; 0x481622 / 0x482b98 u8 -> u16 definition).
Sentinel: 0xff "not found" (picker 0x47d6a0, 0x47ecdf; by-name 0x47e389) -> 0xffff; the compare sites change with the
width. Risk **L** (mixed inventory: constructors, strides, sentinels, picker counters, rewards, lookup callers - not
164 interchangeable edits).

### 4.2 E3 enhancement records 512 -> 4096 (SPEC-old 2.3: 70 instructions; 41 are the fix's own `item_sites`; S)

`max_item_enhancements` 1024 -> 4096 (`limits_rules.hpp:56`; the 41 sites compute from N). Sentinel 0x47ad22
`cmp ax, 0xff` (V) -> `cmp ax, 0xffff` in place (same length), and the constructor default 0xffff (0x47b924-0x47b932)
already reads as none. The signed setter 0x45e510 limits record numbers to 32767: fine for 4096. **Affix pickers
0x47e690 / 0x47ee90 (`sub esp, 0x5f4`, 375 ints each, V): whole-function replacement** by DLL functions of the same
calling convention using descriptor-sized arrays (4096 x 4 = 16 KiB each, static), so random affixes are drawn from
the whole table (PLAN E3 acceptance: 1,000 drops cover every affix id). **RE-4 (Astra):** the two pickers' exact
semantics (candidate filter, weight, RNG calls and order) as a spec the reimplementation and its differential test
(section 10, T6) follow. Record numbers are append-only (saves store them), so old saves keep meaning. Risk **M**.

### 4.3 E3b attached enhancements 375 -> 4096 (SPEC-old 2.4: 87 instructions; S)

Relocate `CPoolInterface<IAttachedEnhancements, 375>` (object 0x2984 at +4, element 0x14): allocation 0x45fb0e grows;
68 layout / capacity instructions in 0x45f2a0-0x45fb60 re-based from N; a second clone of `bitset<375>::findNext`
0x45e5f0 (5 bounds) with 0x45f309 re-aimed at it; **0x45f920 returning 0 (full) becomes a logged error at 0x481938
instead of a silent skip** (SPEC-new R1). The converter preflight (`api_engine.cpp:453`, `reverse.cpp:909`,
`save_format.cpp:1225-1228`) reads `attached_enhancements` from the descriptor. The pointer-flow sweep of this pool
is done (SPEC-old U16: 31 further accesses, nothing new). Risk **M**, ledger complete.

### 4.4 E3c item instances 256 -> 1024, world-item slots 150 -> 1024 (SPEC-old 2.5: 148 + the 52 of `pool_omissions.json`; S)

Instance pool (object at [0x72a524], 256 x 0x34 + handle tables, 0x3c60 bytes): 47 sites of 2.5 **plus the 52
omissions**, all byte-verified by Astra: ring initialisation 0x483090 (12: fields +0x3824/+0x3828/+0x382c, ring array
+0x3420, capacities `cmp eax, 0x100` 0x4830be, `mov eax, 0xff` 0x4830d7, `cmp edx, 0x100` 0x4830e7), ring pop 0x483410
(5: +0x408/+0x40c), destructor walk 0x482a20 (6: bitmap +0x3400, two `cmp edx, 0x100`, the helper call and its
`cmp eax, 0x100`), **the shared 256-bit bitset helper 0x44d0d0 (5 bounds) - cloned for 1024 and the pool's calls
re-aimed; the original stays for its other callers**, three eight-dword bitmap clears (0x48310b, 0x48313c, 0x482a97:
each block replaced as a unit by a 32-dword clear in a cave). The pool relocates to a DLL backing of 1024 x 0x34 +
tables (as the Phase A allocator did).
World-item slot table (manager+0x72a0, 150 x 8; bitmap +0x7750, 5 dwords): 17 x imm 0x96 -> 1024, 44 table / bitmap
sites re-based to the relocated table at the manager's end (32-dword bitmap), the unwind handlers' `add ecx, 0x72a0`
immediates (R2 rows). **Live entity slot** `CInventoryEntity`+0x320: u8 with 0xff "none" at 6 sites (0x479c64,
0x479d27, 0x479da9 `mov byte [ebp+0x320], 0xff` V, 0x479f59, 0x47a117, 0x47aa1c) -> u16 0xffff; **RE-5:** +0x321 is
presumably alignment padding before the i16 at +0x322 (U) - confirm from the constructor and every +0x320 access;
the 5 callers of manager vt+0x120 (0x47d390, instance by slot) pass a zero-extended byte -> word; the state
save / restore 0x479a70 / 0x479fc0 write and read the new u16 field at record +0x12 (section 3.5) under W2 (bridges at
the slot write and the slot read; the quantity bytes unchanged). A full slot table today sets 0xff and takes
0x479f87 (SPEC-old U7, "what the player sees" untraced) - with 1024 slots and the preflight counting world items
against free capacity this path is not expected to run; it is logged if it does. Risk **M-L** (99 pool sites, a
shared helper).

### 4.5 E14 objectives 299 -> 1024, 75 -> 127 per act, 25 -> 64 files (SPEC-old 2.6: 105 instructions; S)

Registrar cap 0x4885c0 `cmp [esp+0x1c], 0x12b; jae` (imm32, V; retail registers indices 0..298) -> 0x400 in place
(indices 0..1023 registrable); per-act cap 0x4899e0 `cmp [edi+0x7d78], 0x4b` (imm8, V) -> 0x7f in place;
the loaded-objective pool (manager+8, 75 x 0x1ac, bitmap +0x7d64, count +0x7d70) -> 127 x 0x1ac relocated (17 stride
sites, 16 bitmap / count sites, 2 unrolled clears -> caves); **the index byte at loaded objective +0x1a9 -> u16** (13
sites; `mov [eax+0x1a9], dl` 0x489a01 V) - **RE-6:** +0x1aa free (record 0x1ac bytes; U)? else a DLL side table keyed by
record; the **per-file base index table 0x72b590** (u8 x 25; stored 0x489252, read `movzx edx, byte [ebx+0x72b590]`
0x489bdf) -> a DLL u16 x 64 table, both sites re-aimed; the file index (`CMissionMgr`+4, u8, 0xff none) unchanged (64
fits); the global state array 0x72b118 (300 bytes; 15 sites), its 300-byte clears (4) and the two snapshot copies
(0x4b dwords + 1 byte -> 0x100 dwords) -> a DLL 1024-byte array; **the snapshot 0x26c -> 0x540 everywhere it is laid
out**: constructor 0x4874b0 (`push 0x12c` 0x487505 -> 0x400), the side-mission stack static block 0x72b5b0 (frames
at +0, +0x26c; pad +0x4d8; count +0x4dc; push 0x5f3630) -> a DLL block with frames at +0, +0x540, pad +0xa80, count
+0xa84, and the manager's snapshot at its W2 offset (section 3.7); capture 0x48a170 (+ 0x489a80), restore 0x489300
(+ 0x4892e0) re-based. SPEC-old U15 (other narrow holders: script functions, objective menu) is closed for the stock
functions; **RE-7:** XML2 Fix's own script functions and `objective_text.cpp`'s journal readers must be checked for a
byte-width objective index when R4 is written. Risk **M**.

### 4.6 E15 zone record caps and float positions (SPEC-old 2.7: 200 instructions; S) and E9 (2.8: 48; S)

Record and manager offsets: regenerated from the W2 layout exactly as Phase A regenerates them from E10A2 (the 190
literal operand edits of `entity_capacity_sites.hpp` "Native saves" are the same rows with new values; the generator
takes `state_record_size`, the four counts and `record_count`). New in W2 against E10A2: (a) the state record stride
0x18 -> 0x20 at every site that walks state records (the two serializer functions 0x4656c0 / 0x4654f0 replaced; the
capture 0x4664d0's per-record advance; the restore 0x465c10 / 0x465c50 / 0x4667e0 reads; the record clear 0x465ec0's
loops: `mov ecx, 0x100` 0x465f32 -> 4096 ordinal-map clear, V) - **RE-8 (Astra):** the complete list of instructions
that add or multiply 0x18 as the state stride or address a state field at +0x12 (positions) in 0x465000-0x467000 and
the class serializers (section 3.5 lists the writers); (b) dynamic / smart 1024 -> 2048 and ordinals 1024 -> 4096
(bounds 0x46667a, 0x46668a, 0x4666f3; the capture bridges `save_core_rules::capture_bridges` take the profile);
(c) **`CSmartEntMgr` (static object, pool 768 x 0x18, count +0x4868; 9 capacity immediates, 24 bitmap dwords, 45 rows)
replaced through its getter 0x4b76f0** by a DLL object at 2048 with W2 state records (0x20) - **RE-9:** its 45 rows are
listed (SPEC-old 2.7 R2 block) but the object's method set and vtable need the same owner-method region hashes Phase
A used for the World and Grid; (d) **E9: `record_count` 8** (section 3.7: 3 imm8 edits + regenerated offsets +
the allocation size). The capture's world-handle scan `cmp ebx, 0x100` 0x4666bd (V) -> 1024 (the World is already
1024 handles; SPEC-new R6 "capture bound taken from the world table's size"). Risk **M** (mechanical, large).

### 4.7 E7 talents: pool 100 -> 256, multiplier 100 -> M = 200 (answers Astra 4)

Verified (V): the hero ID helper **0x4bdc12 is a 7-byte function `imul eax, eax, 0x64; ret 4`** with no direct `call`
(0 E8 callers in .text: it is reached through a vtable or register call - **RE-10:** its callers); shared loading
pushes the cap **0x63 at 0x4c05d9** (`push 0x63`, imm8) into `call 0x4c0460` at 0x4c05de; the three registration pools
compare `cmp ecx, 0x64` at **0x4bdfe4 / 0x4be7e4 / 0x4be044** (V) and store the pool's "next" at +0x12cc / +0xfb8 /
+0x7dc. A sweep of 0x4bd000-0x4c1000 (V) finds **27 immediates of 0x64 / 0x63** (20 `cmp r, 0x64`, 5 `mov eax, 0x63`,
2 `mov r, 0x64`) and **no divide-by-100 magic (0x51eb851f)**: the engine does not decode an owner from a talent ID in
that range. Design:

- **Pool 256**: the three pools' capacity compares and every "last index 0x63" / "count 0x64" of the 27 are
  classified (**RE-11, Astra:** which belong to the three pools, which to the per-hero talent tables, which are
  unrelated) and raised in place where imm8 allows (256 does not: `cmp ecx, 0x64` -> `cmp ecx, 0x100` lengthens the
  instruction by 3 bytes -> caves, or **pool 127** stays imm8-friendly). Decision: **pool 256 with caves** (owner
  rule: headroom). The pools' storage (the dword tables at +0x644 etc.) relocates with the registry object -
  **RE-12:** the registry object's layout and allocation.
- **Multiplier M = 200**: 0x4bdc12 is replaced by a 5-byte `jmp` to a DLL function `imul eax, eax, M; ret 4`;
  the `push 0x63` is reached by re-aiming `call 0x4c0460` (0x4c05de) at a thunk that writes `M - 1` into the pushed
  argument slot and jumps to 0x4c0460 (the thunk is the only way to change an imm8 push to 199 in place). M is a
  descriptor constant: **101..220** (297 stats slots x 220 + 195 < 65536 keeps the saved u16 `OldTalentEntry.id`; a
  stats index past the 21 heroes can own talents, so the bound is the stats table's, not the herostat's). The G11
  census (team bonuses) and the boss-immunity census may lower or raise M before the build without re-design; the
  W2 default is 200 (shared 0..198: 63 used today = 32 %).
- **Consumers**: `tables.cpp:557` (`engine_id = (group + 1) * 100 + position`), `api.cpp:207` (`shared = tid < 100`),
  `reverse.cpp:190` (`e.id / 100` owner check) take the descriptor's M; **historical imports keep 100**: the legacy
  eras of `legacy.lcm` resolve old u16 ids with the multiplier of the era that wrote them (today always 100), and the
  LC file stores catalogue IDs (TALN), so **re-basing M changes no LC byte and never marks a save** (section 7).
  The missing-talent insertion (`save-format.md` "Missing talents on load") classifies shared talents by `< M`.
- **Builder**: V44 (SPEC 74) counts talents against 100 / 9 files; it takes the pool and M from the same descriptor
  (`fix_ini.py` LIMITS). The runtime guards at 0x4c011e / 0x4c04a8 (save-guards.md) keep watching the raised values.

Risk **M** (27 immediates to classify; the thunk and the helper are trivial).

---

## 5. E9: ownership and lifecycle of records 6-8 (answers Astra 3)

**Decision: the eight records live in the native manager** (section 3.7), not in a DLL-side cache that swaps records
in and out of a 5-record manager (revision 1). Reasons, all V:

1. The eviction path 0x466960 (called at zone leave from 0x484900 at 0x484969 - 0x484900's one caller is at
   0x48462c, in the function starting near 0x4844a0 - and from the serializer 0x484a60 at 0x484a96) clears the oldest record with **0x465ec0, which frees the record's world-item slots**
   (flags A bit 0; SPEC-old 1.13, 0x479c10). A sixth record held outside the manager would have to carry its item
   instances outside the item manager too and re-insert them on swap-in: a second item lifecycle. Natively at 8, the
   engine's own lifecycle handles dependent items, names (the zone string table, 0x800 slots) and provenance.
2. `retain_sections()` (`convert.cpp:1318`) carries only `LGCY` and unknown KEEP sections; it never merged extra `ZONE`
   records and does not need to: the `ZONE` section is written from the native stream's record list (`old.zones`,
   `oldsave.cpp:561`, whose ceiling `ceil_zone_records` becomes `native.record_count`).
3. Online: the host's 2 MiB image carries all 8 records; the joiner's `native_zone_metadata` (save_format.cpp:1182)
   infers metadata for every record in the image with the W2 layout. No sidecar, no second transport.
4. The restore timing is the engine's (zone load 0x484ce0 restores the record found by name 0x465d80).

Lifecycle (V from the exe): records are **captured** when a zone is left (0x48462c -> 0x484900 -> 0x466960; skipped
when the current zone name at CMap+0x20 is empty, the manager flag +0x1afd4 is set, 0x4c8770 returns true (untraced;
I: a Danger Room / course check) or the zone's vt+0xa4 "nosave" is set) and when a save is written (0x484a60 -> 0x466960); **evicted** (LRU by time stamp) when a
zone without a record is captured and the count equals the capacity (0x46699c); **reset** by 0x465f90 (clears every
record through 0x465ec0, count 0, flag 0, string table cleared), whose only callers are the constructor 0x487440
(0x487492) and **0x484370**, a `CMap` virtual method (no direct callers) that also clears `CSmartEntMgr` (vt+0xc via
0x4b76f0) when the flag is clear. The port already hooks the campaign reset at 0x5f2e70 (`campaign_reset_hook`) and
clears its own capture metadata there.

**R17 (does the port clear zone memory at mission boundaries like XML1's "DumpZoneMission"?)** Static answer: XML2 PC
has no such clear; records survive mission boundaries until evicted or until 0x484370 runs. What calls 0x484370's
vtable slot is **RE-13 (Astra):** find the slot in `CMap`'s vtable and every caller (expected: new game / resetgame).
Decision regardless of the answer: **W2 keeps XML2's lifecycle** (survive missions, LRU at 8) and records it as the
second deliberate difference beside eviction (D13); the live gate (section 10, L3) walks a mission boundary and checks
that the previous mission's zones are still remembered.

Cached-capture metadata (`cached_captures`, provenance per record) already follows the manager's records by zone name
and time stamp; with 8 records its vector simply holds up to 8.

---

## 6. The converter at W2

Both directions take the layout explicitly (never inferred from a size; `old_stream_to_new`, `new_to_old_stream`,
`native_load_demand`, `native_zone_metadata`, `loaded_zone_metadata` already do). Changes:

- **`oldsave.cpp`** read / write of `OldItemHead`, `OldInstanceHead`, `OldSnapshot`, `OldStateRecord` and the zone-record
  count become layout-driven (as the zone record already is); the model structs gain the wide fields with the retail
  widths as a special case. The reader refuses a record count above `native.record_count`.
- **`reverse.cpp`** (LC -> native): definition index, world slot and objective index narrow to the layout's width with
  the layout's limit in the message; positions: W2 stores the float as is (finite required), RETAIL / E10A2 keep the
  round-and-clamp refusal at line 100 / 446; talent ids use the descriptor's M; `ceil_zone_records` ->
  `native.record_count`; the demand check (line 909) and `api_engine.cpp:453` compare against the descriptor.
- **`rules.hpp` walkers** (`hero_heaps` at 1143-1240: `item_head_size = 0x165`, `instance_fixed = 5`, the equipped
  walk at 1332 / 1528) take the profile: the missing-talent insertion, the level catch-up and the class-growth migration
  walk W2 streams correctly (Astra 1). A `static_assert` ties `item_head_size(profile)` to `sizeof(OldItemHead<layout>)`.
- **`convert.cpp`** `item_pools {375, 512}` -> the descriptor; the importer (`convert_slot`) stays retail-only.
- **Python reference** (`tools/lcsave/layout.py` in legends-classic): the same W2 descriptor, so the vectors are
  exported by the reference and the C++ is checked against them (section 10, codec gate).

Migration paths:

| From | To | What happens |
|---|---|---|
| Retail `.save` (XML2's own file, or a 0.1.x port save) | W2 build | the one-time importer reads it with RETAIL (unchanged) and writes an LC file with feature bits 0; the W2 build then loads that LC file as below |
| LC feature 0 (0.2.x) or feature 1 (0.3.x / 0.4.x) | W2 build | loads: `new_to_old_stream` targets W2; integers -> exact floats, catalogue ids -> W2 indices and talent ids; typed fields translated as today; the next write carries bit 0 and, when section 7 says so, bit 1 |
| W2 build's save **without** bit 1 | 0.4.0 / 0.4.1 (E10A2) build | loads: the LC layout is unchanged and every value fits the E10A2 native layout by construction (section 7) |
| W2 build's save **with** bit 1 | any older build | refused before any native application: "required feature bits 0x00000002 not implemented (needs a newer version)" (`container.cpp:602`), slot listed "(needs a newer version)" (rules.hpp:925), marker never stripped, `.bak` never tried (D10) |
| W2 build's save | retail-layout LC build (no capacity) | refused by bit 0 as today ("needs the entity-capacity build") |
| any LC | a mod build with another catalogue | unchanged: unresolved ids refuse intact (no partial parking) |

---

## 7. Compatibility marking: representability (answers Astra 7)

`required_features`: bit 0 `ENTITY_LAYOUT_CONTIGUOUS10` (Phase A; every capacity build sets it, as today); **bit 1
`NATIVE_W2`**; bit 2 is reserved for E4 stage 2 (`ACTOR_CAPACITY_96`; the E4 design's section 7 says "bit 1" and must
be corrected to bit 2 - section 9).

**Rule:** the W2 writer sets bit 1 exactly when the file it is about to write **cannot be applied by the E10A2 layout**.
It decides that by construction, not by a hand list: after building the LC file, the writer runs the E10A2 reverse
translation (`new_to_old_stream` with `e10a2_layout`, in memory, no engine) over it; a refusal or a drop sets bit 1 and
the log names the first reason (which is the limit's own message: "state position not representable", "more zone
records than the container holds", "native instance/enhancement/world pool exhausted", "world handle outside native
pool", the objective / definition narrowing messages). That dry run is exact because it is the very code an older
build runs; the hand list below is the **test oracle** (section 10, boundary tests), not the implementation:

| Sets bit 1 | Does not set bit 1 |
|---|---|
| a state position that is not an integer in -32767..32767 (0.5 is not representable: `reverse.cpp:100`) | a re-based talent ID (the LC file stores catalogue IDs; the native id is recomputed per build) |
| more than 5 zone records | 8 records of which 3 are empty / never captured (the writer writes only live records) |
| more than 256 instances, 375 attached elements or 150 world items in the stream | an item whose definition index is > 159 on this build but whose catalogue id resolves on the older build too (the index is per build; the older build's own demand check decides) |
| a world-item handle >= 150 | retained KEEP / LGCY sections (opaque, carried; layout-independent) |
| a mission snapshot with an objective whose index on this build is >= 300 - **by the same dry run** (the older build resolves ids itself; what the dry run can test is whether the reverse translation fits 300 states, which it does by catalogue count) | the level catch-up / class-growth optional bits (optional features) |

The dry run costs one extra reverse pass per write (milliseconds; the writer already re-reads its own output).

**Named refusal.** From W2 on, a build that meets an unknown required bit keeps the generic text; a build that knows
the bit's **name but not the feature** cannot exist (knowing the bit means implementing it). What W2 adds is the table
`{bit -> "needs Legends Classic 0.4.2 (XML2 Fix 1.9) or newer"}` for its *own* future: the save menu and the log name
the minimum version for bits this build knows about but refuses for another reason (a W2 build with `SaveLayout`
absent, loading a bit-1 file: "this save needs the W2 save layout; enable it or use the build that wrote it"). Released
1.8.x binaries keep their generic message; that is accepted (Astra 7, last point).

---

## 8. Online (answers Astra 5, the agreement part)

| Change | Online effect | Covered by |
|---|---|---|
| W2 layout (all structures of section 3) | the host's 2 MiB native image and every joiner's reader must agree on the stream layout; equal actor capacities do not imply equal streams | **rule 45 `NativeSaveLayout`** (ID requested from the coordinator; `_rule-ids.md` 45+ free), revision 1, values `{layout ordinal (0 RETAIL, 1 E10A2, 2 W2) \| record_count << 8 \| structural revision << 16, definitions \| enhancement_records << 16, instances \| world_slots << 16, objectives \| talent_multiplier << 16}`; registered **only when W2 is installed** (as rules 28 and 31 do: absent key, RETAIL or E10A2 add no entry, so the released record of every 0.4.x build is unchanged and a 0.4.1 peer is refused as "a rule this PC does not have" on its side and as `45 NativeSaveLayout` on the W2 side); a requested W2 that did not install registers unverified and closes host / join. The structural revision increments whenever equal capacities could still encode the stream differently |
| E2 / E3 / E3b / E3c pools | a full pool refuses an item, a bonus or a drop (lockstep state) | rule 45 values 1-2; `ItemEnhancements=4096` also registers rule 34 as today |
| E7 pool and M | registration refusals and the talent-id space are lockstep state | rule 45 value 3 (M); the pool is implied by the layout ordinal |
| E9 8 records | eviction order and what a revisited zone remembers are lockstep state | rule 45 value 0 (record_count) |
| E14 / E15 | capture completeness and positions are lockstep state | layout ordinal |
| Campaign transfer (kind 3, 'G' / 'H', FastTransfer) | unchanged: same 2 MiB image, same chunks; the joiner's `native_zone_metadata` reads 8 records with the W2 layout | admission guarantees equal layouts; `xml2_campaign_sender_test` / `xml2_reliable_receive_test` with W2 images |
| Join in progress / fresh tail | unchanged (protocol 4) | - |

Rule 1 (entity layout) is **not** reused for the stream layout (Astra 5: equal actor capacities can admit incompatible
streams); rule 45 covers the whole native format.

---

## 9. Composition with E4 (one transaction, one agreement)

- Both installs are contributions to the **same** startup `patch_transaction`; the transaction is all-or-nothing, so a
  requested W2 and a requested E4 stage either both verify or nothing is written (fail-closed as a unit; each
  registers its rule unverified on failure).
- W2 touches no actor structure; E4 touches no save structure (its design section 7: nothing actor-indexed is saved).
  Independent keys: `[Limits] SaveLayout=w2` (W2), `ActorPools=124` / `Actors=96` (E4). Supported together or alone,
  each requiring `EntityCapacity=1`.
- Feature bits: W2 bit 1, **E4 stage 2 bit 2** (correction to the E4 design's section 7).
- Agreement: rule 1 rev 3 / rev 4 (E4), rule 44 ActorAI (E4), rule 45 NativeSaveLayout (W2). Three independent entries;
  a mismatch names the rule.
- Descriptor pattern: W2 publishes one read-only `native_layout` descriptor the way E4 publishes its CAI descriptor;
  consumers (converter, menu, preflight, bridges) read it after publication, never a literal.
- Order: W2 and E4 stage 1 can be built in parallel; E4 stage 2 lands after W2 only because both touch the generic
  arena's base regeneration (E4 5.2 "Phase A generic operands regenerated") and one regeneration pass is simpler to
  verify than two interleaved ones.

---

## 10. Gates (answers Astra 8)

Four gates, in order; none is skipped.

**G1 Codec (deterministic bytes, no engine).**
- The Python reference exports W2 vectors (`export_lcsave_typed_vectors.py` gains `w2-reverse`, `w2-forward`,
  `w2-refuse-downgrade`, `w2-bit1-*` modes); `lcsave_durable_parity` gets the W2 column: for each fixture the C++
  output equals the reference's bytes **exactly** (header CRCs, body CRC, feature bits, section CRCs included). No
  "identical except the writer version": the fixtures pin the writer version and `saved_at`.
- Round trips: LC -> W2 native -> LC. The comparison is **canonical dump equality** (`dump`) plus an explicit allowed
  difference list `{META.saved_at, META.writer, header.writer_version, header.save_sequence, header.body_crc,
  header.header_crc, header.required_features}`; every other difference fails. Migrations the ledger records (talent
  catch-up, dangling-item normalisation) are asserted by name in the fixture, not waved through.
- The legacy test vectors (`XML2_FIX_LEGACY_VECTORS_DIR`) pass on all three layouts.
- `xml2_test --save-format-rules` with the W2 profile: `hero_heaps` walks a W2 stream; `static_assert`s of section 3.

**G2 Engine (the union host executing retail code on the patched image, as Phase A's `entity_capacity_test`).**
- T1 ledger generator vs SPEC-old tables (addresses, counts per group: 164 / 70 / 87 / 148+52 / 105 / 200 / 48) - equal.
- T2 OFF byte-identical: key absent -> hash of every page W2 could touch equals the wave-1 baseline; agreement record
  digest unchanged.
- T3 ledger mutation: one guard byte changed -> nothing written, rule 45 unverified.
- T4 whole-exe text gate (`check_model_pools_text.py` style) per row: no remaining reader of the old width / old
  offset for each relocated pool and field (E2 definition index, E14 +0x1a9 and 0x72b590, E3c +0x320 and the slot table,
  E15 +0x12 positions and the 0x18 stride).
- T5 item manager on the host: load a stream with 1023 definitions, pick (picker 0x47b590 replacement) and look up by
  name an index > 255; Danger Room reward of definition 300; save; load; compare.
- T6 affix pickers differential: retail picker on a stock host vs the DLL picker, same RNG seed, tables of 10 / 375
  / 376 / 4096 affixes -> identical picks at <= 375, full coverage above.
- T7 instance pool 1023 / 1024 / 1025 (refuse at 1024), ring pop / recycle / destructor walk, bitmap helper clone vs
  original at 256; world slots 1023 / 1024; entity slot u16 round trip through 0x479a70 / 0x479fc0 bridges.
- T8 attached pool 4095 / 4096 / 4097 (error logged, no silent skip).
- T9 zone record: capture / restore of 4095 states, 4096 ordinals, 2048 dynamic, 2048 smart, positions 0.5 and
  +/-40000 bit-exact, world slot 1023 in the +0x12 field; eviction at the 9th zone (LRU); 0x465ec0 clear frees slots;
  `CSmartEntMgr` replacement at 2048 vs the static object at 768 (differential at <= 768).
- T10 objectives: registrar 1023 / 1024 (refuse), per act 127, 64 files; state of objective 255 / 256 / 1023 survives
  act change, side-mission push / pop (two frames) and a snapshot copy.
- T11 talents: pools 255 / 256 / 257; M = 200: ids of stats index 21 position 10 = 4410; shared cap 199 through the
  thunk; `missing_talents` classification.
- T12 fail-closed: a page made read-only / a guard wrong / backing allocation failing -> nothing written, rule 45
  unverified, online closed, offline stock.
- T13 agreement: rule 45 present only with the key; E10A2 vs W2 refused naming `45 NativeSaveLayout` both ways; a
  wave-1 peer "a rule this PC does not have".

**G3 Live-save (real saves; the owner's 0.4.2 gate).** Collect, read-only, into
`agent-work/limits-wave2/real-saves/<owner>/`: every `SaveLC\*.lcsave`, `*.bak`, `profile.lcsave`,
`legacy-import.txt` and any remaining `Save\*.save` of Owen (this PC), Shelley (the OJAMD-Shared drop) and the testers
(asked via the Discord thread, zipped), plus the 82-save corpus (round5 SHA-1s). Run (no game): for each file (a)
`dump` on the current build; (b) W2 reverse -> W2 native -> forward -> `dump`; (c) compare under the G1 difference
list; (d) the E10A2 dry run of (b)'s output and the expected bit-1 verdict (expected: **none** of the real saves sets
bit 1 - they were written within the old caps); (e) the preflight demand numbers (instances, attached, world items,
zone records, objectives, max definition index, talents) tabulated as the census. Then in game, each owner's newest
save on the W2 candidate: cold load, play one zone change, save, reload (cold), and one two-instance co-op join of
that campaign (agent-run, private desktop, the launch rules). A failure of any real save blocks the tag.

**G4 Boundary tests** (synthetic saves, both as G1 fixtures and as in-game `save dump` checks): definition count 161 /
255 / 256 / 257 / 1024 and a definition-state pair at index 1023; instance definition index 255 / 256 / 1023; own
enhancement record 255 (readable, no longer "none") and 4095; attached 375 / 376 / 4096; instances 256 / 257 / 1024;
world item handle 149 / 150 / 255 / 256 / 1023; objective 255 / 256 / 1023 in a snapshot, in both stack frames and
across a mission file whose base index is > 255; zone records 5 / 6 / 8 (then the 9th evicts); state positions 0.5,
-32768.5, 40000.0 (bit-exact); talent ids at M = 200 for stats index 1 and 21; a mission-boundary walk (R17: zones of
the previous mission still remembered); each case loaded on an E10A2 build (refused by name or loaded, per section 7's
oracle) and on the W2 build.

What fixtures **cannot** certify (Astra 8): pointer-flow completeness of the relocated pools (U16) - covered by the
guard-page proof in G2 (the old storage is left unmapped in a test build; any access faults), native bonus application
after load (G3 in game: compare an item's stats before save and after reload), campaign transitions and co-op (G3).

---

## 11. Patch ledger by kind, with verification status, and the open RE checks

### 11.1 Ledger (counts are SPEC-old's; every row's bytes are S unless marked)

| Kind | Rows | Verified | Route |
|---|---|---|---|
| In-place imm8 / imm32 value edits | E2 cap 0x480430; E3 sentinel 0x47ad22; E14 0x4885c0, 0x4899e0; E9 0x46699c, 0x487461, 0x487685; E15 capture bound 0x4666bd, clear 0x465f32; E7 the imm8-compatible subset of the 27 | V (this pass) / S | Sol |
| disp32 / imm32 re-base from the layout (generator) | E15 + E9: the 200 + 48 rows (190 already generated by Phase A's `entity_capacity_sites.hpp`); E2 definition array and state pairs; E3b 68 rows; E3c 44 table / bitmap rows + 47 pool rows + the 52 omissions; E14 pool 33 rows, array 15, stack block | S; the 52 omissions byte-checked by Astra | Sol (generator), Astra (re-check the generated values against the tables, as E4's T1) |
| Width widenings (byte -> word loads / stores / compares, same or +1 length) | E2: 14 accessor thunks, 31 count sites, 0x47c250, 3 vt+0x18 callers, 13 by-name callers, 0x480574, 0x481622 / 0x482b98; E3c: 6 sentinel sites, 5 vt+0x120 callers; E14: 13 +0x1a9 sites, 2 base-table sites | S | Sol, with Astra's RE-1 / 2 / 3 / 5 / 6 first |
| Caves (an instruction grows) | E2 picker loop 0x47b732-0x47b769, by-name result 0x47e389; E3c the three eight-dword clears -> 32-dword clears, the bitset helper clone 0x44d0d0 (5 bounds); E3b second findNext clone; E14 two unrolled clears, snapshot copies 0x489a8c; E7 pool compares that leave imm8, the `push 0x63` thunk, the 0x4bdc12 helper | S | Sol |
| Whole-function replacements (region hash on the owner) | E3 pickers 0x47e690 / 0x47ee90; E15 position packer 0x4656c0 / reader 0x4654f0; E15 `CSmartEntMgr` via getter 0x4b76f0; E3c slot write / read in 0x479a70 / 0x479fc0 (bridges) | V for the prologues and the two helpers; semantics RE-4 / RE-9 | Astra (spec), Sol (code), Opus (review) |
| DLL-side storage replacing static / inline data | E2 global 0x6d83bc; E14 0x72b118 array, 0x72b590 base table, 0x72b5b0 stack block; E3c instance pool backing, slot table at the manager's end; E3b pool backing; E7 registry tables (RE-12) | S | Sol |
| Error instead of silent skip | E3b 0x45f920 -> 0x481938 (logged error); E3c full slot table 0x479f87 (logged); capture lists full (already visible: capture flag bit 0) | S | Sol |
| Converter / library / rules | sections 3, 6, 7; `ceil_zone_records`; demand caps; walkers | V (source read) | Sol (C++), Sonnet (Python reference + vectors) |
| Builder | `[Limits] SaveLayout=w2`, `ItemEnhancements=4096` for port builds; V44 from the descriptor; key lists, `harness.py`, `test_builder_parts.py` | - | Sonnet |
| Agreement | rule 45; refusal text | - | Sol |

### 11.2 Open RE checks (route: GPT Astra high, read-only; each is a small, exact question)

| # | Question | Blocks |
|---|---|---|
| RE-1 | Is `CInventorySystem`+0x2589 free (count byte -> u16)? Writers of +0x2588..+0x258b | E2 kind (b) |
| RE-2 | Do all 13 callers of the by-name lookup 0x47e320 consume the full eax after the `or eax, -1` change (none truncates to al)? | E2 |
| RE-3 | Danger Room reward record layout: is +9 free for a u16 definition index, or is a side table needed? | E2 |
| RE-4 | Exact semantics of the two affix pickers 0x47e690 / 0x47ee90 (filter, weights, RNG calls and order) as a spec for the reimplementation and T6 | E3 |
| RE-5 | `CInventoryEntity`+0x321: padding? every access to +0x320..+0x323 and the constructor | E3c |
| RE-6 | Loaded objective +0x1aa / +0x1ab: free? every access to +0x1a9..+0x1ab | E14 |
| RE-7 | XML2 Fix's own script functions and `objective_text.cpp` for byte-width objective indices | E14 |
| RE-8 | Every instruction that uses the state-record stride 0x18 or addresses +0x12 in 0x465000-0x467000 and the class serializers of SPEC-old 1.13 | E15 |
| RE-9 | `CSmartEntMgr` method set, vtable and owner-region hashes for the getter replacement | E15 |
| RE-10 | Callers of 0x4bdc12 (vtable slot or register call) | E7 |
| RE-11 | Classify the 27 immediates of 0x64 / 0x63 in 0x4bd000-0x4c1000 (three pools, per-hero tables, unrelated) | E7 |
| RE-12 | Talent registry object layout and allocation (the +0x644 / +0x7dc / +0xfb8 / +0x12cc tables) | E7 |
| RE-13 | `CMap` vtable slot of 0x484370 and its callers (R17 static close) | E9 documentation; L3 |
| C1-C3 (census, Sonnet, builder data + the G3 real-save table) | G11 team bonuses + boss immunities -> M; largest XML1 zone's placement count -> loader arrays (3.6); peak instances / attached / world items / objectives / definitions in the real saves | E7 M; E15; acceptance (<= 50 %) |

None of these changes a decision above; each fixes a free byte, a caller list or a spec for a reimplementation. Where
a byte is not free, the fallback is already named (side table / cave).

---

## 12. Implementation order, risks ranked, and routing

| Step | What | Gate | Who | Risk |
|---|---|---|---|---|
| 1 | Descriptors: layout.hpp `w2_layout` (parameterised state size / record count), rules.hpp `w2_profile`, layout identity (3.9), Python reference mirror | G1 static asserts, parity of the three layouts | Sol + Sonnet | low |
| 2 | Converter both directions at W2, walkers, demand caps, representability dry run and bit 1, named refusal table | G1 (vectors, round trips, legacy vectors), G4 fixtures | Sol | low-M (many fields, all specified) |
| 3 | Ledger generator for E15 + E9 (record 0x20, 8 records, R6 counts, `CSmartEntMgr`), position functions, capture bound | G2 T1-T4, T9 | Sol (Astra RE-8, RE-9 first) | M (large, mechanical) |
| 4 | E3c pools and entity slot; E3b pool; E3 records + sentinel + pickers | T7, T8, T6, guard-page proof | Sol (Astra RE-4, RE-5 first) | M-L (99 pool sites, shared helper, picker semantics) |
| 5 | E2 definitions | T5, text gate | Sol (Astra RE-1..3 first), Opus review | **L** (the largest mixed inventory) |
| 6 | E14 objectives | T10 | Sol (Astra RE-6, RE-7 first) | M |
| 7 | E7 pool + M + consumers + builder V44 | T11 | Sol (Astra RE-10..12 first; Sonnet census C1) | M |
| 8 | Rule 45, builder key, refusal texts, test-pipe status (`layout W2; defs n/1024; enh n/4096; att n/4096; inst n/1024; world n/1024; obj n/1024; talents n/256; zones n/8`) | T12, T13 | Sol + Sonnet | low |
| 9 | G3 real saves (collection, offline runs, census table), G4 in game, live runs (cold load, zone change, mission boundary, co-op) | G3, G4 | Sonnet (collection + offline), Sol (live), owner's own test last | - |
| 10 | Second-model review of the whole diff (Astra high or Sol high, read-only), then one build; the coordinator assigns the identity | review | Astra / Sol | - |

Risks, ranked: E2 (mixed inventory; a missed byte consumer gives a wrong item silently - the text gate T4 and the
guard-page proof are the answer) > E3c (99 sites plus a shared bitset helper - the clone must leave the original for
its other callers) > E14 (snapshot and stack relocation touch three copies and two stack frames) > E15 / E9 (large
but generated; the risk is a missed 0x18 stride - RE-8) > E3 pickers (semantic reimplementation - T6 differential) >
E7 (27 immediates to classify) > E3b (ledger complete, sweep done).

---

## 13. Answers to the Astra review's eight CONFIRMED findings

| # | Finding | Answer |
|---|---|---|
| 1 | Native layout underspecified (count byte, 160 pairs, snapshots, rules.hpp walkers) | Sections 3.2-3.7 specify every structure with offsets for all three layouts; 6 names the walkers (`hero_heaps` etc.) as profile-driven |
| 2 | Site inventory omits the 52 instance-pool instructions, the +0x320 entity byte, the payload helpers | 4.4 includes all 52 by category, the entity u16 slot (RE-5) and the 5 vt+0x120 callers; 3.5 keeps the payload helpers' 8-byte bound and gives the slot its own field; the E2 inventory is treated by kind (11.1), not as interchangeable edits |
| 3 | E9 lacks ownership / lifecycle; R17 open | Section 5: records 6-8 are native (no DLL swap), lifecycle traced from the exe (capture, eviction, reset callers), online via the native image; R17's static answer and RE-13 |
| 4 | E7 needs native changes | 4.7: 0x4bdc12 helper (verified bytes), the 0x4c05d9 cap via a thunk, three pools + the 27 immediates (RE-11), consumers, historical imports at 100 |
| 5 | Layout selection by `usable_end`; OFF; agreement | 3.9 (identity, combinations, atomic publication, OFF = stock), 8 (rule 45 covers the whole format) |
| 6 | R6 capacities and the payload arithmetic | Section 2 (sentinel convention), 3.6 (counts), 3.8 (1,279,625 bytes = 61 % of 0x200000; payload not grown) |
| 7 | Representability rules | Section 7: the E10A2 dry run decides bit 1; the hand list is the oracle; re-based talent ids never mark; the generic message on released binaries is accepted |
| 8 | Separate codec / engine / live gates; full-capacity pickers | Section 10: G1-G4 with exact comparisons and the boundary list; 4.2: pickers replaced at full capacity |

## 14. What could not be settled statically, and why the plan is safe anyway

- Whether every narrow holder of a widened index is in the ledgers (SPEC-old U16). The sweeps are pattern-based; the
  **guard-page proof** (old storage unmapped in a test build; any access faults) and the whole-exe text gate are the
  dynamic and static answers; a miss is caught before a save is written, never after.
- The free-byte questions RE-1 / 3 / 5 / 6: each has a named fallback (side table or cave), so no decision depends on
  the answer.
- The picker semantics (RE-4) and `CSmartEntMgr`'s method set (RE-9): whole-function replacements are gated by
  differential tests against the retail code on the union host.
- Whether M = 200 is the final multiplier: the census may move it within 101..220; the code reads the descriptor.
- Whether XML2 clears zone memory anywhere but new game (RE-13): the decision (keep XML2's lifecycle) does not depend
  on it; the live gate L3 observes the mission-boundary behaviour.
- The in-game truths no fixture certifies (bonus application after load, zone transitions, co-op): G3 on the real
  saves of Owen, Shelley and the testers, before the tag.
