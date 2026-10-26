# One-pass limits, wave 2: the save-bound raises (design, revision 3)

Status: design, revision 3, 2026-10-25 (Fable lead; owner-approved brief `task-fable-save-plan.md`). No code. Revision
1 is kept as `limits-wave2.v1.md`. Revision 2 answered the eight CONFIRMED findings of the first GPT Astra review
(`20261025-165428-gpt-astra-high-81ac`; section 13). Revision 3 answers the re-review (`20261025-172352-gpt-astra-high-e377`:
five of the eight closed, the layout arithmetic confirmed, seven new findings, RE-1..13 answered) - section 13.1 - and
adds the implementation-ready ledger (section 15). Revision 3.1 folds Astra report `20261025-180422-gpt-astra-high-0822`
(RE-14 / 15 / 16 answered: ledger rows W2-E15-14a..l, W2-L-17, W2-E14-04a/b, the R17 loader-condition matrix). The
one-page summary is `DECISIONS.md` beside this file. Revision 3.2 folds the seven design corrections the step 1-2
implementer sent back (`agent-work/w2-impl-REPORT.md` "Design corrections"; xml2-fix `w2/impl-1` 6f543e6, 2ad158b),
each checked against that code.

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
| E7 talents | pool 100 x 3; hero ID = (stats index + 1) x 100 + position; shared 0..98 (cap 99) | same | pool **256** (registrations per pool); **multiplier M = 200**, shared 0..198 (cap 199); M is a descriptor constant **101..219** with the rule **position < M** | - | Census C1 (`agent-work/limits-wave2/census-c1-c2.md`): XML1 has **no team bonuses** (0 shared talents), boss immunities are **14** shared talents already inside the shipped 63; largest per-hero file 15 positions; even the pessimistic 102 shared fit M = 200 (51 %). The tight table is the **registry pool**: V44 reports 99 of 100 in five zones -> 256 makes the worst zone 39 %. With p < M the largest id is 298 x M - 1 <= 65535 iff M <= 219 |
| Zone-string table | 2048 slots (mask 0x7ff), arena 0x3800; saved as 0x5808 bytes | same | **8192 slots (mask 0x1fff), arena 0x20000**; saved as 0x28008 bytes | slot 0 reserved (value 1), 0 = free, as today | 8 records x R6 caps can hold more than 2047 distinct names (re-review 5); measured 345 names / 5,467 bytes with 5 records |
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
| `talent_multiplier`, `talent_pool` | 100, 100 | 100, 100 | **200, 256** (rule: talent position < M; shared id < M - 1) |
| `zone_string_slots`, `zone_arena`, `zone_strings_size` | 0x800, 0x3800, 0x5808 | same | **0x2000, 0x20000, 0x28008** |
| `payload`, `usable_end`, `wrapper` | 0x2fc00 / 0x2fc00 / 0x2fc78 | 0x200000 / 0x200000 / 0x200078 | **0x200000 / 0x200000 / 0x200078** (unchanged, section 3.8) |

Every W2 descriptor value is published once (section 3.9) and read by every consumer that today holds a literal:
`lcsave`, `save_format_rules`, `save_core_rules`, **`objective_text` (index offset 0x1a9, table 256, state array
0x72b118, the shared allocation site), `forced_teams_rules` (`side_record_size` 0x26c, `side_count_offset` 0x4dc),
`test_state_rules` (byte index, 75 objectives, 0x72b118)**, SaveTrace, the capture bridges. A module that keeps a
literal is a W2 install failure (the transaction's union guards see the overlap: re-review finding 2).

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

Positions: the packer 0x4656c0 (round + clamp to +/-32767) and the reader 0x4654f0 (`movsx ecx, word [esi+0x12]`
0x46550c, `movsx edx, word [esi+0x14]` 0x465526, V) are replaced for W2 by DLL functions that store and load the
three floats at +0x14 unchanged (bit-exact). The LC `STATE.position` is already 3 x f32.

**Every constructor, copy and clear of a state record initialises or carries the two new fields** (re-review layout
audit): the record clear 0x465ec0 (its per-record loop writes `[esi-0x12]..[esi+4]` relative to `lea esi, [ebp+0x16]`
and advances `add esi, 0x18` at 0x465f17, V: replaced whole, section 4.6), the constructors 0x465e70 / 0x4664a0 /
0x487610, the smart-capture copies 0x46678b..0x46679f, the restore copies into a **24-byte stack record** at
0x46686c..0x466882 (the stack record grows to 0x20; frame references grow by 8); the restore-loop clear
0x466906..0x466932 and cleanup funclet 0x673630 (`8d4dd8` -> `8d4dd0`) grow too. The 0x4b7630 copy is dead under E15-13. The vector strides 0x48759a /
0x48764a / 0x48766a grow to 0x20 (RE-8). A W2 record's slot field is 0xffff and its floats 0.0 after a clear. **Non-owning records
(correction 7, implemented `oldsave.cpp` read / write, `reverse.cpp:445`):** the slot field is meaningful only when
flags A bit 0 is set; the converter writes **0xffff** on every record that owns no item and a non-0xffff value read
there is **not carried** into the LC file; the engine rows (clear, constructors, the slot-write bridge) must keep
writing 0xffff on such records, so the engine and the converter agree byte for byte.

**Disposal adapter (re-review finding 1, V).** The record clear 0x465ec0 calls **0x479c10** (0x465ee7) for every state
record with **flags B bit 0** (+0x11; 0x465edd `f646ff01`). 0x465e70 likewise tests flags B at
0x465e7e (`f6461101`). Then 0x479c10 tests **flags A bit 0** at 0x479c15 (`f6461001`): disposal requires both flags.
The retail adapter reads **one payload byte** through 0x4655d0 (0x479c2b, `push 1`) and passes it to
**0x479ad0**, which zero-extends it (`movzx ecx, byte [esp+0x10]` 0x479aed) and releases that slot through manager
vt+0x120 (0x479af5). Under W2 the payload byte is 0 and the slot is at +0x12, so an unpatched disposal would free slot 0
and leak the real slot. The W2 whole-function adapter at 0x479c10 preserves the payload cursor start/end, reads
the u16 at `record + 0x12`, skips the non-owning 0xffff sentinel, and calls 0x479ad0 with the full slot; its `movzx` becomes a
word load; vt+0x120 and its 5 callers are full-width (section 4.4). The same adapter serves the eviction path
(0x466960 -> 0x465ec0), the reset 0x465f90 and the destructor 0x4875f4, which all go through 0x465ec0.

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
entities (SPEC-old 1.13 "Ordinals") are a separate **runtime** ceiling: an ordinal above 1024 cannot occur until that
loader is widened, so the 4096 map is headroom, not a claim.

**Loader arrays (census C2, `census-c1-c2.md`, decision).** The largest XML1 zone is `arbiter/a_int/arb3_4` with **582**
placements (554 without spawners; 82 of 210 zones exceed 256, none exceeds 600); the largest zone in the whole install is
XML2's stock `act5/egypt/egypt4` with **750**. The loader 0x484ce0 (1,636 instructions, frame 0xf3d8) builds **seven
vector-constructed stack arrays of 0x400 elements** (four of 12 bytes at esp+0x60c0 / +0x90c8 / +0xc0d0 / +0x20b0, three
of 4 bytes at +0x50b8 / +0xa0 / +0x10a8, constructed at 0x485327..0x485420) and **has no bound check**: a zone past 1,024
overruns the stack silently. Decision: **the arrays are not raised in W2.** Why: (1) they are esp-relative arrays inside
one 1,636-instruction frame, so a raise is a whole-frame re-layout (every esp+disp operand of the function regenerated, or
all seven arrays moved to heap storage with every access rewritten) - the technique exists (Phase A's whole-frame CCollide
replacements) but the ledger does not, and it is the largest single function in this pass; (2) nothing about it is
save-bound: the one-pass rule covers limits written into a save, and this one can be raised later without a save break
(the 4096 ordinal map already leaves room); (3) the data needs 56.8 % (XML1) / 73.2 % (XML2 stock, not played in the XML1
mode) of 1,024. What W2 does instead: (a) **builder / validator error** for any shipped zone with more than 1,024
placements (`<inst>` count per `Maps/*.XMLB`, the C2 counting rule), (b) a **runtime preflight** in the DLL at
0x485724, before the nine-argument parser call fills the arrays. It counts every `<inst>` in the recursive XML tree
against `loader_placements` (1024), refuses by zone name, destroys the XML reader and uses the native failure cleanup.
Correction: the numbering hook 0x48624c runs **after** the arrays fill. The parser 0x461d10 already has a count == 1024
check at 0x461d99, but silently truncates the rest; preflight turns that into a named refusal. The bridge depends on
the CMap frame and cleanup, guarded by SHA-256 of [0x484ce0, 0x486353). The later raise to 2,048 (the <= 50 % rule for XML2 stock zones) is a runtime row with its
own frame ledger (**RE-17**, Astra: the complete esp-displacement map of 0x484ce0 relative to the seven arrays).

### 3.7 Manager (`CMap`+0x3b0 allocation; SPEC-old 2.8)

| Offset (W2) | Field | E10A2 | Retail |
|---|---|---|---|
| 0 | **8** records x 0x24388 | 5 x 0x19b88 | 5 x 0x55e0 |
| 0x121c40 | pad | 0x809a8 | 0x1ad60 |
| 0x121c44 | record count | 0x809ac | 0x1ad64 |
| 0x121c48 | mission snapshot (**0x540**) | 0x809b0 (0x26c) | 0x1ad68 |
| 0x122188 | flag | 0x80c1c | 0x1afd4 |
| 0x12218c | newest time stamp | 0x80c20 | 0x1afd8 |
| **0x122190** | manager size (1,188,240 bytes) | 0x80c24 | 0x1afdc |

(E10A2 column corrected in revision 3: layout.hpp asserts `manager_count == 526764` = 0x809ac.)

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
| Tags (the writer has 10: SAVEGAMEBEGIN, CHARACTERSUNLOCKED, CHARACTERSRESTORE, three Danger Room, ZONESTRINGS, ZONES, MISSION STATE, SAVEGAMEEND; SPEC-old 1.3) | 10 x 52 | 520 |
| Item head | 2 + 1024 x 2 + 12 + 24 | 2,086 |
| Item list counts (carried, world) | 2 x 4 | 8 |
| Item instances (bounded by the 1024 pool) | 1024 x (6 + 12 + 4) | 22,528 |
| Attached enhancements (bounded by the 4096 pool) | 4096 x 6 | 24,576 |
| Hero blocks without equipment | 21 x 303 | 6,363 |
| Unlocked list, restore flag, 22 restore records | 600 + 1 + 22 x 213 | 5,287 |
| Game block, Danger Room (75 courses x 26 + 8 grades), Review, profile, script variables, town centres | 220 + 1,958 + 128 + 552 + 2,112 + 4 | 4,974 |
| Three mission snapshots + stack tail | 3 x 0x540 + 8 | 4,040 |
| Zone string table (W2: 8192 slots, 0x20000 arena) | 4 + 8192 x 4 + 4 + 0x20000 | 163,848 |
| Zone records | 4 + 8 x 0x24388 | 1,186,884 |
| **Total** | | **1,421,114 = 0x15afba, 67.8 % of 0x200000** (conservative, 3 spare tags: 1,421,270) |

With the retail-sized string table the ceiling is 1,279,802 bytes (0x13873a, 61.0 %; conservative 1,279,958), the
re-review's corrected figure; revision 2's 1,279,625 had 13 tags, no list counts and a 62-course Danger Room. So the
payload stays 0x200000, the wrapper 0x200078, the 10,486 transfer chunks and FastTransfer unchanged. Revision 1's
"0x280000" was an arithmetic error (4096 x 0x20 x 5 = 0xa0000); the payload is **not** grown. The manager backing
(1.19 MB, DLL memory) and the stream wrapper (2 MiB, allocated by the game at the four sites of SPEC-old 2.10) are
different allocations; only the former changes size. The translator's preflight refuses anything larger than the
payload before the engine sees it (SPEC-new 4.5 row 4), so the bound is a budget, not a trust.

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
| lc | absent / 0 | absent | RETAIL (the configured baseline) | rule 1 rev 1 |
| lc | 1 | absent | E10A2 (the configured baseline: `EntityCapacity` alone still selects E10A2, exactly as 0.4.x does) | rule 1 rev 3 |
| lc | 1 | **w2** | **W2** | rule 1 rev 3 (or rev 4 with E4 stage 2) + **rule 47 NativeSaveLayout** (registered only for W2) |
| lc | absent / 0 | w2 | refused: "SaveLayout=w2 needs EntityCapacity=1" (W2 tokens are contiguous) | unverified, online closed |

One key selects the whole layout and **every** engine raise of section 4; the raises are not individually selectable,
because the stream is one contract (the owner's one-pass rule, in code). `[Limits] ItemEnhancements` under W2 must be
absent or 4096 (W2 sets it; another value is refused with a log line).

**Atomic publication.** The W2 install is one contribution to the entity-capacity startup transaction
(`patch_transaction`): every site guarded on retail bytes, every replaced function on a region hash, everything
prepared before the first write; on commit the publication callbacks run together: (1) the lcsave layout pointer and
`lc_known_required_features`, (2) the converter's demand caps (`NativeLoadDemand` compared against the descriptor, not
the literals 256 / 375 / 150), (3) the save menu's feature-name table (section 7), (4) rule 47 registration. A failed
guard or write rolls the bytes back to retail, publishes nothing, registers rule 47 unverified (online closed) and the
LC seam is not installed (the game refuses to start with the LC notice, as a Phase A failure does today). **No
`SaveLayout` key = the configured baseline** (RETAIL or E10A2, byte-identical to 0.4.x); **no `[Save] Format` = stock
bytes**; both checked by the whole-exe text gate (section 10, T2). Modules that already hook W2-affected sites
(`objective_text`, `forced_teams`, `test_state`, SaveTrace, the save-core bridges) contribute to the same transaction
with descriptor-derived expectations, so an overlap is a union-guard disagreement, never two patches on one site.
**When (correction 6):** `objective_text::contribute()` exists now (2ad158b) and its standalone `install()` commits the
same four writes through `patch_transaction`; it is **wired into the startup transaction by the W2 install at step 6**
(when the allocation-site overlap exists), not before - joining it earlier would tie the module to every other
startup contributor and change the configured-baseline behaviour.

---

## 4. Per-row engine changes

Each row names its ledger (SPEC-old section, instruction count), the kinds of edit, and what is **RE** (open). The
site tables themselves are SPEC-old's; this page does not repeat 900 rows.

### 4.1 E2 item definitions 160 -> 1024 (SPEC-old 2.2: 164 instructions, 10 assumption groups; S)

Kinds: (a) **relocate** the inline definition array (manager+4, 160 x 0x3c) and the state pairs (manager+0x785c) to the
manager's end, as `item_layout_for` already moves the enhancement pool: allocation 0x480a25 grows, the 18 `imul`
strides and 6 loops keep 0x3c, constructor / destructor iterators (0x480725-0x48073a, 0x48098f-0x4809a2, 0x673aa4) run
the same constructors on the new storage; the cap 0x480430 (`cmp [esp+0x1c], 0xa0`, imm32, V) -> 1024 in place;
(b) **widen** the count byte +0x2588 (31 sites) to a **u16 in place**: RE-1 (Astra, LIKELY) found accesses to +0x2588
only and none to +0x2589..+0x258b (the next field is the XML handle at +0x258c); the count stores 0x4802b8, 0x480576,
0x4805ed write both bytes, the byte reads become word reads (same length or one byte longer -> caves where longer);
(c) **widen the index end to end**: 14 accessor thunks compare `al` -> `ax`; 0x47c250's result; the 3 external callers
of vt+0x18; the picker 0x47b590's byte loop counter and byte result (`inc al` 0x47b74e; cave: the loop becomes a DLL
function); the by-name lookup 0x47e320: its failure return `or al, 0xff` 0x47e389 -> `or eax, -1` **and its success
return `mov al, bl` 0x47e393 -> `movzx eax, bx`** (V; RE-2), and its 13 callers: **10 pass the full EAX on unchanged
(the `call [edx+0x78]` sites), 3 truncate and are patched: 0x49ed9d `mov [esp+8], al`, 0x49ee11 `mov [esp+4], al`
(both V: byte stack slots -> word/dword stores, the receiving code reads them accordingly) and 0x4d47da** (next); the
loader's `inc cl` 0x480574; the **Danger Room reward**: records have stride 0x14, definition byte at +8, **+9 unused,
u16 at +0xa** (`mov word [edi+0xa], ax` 0x4d47b7, V; RE-3 LIKELY) -> the definition becomes a **u16 at +8 in place**:
store 0x4d47da `mov [edi+8], al` -> `mov [edi+8], ax`, readers 0x4c8fe1 / 0x4ca138 -> word loads, **sentinel compares
0x4c8fd2 / 0x4ca129 `cmp byte [esi+8], 0xff` -> `cmp word [esi+8], 0xffff`** (V), and the reward constructor initialises
+8..+9 to 0xffff; the global 0x6d83bc (9 sites) -> a DLL u32 with the 9 sites re-aimed; (d) **stream**: `OldItemHead` / `OldInstanceHead` per section 3 (0x47c52c / 0x47f81f write
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
the whole table (PLAN E3 acceptance: 1,000 drops cover every affix id). **Picker specification (RE-4, Astra,
CONFIRMED; the reimplementation and T6 follow it exactly):** both are `__thiscall` with three stack arguments, `ret 0xc`.
0x47ee90 scans records `[0, count_605c)`, 0x47e690 scans `[count_605c, count_605c + count_6060)`, in ascending record
order. A record is a candidate when its level is <= requested + 5; a level <= requested - 15 additionally requires the
record's vt+0x2c flag; a non-zero key from vt+0xc excludes the record when an enhancement already in the supplied chain
has that key. Each accepted record appears once (no weights). No candidates -> return -1 **without** touching the RNG;
otherwise exactly one call `0x5601e0(0, count - 1, true)` (also for a single candidate) and the candidate at that index
is returned. Record numbers are append-only (saves store them), so old saves keep meaning. Risk **M**.

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
0x479d27, 0x479da9 `mov byte [ebp+0x320], 0xff` V, 0x479f59, 0x47a117, 0x47aa1c) -> **u16 at +0x320, 0xffff**: RE-5
(Astra, LIKELY) - the constructor initialises the slot at 0x47aa1c and the quantity at +0x322 at 0x47aa23, and no access
to +0x321 as an owner field was found; **dword accesses to +0x320 elsewhere belong to other owner classes and are not
widened** (the ledger is owner-specific: `CInventoryEntity` methods only); the 5 callers of manager vt+0x120 (0x47d390,
instance by slot) pass a zero-extended byte -> word; the state save / restore 0x479a70 / 0x479fc0 write and read the
new u16 field at record +0x12 (section 3.5) under W2 (bridges at the slot write and the slot read; the quantity bytes
unchanged); **the disposal adapter of section 3.5 (0x479c10 / 0x479ad0)**. A full slot table today sets 0xff and takes
0x479f87 (SPEC-old U7, "what the player sees" untraced) - with 1024 slots and the preflight counting world items
against free capacity this path is not expected to run; it is logged if it does. Risk **M-L** (99 pool sites, a
shared helper, the disposal chain).

### 4.5 E14 objectives 299 -> 1024, 75 -> 127 per act, 25 -> 64 files (SPEC-old 2.6: 105 instructions; S)

Registrar cap 0x4885c0 `cmp [esp+0x1c], 0x12b; jae` (imm32, V; retail registers indices 0..298) -> 0x400 in place
(indices 0..1023 registrable); per-act cap 0x4899e0 `cmp [edi+0x7d78], 0x4b` (imm8, V) -> 0x7f in place;
the loaded-objective pool (manager+8, 75 x 0x1ac, bitmap +0x7d64, count +0x7d70) -> 127 x 0x1ac relocated (17 stride
sites, 16 bitmap / count sites, 2 unrolled clears -> caves); **the global index of a loaded objective moves to a DLL
side table** (decision; RE-6 CONFIRMED that **+0x1aa is occupied** by the target count / flag bits - constructor
0x4886e5..0x4886f0, parser 0x488970..0x4889ca, the copy routine 0x488430 and the journal code access it - so a u16 at
+0x1a9 would overwrite live data; +0x1ab had no direct access but a split field is not taken). Side-table contract:
`u16 wide_index[127]`, indexed by **pool slot** = (record - (manager + 8)) / 0x1ac; the byte at +0x1a9 keeps the low
8 bits (so every unpatched reader of the byte still sees a consistent value for indices < 256 and the test pipe can
cross-check); the 13 readers of +0x1a9 become `wide_index[slot]` through caves; the registrar's store 0x489a01 writes
both; **copy semantics (RE-16, CONFIRMED)**: every move of a loaded-objective record happens inside the **heapsort 0x489680**
(called at 0x489a60): three helper calls 0x488430 into two stack temporaries and six inline `rep movsd` copies of 0x1ac
bytes back and forth between pool records and those temporaries (ledger row W2-E14-04b lists all nine). Hooking the
helper alone never sees the copies back into the pool, so the sort is **replaced whole** by a DLL heapsort that yields
the identical permutation and moves the wide indices with the records; the fallback is nine bridges with one shadow
value consumed only on temporary-to-pool copies and scoped to the invocation; **reuse**: the pool allocation sets
`wide_index[slot] = 0xffff` and the release / bitmap clear does the same (the two unrolled clears and the 16 bitmap
sites are the hooks), so a stale index can never be read from a reused slot; **sorting**: if records are sorted by
swapping in place the two copies above cover it; `objective_text`'s own 256-entry table is indexed by the same wide
index and grows to 1024 (next paragraph). The **per-file base index table 0x72b590** (u8 x 25; stored 0x489252, read
`movzx edx, byte [ebx+0x72b590]` 0x489bdf) -> a DLL u16 x 64 table, both sites re-aimed; the file index
(`CMissionMgr`+4, u8, 0xff none) unchanged (64 fits); the global state array 0x72b118 (300 bytes; 15 sites), its
300-byte clears (4) and the two snapshot copies (0x4b dwords + 1 byte -> 0x100 dwords) -> a DLL 1024-byte array;
**the snapshot 0x26c -> 0x540 everywhere it is laid out**: constructor 0x4874b0 (`push 0x12c` 0x487505 -> 0x400), the
side-mission stack static block 0x72b5b0 (frames at +0, +0x26c; pad +0x4d8; count +0x4dc; push 0x5f3630) -> a DLL
block with frames at +0, +0x540, pad +0xa80, count +0xa84, and the manager's snapshot at its W2 offset (section 3.7);
capture 0x48a170 (+ 0x489a80), restore 0x489300 (+ 0x4892e0) re-based.
**Existing DLL consumers join the descriptor (RE-7, CONFIRMED):** `objective_text.cpp` (rules: `state_bytes =
0x72b118`, `index_offset = 0x1a9`, a 256-entry text table, `reset_slot(uint8_t)`, and it patches the **same allocation
site** the W2 pool relocation needs) reads the wide index from the side table, sizes its table from the descriptor
(1024) and contributes its allocation-site expectation to the transaction; `forced_teams_rules.hpp` (`side_record_size
0x26c`, `side_count_offset 0x4dc`, `side_names_offset 0xa4`) takes the snapshot size and count offset from the
descriptor (otherwise under W2 it reads objective bytes as the stack count and writes party names into the wrong
frame); `test_state_rules.hpp` (byte index at +0x1a9, "<= 75", `0x72b118`) likewise. No separate objective-index
holder exists in the fix's own script functions (RE-7). Risk **M**.

### 4.6 E15 zone record caps and float positions (SPEC-old 2.7: 200 instructions; S) and E9 (2.8: 48; S)

Record and manager offsets: regenerated from the W2 layout exactly as Phase A regenerates them from E10A2 (the 190
literal operand edits of `entity_capacity_sites.hpp` "Native saves" are the same rows with new values; the generator
takes `state_record_size`, the four counts and `record_count`). New in W2 against E10A2: (a) the state record stride
0x18 -> 0x20 at every site that walks state records. **The state-layout site list (RE-8, Astra, LIKELY complete for
the owner regions):** scaled `3 x 8` addressing at 0x465c2f/0x465c32, 0x465c87/0x465c8a, 0x4666ab/0x4666ae,
0x466736/0x466739, 0x466838/0x46683b/0x46683f; stride advances at 0x465f17 (`add esi, 0x18`, V), 0x4660ee, 0x4662fd,
0x466415; clears / constructors 0x465e70, 0x465ec0 (replaced whole: its negative-displacement field writes
`[esi-0x12]..[esi+4]`, the position words at esi..esi+4, and `mov ecx, 0x100` 0x465f32 -> 4096 ordinal-map clear, V),
0x4664a0, 0x487610; vector strides 0x48759a, 0x48764a, 0x48766a; smart-capture copies 0x46678b..0x46679f; restore
copies into a 24-byte stack record 0x46686c..0x466882 (**the stack frame grows to 0x20**); the smart-manager copy
0x4b7630; the two position functions 0x4656c0 / 0x4654f0 (replaced). The class serializers use the shared payload
helpers and need no change except `CInventoryEntity` (section 4.4). The generator sweeps for these roles (scaled
addressing, stride add, negative displacement from a record-end cursor), not for the literal 0x18 alone; (b) dynamic /
smart 1024 -> 2048 and ordinals 1024 -> 4096 (bounds 0x46667a, 0x46668a, 0x4666f3; the capture bridges
`save_core_rules::capture_bridges` take the profile); (c) **`CSmartEntMgr` replaced through its getter 0x4b76f0** by a
DLL object: RE-9 (Astra, CONFIRMED) - a static object with an **eight-slot vtable 0x68e440** (slots +0..+0x1c ->
0x4b76d0 deleting destructor, 0x4b7100 update, 0x4b7000 allocate, 0x4b70e0 reset, 0x4b6fc0 clear records, 0x4b6fb0
count, 0x4b6ff0 indexed access, 0x4b75c0 append-copy); the replacement implements **the whole method family** on the
new layout (returning a larger backing alone is insufficient): 2048 x 0x20 records at +0, **bitmap at +0x10008, count
at +0x10108, object size 0x1010c**; owner-region hash **SHA-256 of [0x4b6e70, 0x4b7744) =
`1969cfd96a7b2701a2f30d915956192d795e1a43fd1f6248c5d0d52b27d04a4b`** guards the install (as Phase A guards the World
and Grid owners); the 45 rows of SPEC-old 2.7 (R2 block) are its ledger; (d) **zone-string table 8192 slots / 0x20000
arena** (section 2): the 11 slot sites (hash mask 0x7ff at 0x41a3fb, 0x465816, 0x46582d, 0x4658d1, 0x4658df; `mov ecx,
0x800` clears at 0x46568d, 0x465fed, 0x4661d8, 0x466394, 0x6021b9; `cmp esi, 0x800` 0x466258), the arena bound
`cmp ecx, 0x3800` 0x41a427, the garbage collector 0x466020 and the stream copy of the 0x5808 block in 0x484a60 /
0x484b70 - **RE-14 (Astra, CONFIRMED sites, LIKELY complete)**: six probe masks (0x419d70 added), 11 arena-used and 20
arena / reserved-byte displacements (0x2004 / 0x2008 / 0x2009 -> 0x8004 / 0x8008 / 0x8009), 24 base-relative accesses
that keep their bytes, the stream sizes 0x484ac1 / 0x484ba6, the static getter / initialiser [0x6021a0, 0x6021f4)
replaced, **the GC frame 0x466020 replaced** (0x110 -> 0x410 frame, 64 -> 256-dword bitmap, scan bound 0x40 -> 0x100,
stack argument +0x124 -> +0x424), bounded probing with a named refusal (0x419d40 has no cycle stop and 0x41a41a
publishes the offset before the space check), the existing `capture_compare` arena cave regenerated, and the
`OldZoneStrings` consumers (`internal.hpp:112`, `oldsave.cpp`, `reverse.cpp`, `save_format.cpp:984` `record_zone`)
on the descriptor - ledger rows W2-E15-14a..l and W2-L-17; 8,191 non-empty names stays an explicit supported limit;
(e) **E9: `record_count` 8** (section 3.7: 3 imm8 edits + regenerated offsets +
the allocation size). The capture's world-handle scan `cmp ebx, 0x100` 0x4666bd (V) -> 1024 (the World is already
1024 handles; SPEC-new R6 "capture bound taken from the world table's size"). Risk **M** (mechanical, large).

### 4.7 E7 talents: pool 100 -> 256, multiplier 100 -> M = 200 (answers Astra 4)

Verified (V): the hero ID helper is the **function 0x4bdc00** (`call 0x44b8f0; ... call [edx+0x3c]; inc eax; imul eax,
eax, 0x64; ret 4`), **vtable 0x68ec24 slot +0x30**, called at **0x4c061e** (`call [eax+0x30]`); 0x4bdc12 is its six-byte
tail `imul; ret 4` (RE-10, CONFIRMED). Shared loading pushes the cap **0x63 at 0x4c05d9** (`push 0x63`, imm8) into
`call 0x4c0460` at 0x4c05de; **0x4c0644 `lea ecx, [eax + 0x64]`** (V) is the per-owner **exclusive ID endpoint** (base +
M), outside the immediate sweep; the three registration pools compare `cmp ecx, 0x64` at **0x4bdfe4 / 0x4be7e4 /
0x4be044** (V). A sweep of 0x4bd000-0x4c1000 (V) finds **27 immediates of 0x64 / 0x63** and **no divide-by-100 magic
(0x51eb851f)**: the engine does not decode an owner from a talent ID. **Classification (RE-11, Astra, CONFIRMED):**
pool A `0x4bdfe4, 0x4beabe, 0x4bead5, 0x4beae5, 0x4befa6`; pool B and its outer consumers `0x4be58f, 0x4be5ec,
0x4be62d, 0x4be650, 0x4be676, 0x4be6b8, 0x4be7e4, 0x4beb03, 0x4beede, 0x4beef5, 0x4bef05, 0x4bef60, 0x4bef7b,
0x4bf319, 0x4bf358, 0x4bfaf5, 0x4bfed1`; pool C `0x4be044, 0x4be9ee, 0x4bea05, 0x4bea15`; the common three-pool
fullness value `0x4c0115`; separately 0x4bdc12 (M), 0x4c05d9 (shared cap), 0x4c07c5 (unrelated stack allocation) and
0x4c0644 (the endpoint). **Registry (RE-12, CONFIRMED):** allocated **0x3228 bytes at 0x4c0584**, constructor
**0x4c0370** (vtable 0x68ec24), singleton pointer **0x77339c**; three embedded pools plus trailing index tables: pool A
at +0x370 (100 x 0x2c records; ring +0x1134, head +0x12cc, bitmap +0x12d4, count +0x12e4), pool B at +0x1724 (100 x
0x24; ring +0xe20, head +0xfb8, bitmap +0xfc0, count +0xfd0, generation table +0xfd4, mask / shift +0x1164 / +0x1168),
pool C at +0x289c (100 x 0x10; ring +0x644, head +0x7dc, bitmap +0x7e4, count +0x7f4), index tables at +0x1658 and
+0x3094 (offsets are pool-relative, not one table). Design:

- **Pool 256, whole-registry relocation**: the registry object moves to a DLL backing laid out from the descriptor
  (three pools of 256 records with their rings, heads, bitmaps, counts, B's generation table and token mask / shift
  (256 entries: mask 0xff, shift 8), the index tables), every pool-relative displacement regenerated (the 27 + the
  constructor's and resets' field writes), capacity compares raised (imm8 -> imm32 lengthens by 3 bytes -> caves);
  **the generation loop 0x4bfa20..0x4bfaf8 updates five entries per iteration (`add edx, 5` 0x4bfaec, bound
  `cmp eax, 0x64` 0x4bfaf5, V) and is replaced whole by a capacity loop** (256 is not a multiple of 5: the retail
  loop at bound 256 would touch indices 255..259); **every four-dword bitmap clear becomes an eight-dword clear**
  (pool C's constructor clear at 0x4c042c..0x4c0434 V; pools A / B inside 0x4befa0 / 0x4bf2c0 and the resets).
  The pool capacity bounds **registrations**, not positions (next item).
- **Multiplier M = 200**: the six-byte tail at 0x4bdc12 takes a 5-byte `jmp` (plus one pad byte) to a DLL function
  `imul eax, eax, M; ret 4`, guarded on the whole of 0x4bdc00..0x4bdc18; the endpoint `lea ecx, [eax + 0x64]` at
  0x4c0644 (3 bytes) becomes a cave (`lea ecx, [eax + M]` needs disp32); the `push 0x63` is reached by re-aiming
  `call 0x4c0460` (0x4c05de) at a thunk that, at entry, writes `M - 1` into the third argument at **[esp+0xc]**,
  preserves ECX and tail-jumps to 0x4c0460 (the only way to change an imm8 push to 199 in place).
  **ID-space rule (re-review 7):** a talent's position `p` satisfies **`p < M`** (so `(k+1) x M + p` never reaches the
  next owner's base, and the shared range is `0 .. M-2` with cap `M-1`), and for every stats slot `k <= 296` the id
  `(k+1) x M + (M-1) = 298 x M - 1 <= 65535` keeps the saved u16 `OldTalentEntry.id`, which gives **M <= 219**. M is a
  descriptor constant in **101..219**; the builder validator refuses a talent file with >= M positions and a shared
  table with >= M-1 entries. **Census C1 (done, `census-c1-c2.md`)**: team bonuses 0, immunities 14, shared total 63,
  largest per-hero file 15 positions, stats table 246 entries; M = 200 holds every planned and pessimistic case
  (<= 102 shared = 51 %), so **M = 200 is final** unless a mod needs more. The registry pool is the binding table today
  (99 of 100 names in five zones, 92 in 694 of 757 zones): the 256 raise brings the worst zone to 39 %.
- **Consumers (correction 3, implemented)**: `tables.cpp:557` (`engine_id = (group + 1) * 100 + position`),
  **`tables.cpp:733` (`herostat_talents`)**, `api.cpp:207` (`shared = tid < 100`), `reverse.cpp:190` (`e.id / 100`
  owner check) and the **Python `PositionTables._tal` / preflight owner check** held M = 100 ids; the tables now keep
  **(owner + 1, position) codes** and the engine id is computed per layout (`talent_engine_id`, `oldsave.cpp:907`:
  `(owner + 1) x M + position`), which refuses a position >= M or a shared position >= M - 1 **by name** instead of
  colliding, and refuses an id above 0xffff; **historical imports keep 100**: the legacy
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
   (flags B bit 0 gates the clear call, then flags A bit 0 gates disposal; SPEC-old 1.13, 0x479c10). A sixth record held outside the manager would have to carry its item
   instances outside the item manager too and re-insert them on swap-in: a second item lifecycle. Natively at 8, the
   engine's own lifecycle handles dependent items, names (the zone string table, 0x800 slots) and provenance.
2. `retain_sections()` (`convert.cpp:1318`) carries only `LGCY` and unknown KEEP sections; it never merged extra `ZONE`
   records and does not need to: the `ZONE` section is written from the native stream's record list (`old.zones`,
   `oldsave.cpp:561`, whose ceiling `ceil_zone_records` becomes `native.record_count`).
3. Online: the host's 2 MiB image carries all 8 records; the joiner's `native_zone_metadata` (save_format.cpp:1182)
   infers metadata for every record in the image with the W2 layout. No sidecar, no second transport.
4. The restore timing is the engine's (zone load 0x484ce0 restores the record found by name 0x465d80).

Lifecycle (V from the exe, RE-13 CONFIRMED by Astra): `CMap`'s **unload method vt+0x30 = 0x4844a0** (called by the
zone loader at **0x484e48** `call [eax+0x30]` with a second argument of 0, and virtually elsewhere - no direct callers)
takes a boolean: when it is set (`test bl, bl` 0x48461e) it calls **vt+0x34 = 0x484370 at 0x484627**, which clears
`CSmartEntMgr` (vt+0xc via 0x4b76f0) and **resets every zone record** through 0x465f90 (0x465ec0 per record, count 0,
flag 0, string table cleared) when the manager flag +0x1afd4 is clear; otherwise it **captures** the zone at 0x48462c
(0x484900 -> 0x466960; skipped when the current zone name at CMap+0x20 is empty, the flag +0x1afd4 is set, 0x4c8770
returns true (untraced) or the zone's vt+0xa4 "nosave" is set). Records are also captured when a save is written
(0x484a60 -> 0x466960) and **evicted** (LRU by time stamp) when a zone without a record is captured at full count
(0x46699c). The constructor 0x487440 (0x487492) is 0x465f90's only other caller. The port hooks the campaign reset at
0x5f2e70 (`campaign_reset_hook`) and clears its own capture metadata there.

**R17 (does the port clear zone memory at mission boundaries like XML1's "DumpZoneMission"?)** The reset is a
**conditional part of zone unloading**, selected by the unload method's boolean. **RE-15 (Astra, CONFIRMED call values,
LIKELY exhaustive callers):** 0x4844a0 has no direct caller; its only literal reference is the vtable entry 0x6887bc
(V). The immediate callers of vt+0x30 and the value they pass:

| Caller | Reset selector (first argument) | Second argument |
|---|---|---|
| 0x4698cf in the CGame forwarding method 0x4698c0 (CGame vt+0x148) | forwards its own first argument | forwards its second |
| 0x484e48 in the zone loader 0x484ce0 | **`reset = !loader_argument && ((CMap.flags_220 & 1) != 0 \|\| CMap.string_120[0] == 0)`** (initialisation 0x484dce; argument test 0x484e00 `mov al, [esp+0xf3f8]` V; flag / string tests 0x484e0b `test byte [ebp+0x220], 1` V .. 0x484e26; set-true 0x484e28 V) | 0 |
| 0x4876a9 in the CMap teardown 0x4876a0 (vt+0x08) | 1 | 1 |

The CGame forwarding method's eight virtual callers: 0x469959 (CGame vt+0x140 teardown) `(0,1)` capture, then the map
teardown resets; 0x46e2bc (the native save loader) `(1,1)` reset; 0x5b5e45 (branch before `mainmenuexit`) `(1,1)` reset;
0x5c9e79 `(0,1)` capture; 0x5e1eeb `(0,1)` capture; 0x5f2eaf (conditional branch in `resetgame`) `(1,1)` reset;
0x5f3c2e `(1,0)` reset; 0x613eae (network soft reset) `(1,0)` reset. "Reset selected" stays conditional inside
0x484370 (the manager reset runs only when the flag +0x1afd4 is clear, 0x48438f..0x4843b2), and capture keeps its
empty-name / pending-restore / mode / `nosave` exclusions.

The script connection: `loadMapKeepTeam` emits `loadmap %s 0 0` (string 0x68d30c, queue call 0x4a0d03) and
`loadMapChooseTeam` `loadmap %s 0 1` (0x68d31c); the console `loadmap` computes **side-mission stack count > 0** at
0x5f47dd..0x5f47eb (`call [edx+0x44]` V) and passes it at 0x5f48c9 through CGame 0x4698f0 to CMap 0x4840b0, whose
load-map method sets flags bits 0 and 1 (0x484101 `or byte [esi+0x220], 3` V) and forwards that boolean to the loader
at 0x484144; the zone-transition method 0x484010 clears bit 0 and sets bit 1 (0x484064 `and dl, 0xfe` V ..0x48406a);
`restorelastzone` / the ForcedTeams return pass the non-empty-stack boolean, which suppresses the loader reset. **So an
ordinary `loadmap` with an empty side-mission stack selects reset; a stacked return selects capture; a normal zone
transition preserves the cache while `string_120` is non-empty.** The converted scripts use all three paths
(`begin_mansion2.py:59/63` KeepTeam / ChooseTeam, `mansion/man2/from_backyard.py:3` `loadZone`,
`x1/missions/end_jug_fb.py:13` `popParty`), so zone memory is **not** universally retained across missions. **NOT
FOUND:** a proof that every converted boundary matches XML1's `DumpZoneMission` exactly (a port-side mission-script
coverage read remains; it is a behaviour question, not a W2 layout question). Revision 2's claim that records "survive
mission boundaries" is **withdrawn**. Decision: **W2 preserves XML2's actual conditional lifecycle unchanged** (the
raise changes capacity, never when a reset or capture happens); the matrix above is the contract the live gate L3
checks (an empty-stack `loadmap` forgets, a stacked return and a plain transition remember).

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
- **Profile file (correction 4)**: the profile directions validate the build's table budgets (item definitions 160), so
  a 1024-definition build would have `profile.lcsave` refused; `old_profile_to_new` / `new_to_old_profile` get layout
  overloads (`lcsave.hpp:314-315`) and `save_format` passes the installed layout (implemented).
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

**Rule:** the W2 writer sets bit 1 exactly when the file it is about to write **cannot be applied by a pre-W2 build**.
It decides that with a **target-aware representability probe** `representable(file, target)` for target = E10A2 (the
pre-W2 release layout), run in memory after the LC file is built, no engine (re-review 4: a bare reverse dry run is not
enough, because `reverse.cpp:241` accepts objective indices through 299 while the old engine truncates at 255, the
enhancement check proves u16 width but not the old pool, and the W2 talent mapping would misjudge an M = 100 build).
The probe:

1. runs the **target's** reverse translation (`new_to_old_stream` with `e10a2_layout`) **in probe mode** (correction 1,
   `reverse.cpp:124-131, 680`): the target's pools, handles, positions, record count, ordinals, lists, snapshot
   objectives and talent multiplier (M = 100: owner check, shared `< 99`), but **definition indices bounded by this
   build's table** (a literal target pass would refuse every index > 159 and contradict the oracle), the model **not
   written** at the target's widths, and a bit 1 the file already carries **ignored** (the probe judges content); a
   refusal or any non-zero `dropped` counter marks the file;
2. then applies the **engine constraints the old translation does not check**: every objective index resolved on this
   build must be **<= 255** (the old +0x1a9 byte and the 0x72b590 base table truncate above it; the old reverse accepts
   up to 299) - **per registered objective** (correction 2): the forward writes every objective the build registers
   into every snapshot, so **a build that registers more than 256 objectives marks every save it writes: its saves are
   always W2-only** (and > 299 is refused by the target translation anyway); this is the conservative reading and is
   implemented that way, every enhancement record number `< 512` (the pre-W2 release default of `[Limits] ItemEnhancements`; the
   target's actual INI is unknowable, so the release default is the contract), attached elements `<= 375`, instances
   `<= 256`, world items `<= 150` and handles `< 150`, zone records `<= 5`, every talent position `< 100` and every
   shared id `< 99` **on the target's mapping**, positions integral in -32767..32767;
3. and the **feature gates**: the target knows bit 0 only; an optional feature the target lacks is droppable by
   definition and does not mark.

A marked file logs the first failing rule by name. The hand list below is the **test oracle** (section 10, boundary
tests) for the probe, not the implementation:

| Sets bit 1 | Does not set bit 1 |
|---|---|
| a state position that is not an integer in -32767..32767 (0.5 is not representable: `reverse.cpp:100`) | a re-based talent ID (the LC file stores catalogue IDs; the native id is recomputed per build) |
| more than 5 zone records | 8 records of which 3 are empty / never captured (the writer writes only live records) |
| more than 256 instances, 375 attached elements or 150 world items in the stream | an item whose definition index is > 159 on this build but whose catalogue id resolves on the older build too (the index is per build; the older build's own demand check decides) |
| a world-item handle >= 150 | retained KEEP / LGCY sections (opaque, carried; layout-independent) |
| any registered objective with index > 255 on this build (every save of a build registering more than 256 objectives), an enhancement record >= 512, a talent position >= 100 or a shared talent id >= 99 | the level catch-up / class-growth optional bits (optional features) |

The probe costs one extra reverse pass per write (milliseconds; the writer already re-reads its own output). Its
rules are a table in `lcsave` keyed by target layout, so a later layout adds a column, not code.

**Named refusal.** From W2 on, a build that meets an unknown required bit keeps the generic text; a build that knows
the bit's **name but not the feature** cannot exist (knowing the bit means implementing it). What W2 adds is the table
`{bit -> "needs Legends Classic 0.4.2 (XML2 Fix 1.9) or newer"}` for its *own* future: the save menu and the log name
the minimum version for bits this build knows about but refuses for another reason (a W2 build with `SaveLayout`
absent, loading a bit-1 file: "this save needs the W2 save layout ([Limits] SaveLayout=w2); enable it or use the build
that wrote it"). Released 1.8.x binaries keep their generic message; that is accepted (Astra 7, last point).

**Bit-1 edge cases (correction 5, implemented):** a bit-1 file on a **RETAIL** layout shows the entity-capacity
downgrade text first (bit 0 is always set with bit 1, and capacity is the prerequisite); **bit 1 without bit 0 is a
damaged marker** (`container.cpp:608`: reader and decision refuse, the writer never emits it); the slot list shows
`(needs the W2 save layout) <description>` (`rules.hpp:954`); the log / reason `w2_refused` names `[Limits]
SaveLayout=w2` and Legends Classic 0.4.2 / XML2 Fix 1.9.

---

## 8. Online (answers Astra 5, the agreement part)

| Change | Online effect | Covered by |
|---|---|---|
| W2 layout (all structures of section 3) | the host's 2 MiB native image and every joiner's reader must agree on the stream layout; equal actor capacities do not imply equal streams | **rule 47 `NativeSaveLayout`** (coordinator-assigned ID 47), revision 1, values `{layout ordinal (0 RETAIL, 1 E10A2, 2 W2) \| record_count << 8 \| structural revision << 16, definitions \| enhancement_records << 16, instances \| world_slots << 16, objectives \| talent_multiplier << 16}`; registered **only when W2 is installed** (as rules 28 and 31 do: absent key, RETAIL or E10A2 add no entry, so the released record of every 0.4.x build is unchanged and a 0.4.1 peer is refused as "a rule this PC does not have" on its side and as `47 NativeSaveLayout` on the W2 side); a requested W2 that did not install registers unverified and closes host / join. The structural revision increments whenever equal capacities could still encode the stream differently |
| E2 / E3 / E3b / E3c pools | a full pool refuses an item, a bonus or a drop (lockstep state) | rule 47 values 1-2; `ItemEnhancements=4096` also registers rule 34 as today |
| E7 pool and M | registration refusals and the talent-id space are lockstep state | rule 47 value 3 (M); the pool is implied by the layout ordinal |
| E9 8 records | eviction order and what a revisited zone remembers are lockstep state | rule 47 value 0 (record_count) |
| E14 / E15 | capture completeness and positions are lockstep state | layout ordinal |
| Campaign transfer (kind 3, 'G' / 'H', FastTransfer) | unchanged: same 2 MiB image, same chunks; the joiner's `native_zone_metadata` reads 8 records with the W2 layout | admission guarantees equal layouts; `xml2_campaign_sender_test` / `xml2_reliable_receive_test` with W2 images |
| Join in progress / fresh tail | unchanged (protocol 4) | - |

Rule 1 (entity layout) is **not** reused for the stream layout (Astra 5: equal actor capacities can admit incompatible
streams); rule 47 covers the whole native format.

---

## 9. Composition with E4 (one transaction, one agreement)

- Both installs are contributions to the **same** startup `patch_transaction`; the transaction is all-or-nothing, so a
  requested W2 and a requested E4 stage either both verify or nothing is written (fail-closed as a unit; each
  registers its rule unverified on failure).
- W2 touches no actor structure; E4 touches no save structure (its design section 7: nothing actor-indexed is saved).
  Independent keys: `[Limits] SaveLayout=w2` (W2), `ActorPools=124` / `Actors=96` (E4). Supported together or alone,
  each requiring `EntityCapacity=1`.
- Feature bits: W2 bit 1, **E4 stage 2 bit 2** (correction to the E4 design's section 7).
- Agreement: rule 1 rev 3 / rev 4 (E4), rule 44 ActorAI (E4), rule 47 NativeSaveLayout (W2). Three independent entries;
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
- T3 ledger mutation: one guard byte changed -> nothing written, rule 47 unverified.
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
  +/-40000 bit-exact, world slot 1023 in the +0x12 field; eviction at the 9th zone (LRU); **disposal: evicting /
  resetting a record that owns slots 0, 255, 300 and 1023 releases exactly those slots (the adapter of 3.5) and leaves
  every other slot allocated**; `CSmartEntMgr` replacement at 2048 vs the static object at 768 (differential at <= 768
  over the whole method family); zone-string table 8191 names / arena 0x1ffff bytes interned and found again, the 8192nd
  refused with the limit named.
- T9b boundary pairs distinguish the **largest valid index** from the **largest valid count** for every table (index
  1023 / count 1024 for definitions, instances, world slots, objectives, ordinals; state record 4095 / count 4096; record
  4095 / 4096 enhancements; talent position M-1 / M registrations): the index case loads, the count+1 case refuses by
  name.
- T10 objectives: registrar 1023 / 1024 (refuse), per act 127, 64 files; state of objective 255 / 256 / 1023 survives
  act change, side-mission push / pop (two frames), **a whole-record copy through 0x488430 (side table follows), a
  pool slot release and reuse (side entry 0xffff in between)**, and a snapshot copy; `objective_text` shows the right
  description for objective 300; ForcedTeams reads the W2 stack count and writes names into the right frame.
- T11 talents: pools 255 / 256 / 257 per pool (**the generation loop replacement at 256 touches indices 0..255 only; a
  canary word after each table is unchanged**), eight-dword bitmap clears; M = 200: ids of stats index 21 position 10 =
  4410, position 199 accepted, position 200 refused by the validator; shared cap 199 through the thunk; the endpoint
  cave at 0x4c0644 yields base + 200; `missing_talents` classification.
- T12 fail-closed: a page made read-only / a guard wrong / backing allocation failing -> nothing written, rule 47
  unverified, online closed, offline stock.
- T13 agreement: rule 47 present only with the key; E10A2 vs W2 refused naming `47 NativeSaveLayout` both ways; a
  wave-1 peer "a rule this PC does not have".

**G3 Live-save (real saves; the owner's 0.4.2 gate).** Collect, read-only, into
`agent-work/limits-wave2/real-saves/<owner>/` exactly the list of **census C3** (`census-c1-c2.md` section C3): priority 1
Owen's play profile (`Save\saveslot0..7.save`, `settings.dat`, the two backup folders, `SaveLC\` with 10 `.lcsave`, 7
`.bak`, `profile.lcsave` and `legacy-import.txt`), priority 2 the candidate profiles with real progress (0.2.0 .. 0.7.0b,
the coop / loot-cap / poolsfix profiles), priority 3 the harness captures for breadth; Shelley's `Save` / `SaveLC` through
her session on the OJAMD-Shared drop; the testers' zips (Discord thread); the 82-save retail corpus
(`accept-020-saves`, zone peaks already in `corpus-native-sizing.json`). This PC alone holds 594 save-type files in 369
unique contents (de-duplicate by SHA-1). Run (no game): for each file (a)
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
-32768.5, 40000.0 (bit-exact); talent ids at M = 200 for stats index 1 and 21; the **loader-condition matrix** of
section 5 (an empty-stack `loadmap` resets the cache, a stacked `restorelastzone` return and a plain zone transition
keep it; the manager flag +0x1afd4 set suppresses both); each case loaded on an E10A2 build (refused by name or loaded, per section 7's
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
| Agreement | rule 47; refusal text | - | Sol |

### 11.2 RE checks: answered (RE-1..13, GPT Astra high, report e377) and open (RE-14..16)

| # | Question | Answer (A = Astra; V = re-read here) | Folded into |
|---|---|---|---|
| RE-1 | `CInventorySystem`+0x2589 free? | **LIKELY** padding: accesses to +0x2588 only; count stores 0x4802b8, 0x480576, 0x4805ed; next field +0x258c (A) | 4.1 (u16 in place, init both bytes) |
| RE-2 | The 13 by-name callers | **CONFIRMED**: 10 pass EAX on; 3 truncate: 0x49ed9d, 0x49ee11, 0x4d47da (V); the success return 0x47e393 `mov al, bl` also widens (V) | 4.1 |
| RE-3 | Reward record layout | **LIKELY**: stride 0x14, definition +8, +9 unused, u16 at +0xa (0x4d47b7 V); sentinel compares 0x4c8fd2 / 0x4ca129 `cmp byte [esi+8], 0xff` (V) | 4.1 (u16 at +8 in place) |
| RE-4 | Picker semantics | **CONFIRMED**: specification in 4.2 | 4.2, T6 |
| RE-5 | `CInventoryEntity`+0x321 | **LIKELY** padding: constructor 0x47aa1c (slot) / 0x47aa23 (quantity); other +0x320 dword accesses are other owners (A) | 4.4 (owner-specific ledger) |
| RE-6 | Loaded objective +0x1aa | **CONFIRMED occupied** (target count / flags; constructor 0x4886e5..f0, parser 0x488970..ca, copy 0x488430, journal); +0x1ab no direct access (A) | 4.5 (side table) |
| RE-7 | Other objective-index holders in the fix | **CONFIRMED**: `objective_text` (narrows, fixed 256 / 0x72b118, shares the allocation site), `test_state` (byte, 75), ForcedTeams (0x26c / 0x4dc); no holder in the fix's script functions (A) | 3.1, 4.5 |
| RE-8 | State-layout sites | **LIKELY complete** for the owner regions: list in 4.6 (A; 0x465f17 and 0x465ec0's loop V) | 4.6, 3.5 |
| RE-9 | `CSmartEntMgr` | **CONFIRMED**: vtable 0x68e440, 8 methods, new layout offsets, owner-region hash (A) | 4.6 |
| RE-10 | Callers of the multiplier | **CONFIRMED**: function 0x4bdc00, vtable 0x68ec24 +0x30, caller 0x4c061e; 0x4bdc12 is the 6-byte tail (V) | 4.7 |
| RE-11 | The 27 immediates | **CONFIRMED**: pools A / B / C, common fullness 0x4c0115, M, cap, unrelated 0x4c07c5; plus the missed endpoint 0x4c0644 (V) | 4.7 |
| RE-12 | Registry layout | **CONFIRMED**: 0x3228 at 0x4c0584 (V), ctor 0x4c0370 (V), singleton 0x77339c, three pools + index tables (A) | 4.7 |
| RE-13 | 0x484370's slot and callers | **CONFIRMED chain**: vt+0x34 (V), caller 0x484627 in the unload method 0x4844a0 = vt+0x30 (V), zone loader 0x484e48 (V); mission-wide behaviour **NOT FOUND** | 5 |
| RE-14 | Zone-string table sites | **CONFIRMED sites, LIKELY complete** (A, report 0822): six masks incl. 0x419d70, 31 displaced fields, 24 base-relative accesses, stream sizes, static getter, the GC frame, unbounded probing, `capture_compare` cave, consumers (V: 0x419d70, 0x484ac1 / 0x484ba6, 0x466020 / 0x46602e / 0x4661b7 / 0x466052 / 0x4661ff, 0x41a414, 0x41a42f, 0x4656a1, 0x6021a0 / 0x6021c0) | 4.6 (d), rows W2-E15-14a..l, W2-L-17 |
| RE-15 | Callers of `CMap` vt+0x30 and their boolean | **CONFIRMED values, LIKELY exhaustive** (A): three immediate callers, the loader condition, eight CGame callers, the `loadmap` / stack-count script connection (V: 0x6887bc, 0x484e00, 0x484e0b, 0x484e28, 0x4698cf, 0x4876a9, 0x5f47dd, 0x484101, 0x484064) | 5 (matrix), G4, L3 |
| RE-16 | Objective copy destinations | **CONFIRMED** (A): all three 0x488430 callers are in the heapsort 0x489680 (called at 0x489a60), destinations are stack temporaries, plus six `rep movsd` copies (V: the three calls, the caller, the six `mov ecx, 0x6b`) | 4.5, row W2-E14-04b |
| C1, C2 (done: `census-c1-c2.md`) | C1: team bonuses 0, immunities 14, shared 63, largest hero file 15 -> M = 200 final, pool 256 needed (99 of 100 today); C2: largest XML1 zone 582 placements (XML2 stock 750), loader arrays 1,024 unbounded -> not raised in W2, validator + runtime guard (3.6) | 4.7, 3.6 |
| C3 (collection list done; the runs are G3) | peak instances / attached / world items / objectives / definitions / talents / zone-string names per real save; the probe verdict per file | G3 |
| **RE-17** (open, later runtime row) | esp-displacement map of the zone loader 0x484ce0 relative to its seven 0x400 arrays, for the 2,048 raise after W2 | not W2 |

None of the open three changes a decision above: RE-14 completes a ledger whose shape is known, RE-15 decides wording
and a live-gate line, RE-16 decides whether the side table's one-entry shadow is needed.

---

## 12. Implementation order, risks ranked, and routing

| Step | What | Gate | Who | Risk |
|---|---|---|---|---|
| 1 | **DONE (6f543e6)**: descriptors (`NativeShape`, `w2_layout`, `w2_profile`), layout identity, Python mirror | G1 static asserts, parity of the three layouts (77 parity cases, 0 failures) | Sol + Sonnet | low |
| 2 | **DONE (6f543e6, 2ad158b)**: converter both directions, walkers, demand caps, the probe in probe mode and bit 1 with its edge cases, named refusals, profile overloads, talent codes, the three consumer modules on the layout view; the `[Limits] SaveLayout` key and combination table wait for the W2 install (nothing can select W2 yet, the baseline is untouched) | G1 (vectors, round trips, legacy vectors, 74 W2 check sites, fuzz 2000 x 2), G4 fixtures | Sol | low-M |
| 3 | Ledger generator for E15 + E9 (record 0x20, 8 records, R6 counts, `CSmartEntMgr` method family, zone-string table 8192 / 0x20000 incl. the GC frame and `capture_compare`'s arena cave, W2-E15-14d), position functions, record clear + disposal adapter (non-owning slots 0xffff), capture bound, loader runtime guard; publish the string-table backing to `zone_strings_object` | G2 T1-T4, T9 | Sol (RE-8 / 9 / 14 answered) | M (large, mechanical) |
| 4 | E3c pools and entity u16 slot (owner-specific), disposal chain; E3b pool; E3 records + sentinel + pickers to the RE-4 spec | T7, T8, T6, T9 disposal, guard-page proof | Sol (RE-4 / RE-5 answered) | M-L (99 pool sites, shared helper, picker reimplementation) |
| 5 | E2 definitions (RE-1..3 answered: u16 count in place, 3 truncating callers + success return, reward u16 at +8) | T5, text gate | Sol, Opus review | **L** (the largest mixed inventory) |
| 6 | E14 objectives: side table, heapsort replacement, pool relocation shared with objective_text (**wire `objective_text::contribute()` into the startup transaction here**), snapshot 0x540 / stack 0xa88; ForcedTeams + test_state already on the view | T10 | Sol | M |
| 7 | E7: whole-registry relocation at 256, generation loop replaced, eight-dword clears, M = 200 (helper tail, endpoint cave, cap thunk), consumers, builder V44 + position rule | T11 | Sol (RE-10..12 and census C1 answered) | M |
| 8 | Rule 47, builder key, refusal texts, test-pipe status (`layout W2; defs n/1024; enh n/4096; att n/4096; inst n/1024; world n/1024; obj n/1024; talents n/256; zones n/8`) | T12, T13 | Sol + Sonnet | low |
| 9 | G3 real saves (the C3 list: collection, offline runs, census table), G4 in game, live runs (cold load, zone change, mission boundary as evidence, co-op) | G3, G4 | Sonnet (collection + offline), Sol (live), owner's own test last | - |
| 10 | Second-model review of the whole diff (Astra high or Sol high, read-only), then one build; the coordinator assigns the identity | review | Astra / Sol | - |

Risks, ranked: E2 (mixed inventory; a missed byte consumer gives a wrong item silently - the text gate T4 and the
guard-page proof are the answer) > E3c (99 sites plus a shared bitset helper - the clone must leave the original for
its other callers) > E14 (snapshot and stack relocation touch three copies and two stack frames) > E15 / E9 (large
but generated; the risk is a missed 0x18 stride - RE-8) > E3 pickers (semantic reimplementation - T6 differential) >
E7 (whole-registry relocation; the five-at-a-time loop) > E3b (ledger complete, sweep done). RE-14 / 15 / 16 are
answered (report 0822) and folded into the ledger; no RE gates the start.

---

## 13. Answers to the first Astra review's eight CONFIRMED findings (revision 2), with the re-review's verdict

| # | Finding | Answer | Re-review (e377) |
|---|---|---|---|
| 1 | Native layout underspecified (count byte, 160 pairs, snapshots, rules.hpp walkers) | Sections 3.2-3.7 specify every structure with offsets for all three layouts; 6 names the walkers (`hero_heaps` etc.) as profile-driven; 3.1 lists the DLL consumers that must read the descriptor | Closed |
| 2 | Site inventory omits the 52 instance-pool instructions, the +0x320 entity byte, the payload helpers | 4.4 includes all 52 by category, the entity u16 slot (RE-5) and the 5 vt+0x120 callers; 3.5 keeps the payload helpers' 8-byte bound, gives the slot its own field and adds the disposal adapter | Closed (disposal added in revision 3) |
| 3 | E9 lacks ownership / lifecycle; R17 open | Section 5: records 6-8 are native (no DLL swap), the conditional unload lifecycle (vt+0x30 / vt+0x34, 0x484627, 0x484e48) is described, the disposal adapter covers eviction and reset; the "survive missions" claim is withdrawn; RE-15 open | Not closed in revision 2 -> revised in revision 3 |
| 4 | E7 needs native changes | 4.7: the 0x4bdc00 helper (vtable 0x68ec24 +0x30, caller 0x4c061e; tail 0x4bdc12), the 0x4c05d9 cap via a thunk, the endpoint 0x4c0644, the 27 classified immediates, the five-at-a-time generation loop replaced, eight-dword bitmap clears, whole-registry relocation (0x3228), consumers, historical imports at 100, the position < M rule; census C1 fixes M = 200 | Not closed in revision 2 -> revised in revision 3 |
| 5 | Layout selection by `usable_end`; OFF; agreement | 3.9 (identity, combinations, atomic publication, baseline / stock semantics, overlapping hooks participate), 8 (rule 47 covers the whole format) | Closed |
| 6 | R6 capacities and the payload arithmetic | Section 2 (sentinel convention), 3.6 (counts; loader arrays decided with census C2), 3.8 (1,421,114 bytes with the raised string table = 67.8 % of 0x200000, 1,279,802 with the retail table; payload not grown), name storage sized (8192 / 0x20000) | Not closed in revision 2 -> revised in revision 3 |
| 7 | Representability rules | Section 7: a target-aware probe (target translation + engine constraints + target mappings + drops + feature gates) decides bit 1; the hand list is the oracle; re-based talent ids never mark; the generic message on released binaries is accepted | Not closed in revision 2 -> revised in revision 3 |
| 8 | Separate codec / engine / live gates; full-capacity pickers | Section 10: G1-G4 with exact comparisons, the boundary list and T9b (largest index vs largest count); 4.2: pickers replaced at full capacity to the RE-4 specification; G3 collects the census C3 list | Closed |

### 13.1 The re-review's seven new findings (revision 3)

| # | Finding | Answer |
|---|---|---|
| 1 | Eviction can free the wrong world item (0x465ec0 -> 0x479c10 -> 0x4655d0 byte -> 0x479ad0) | 3.5 "Disposal adapter" (chain re-read: 0x465ee7, 0x479c2b, 0x479aed, 0x479af5 V); T9 disposal line; ledger rows W2-E3c-10..12 |
| 2 | Objective widening overwrites +0x1aa; objective_text / ForcedTeams / test_state keep literals | 4.5: side table with pool-slot key; the heapsort 0x489680 (nine moves, RE-16) replaced whole or bridged with an invocation-scoped shadow; the three modules read the descriptor and join the transaction (3.1, 3.9); rows W2-L-10..12, W2-E14-04a/b |
| 3 | E7: endpoint 0x4c0644, five-at-a-time loop 0x4bfa20..0x4bfaf8, four-dword clears | 4.7: endpoint cave, loop replaced whole, eight-dword clears, whole-registry relocation from RE-12's layout; rows W2-E7-01..07 |
| 4 | A reverse dry run does not prove old-engine representability | 7: target-aware probe in probe mode (definition indices per build) with the engine constraints (objective > 255 per registered objective, records < 512, pools, positions), the target's talent mapping (M = 100), drops and feature gates, and the bit-1 edge cases; row W2-L-08 (done) |
| 5 | Name storage is a smaller independent ceiling | Section 2 / 3.1 / 4.6 (d): 8192 slots, 0x20000 arena, saved 0x28008; stream bound recomputed (3.8); RE-14's full inventory incl. the GC frame and bounded probing; rows W2-E15-14a..l, W2-L-17 |
| 6 | R17 overstated | 5: the loader-condition matrix and the eight CGame callers (RE-15); "survive missions" withdrawn; G4 / L3 check the matrix |
| 7 | Explicit position < M rule | 2 / 4.7: `p < M`, shared `< M-1`, `298 x M - 1 <= 65535` -> M in 101..219; builder validator refuses beyond; pool capacity bounds registrations only |

Also folded: the E10A2 offsets of 3.7 (0x809a8 / 0x809ac / 0x809b0 / 0x80c1c / 0x80c20), the stream bound (10 tags,
list counts, 75 courses), rule ID 47, the explicit initialise / carry requirement for the new record fields (3.5), T9b,
census C1 (M = 200 final; the registry pool is the tight table), census C2 (loader arrays stay 1,024 with a validator
error and a runtime guard; RE-17 for the later raise), census C3 (the G3 collection list).

## 14. What could not be settled statically, and why the plan is safe anyway

- Whether every narrow holder of a widened index is in the ledgers (SPEC-old U16). The sweeps are pattern-based; the
  **guard-page proof** (old storage unmapped in a test build; any access faults) and the whole-exe text gate are the
  dynamic and static answers; a miss is caught before a save is written, never after.
- RE-1 / 3 / 5 are LIKELY (sweep-based), not CONFIRMED: each in-place widening is guarded on its bytes and has a named
  fallback (side table / cave); the union-host tests write and read the widened fields through retail code.
- The picker semantics (RE-4, specified) and `CSmartEntMgr`'s method family (RE-9, enumerated): whole-function
  replacements are gated by differential tests against the retail code on the union host.
- Whether every converted mission boundary matches XML1's `DumpZoneMission` (RE-15 gives the engine's matrix and the
  script paths, not a per-boundary proof): a behaviour question outside the layout; W2 preserves the engine's conditions
  unchanged and L3 checks the matrix.
- RE-14's inventory is LIKELY complete (direct-reference and literal sweeps): the guard-page proof on the retired
  0xa26c30 object catches any indirect reader; 8,191 non-empty names is an explicit supported limit.
- The zone loader's 1,024 placement arrays stay unbounded natively; the validator error and the runtime guard make an
  overrun impossible for shipped content and loud for mods (3.6).
- The in-game truths no fixture certifies (bonus application after load, zone transitions, co-op): G3 on the real
  saves of the C3 list, before the tag.

## 15. Implementation-ready ledger

One row per unit of work an implementer builds and a checker verifies independently. **ID** is the handle both use;
**Site** is an address, a range or a SPEC-old group (whose rows are the exact instruction list); **Retail** is what the
guard expects (bytes quoted where re-read in this pass, otherwise the SPEC-old row bytes); **W2** is the change;
**Kind**: I = in-place value edit (same length), R = regenerated displacement / immediate from the descriptor, C = cave
(instruction grows), F = whole-function replacement under a region hash, D = DLL-side data or object, S = source
(converter / rules / consumer), T = test; **Status**: V re-read here, S SPEC-old (verify_step0 PASS), A Astra e377
(CONFIRMED / LIKELY as given), O open. The check column is the test ID of section 10 that proves the row.

### 15.1 Layout, converter and consumers (S rows)

| ID | Site | Retail | W2 | Kind | Status | Check |
|---|---|---|---|---|---|---|
| W2-L-01 | `lcsave/layout.hpp` `make_native_layout` | state size and record count constants | parameters `state_record_size`, `record_count`, `zone_string_slots`, `zone_arena`; `w2_layout` = section 3.1; static asserts of 3.6 / 3.7 | S | **done 6f543e6** | G1 |
| W2-L-02 | `save_format_rules.hpp` `native_profile` | `retail_profile`, `e10a2_profile` | `w2_profile` with the same fields; cross static asserts against `w2_layout` | S | **done 6f543e6** | G1 |
| W2-L-03 | `save_format.cpp:2056` | `usable_end` comparison | select by `profile.name` through `lcsave::native_layout(id)`; veto on null / mismatch; `lc_known_required_features = layout->feature_bits` | S | **done 6f543e6** | G1, T12 |
| W2-L-04 | `oldsave.cpp` item head / instance head / snapshot / state record / zone strings / world slots / zone count (`ceil_zone_records`, `internal.hpp:28`) | retail widths, `ceil_zone_records = 5` | layout-driven widths; count <= `native.record_count`; non-owning records carry slot 0xffff (3.5) | S | **done 6f543e6** | G1 |
| W2-L-05 | `reverse.cpp:100, 241, 446, 505, 909`; `api_engine.cpp:453`; `save_format.cpp:1225-1228`; `convert.cpp:25` | literal 256 / 375 / 150 / 300 / 5 / {375, 512}; i16 position refusal | descriptor values (`demand_refusal`); W2 positions stored as floats; narrowing messages name the layout's limit | S | **done 6f543e6** | G1, G4 |
| W2-L-06 | `save_format_rules.hpp:1143-1240, 1332, 1528` (`item_head_size`, `instance_fixed`, equipped walks) | 0x165 / 5 | `item_head_size(profile)`, `instance_fixed(profile)`; static assert against `OldItemHead` | S | **done 6f543e6** | G1 |
| W2-L-07 | `tables.cpp:557` and **`:733` (`herostat_talents`)**, `api.cpp:207`, `reverse.cpp:190`, missing-talent insertion, the Python `PositionTables._tal` / preflight owner check | `* 100`, `< 100`, `/ 100` ids in the tables | tables keep (owner + 1, position) **codes**; `talent_engine_id(code, layout)` = `(owner + 1) x M + position`, refusing position >= M / shared >= M - 1 / id > 0xffff by name; legacy importer keeps 100 | S | **done 6f543e6** | T11, G1 |
| W2-L-08 | Representability probe (section 7): `reverse.cpp:955` `representable`, probe mode `reverse.cpp:124-131, 680`, saver `convert.cpp:1430-1446` | - | `representable(file, e10a2)` in probe mode (definition indices per build, model not written at target widths, carried bit 1 ignored) + engine constraints (objective > 255 **per registered objective**, records >= 512, pools, positions) + target talent mapping + drops + feature gates; sets bit 1 exactly when it fails; names the rule | S | **done 6f543e6** | G4 oracle |
| W2-L-09 | `container.cpp:602, 608`, `rules.hpp:640-643, 954`, `typed_registry.cpp`, slot list | generic "needs a newer version" | `w2_refused` names `[Limits] SaveLayout=w2` and 0.4.2 / 1.9; slot list "(needs the W2 save layout) <description>"; RETAIL + bit 1 -> the downgrade text; bit 1 without bit 0 -> "damaged marker" refusal, never written | S | **done 6f543e6** | G4 |
| W2-L-10 | `objective_text.cpp` / `_rules.hpp` (`state_bytes 0x72b118`, `index_offset 0x1a9`, 256 entries, allocation site) | literals | reads the layout's objective view (1024-entry table by global index, index / state array from `save_layout_view`); `contribute()` provided, standalone `install()` commits through `patch_transaction` (same four writes, same bytes); **the W2 install wires `contribute()` into the startup transaction at step 6** when the allocation-site overlap exists | S | **done 2ad158b** (step-6 wiring open) | T10, T2 |
| W2-L-11 | `forced_teams_rules.hpp:428-433` (`side_record_size`, `side_count_offset`, names offset) | 0x26c / 0x4dc / 0xa4 | descriptor snapshot size and count offset (0x540 / 0xa84 under W2) | S | **done 2ad158b** | T10 |
| W2-L-12 | `test_state_rules.hpp:27-29, 502-512` | byte index, "<= 75", 0x72b118 | descriptor-driven | S | **done 2ad158b** | T10 |
| W2-L-13 | Python reference `tools/lcsave/layout.py` + vector export | RETAIL / E10A2 | W2 descriptor, `w2-*` modes | S | **done 6f543e6** (legends-classic `tools/lcsave/*`, `export_lcsave_typed_vectors.py` `w2-reverse` / `w2-bit1`) | G1 |
| W2-L-14 | Builder `fix_ini.py`, V44, key lists, harness; zone validator | - | `SaveLayout=w2`, `ItemEnhancements=4096`; validator refuses talent position >= M, shared >= M-1, definitions > 1024, **any zone with > 1024 placements** (C2 counting rule) | S | - | builder tests |
| W2-L-15 | Agreement rule 47 (section 8); status line | - | registered only with W2 installed; values as 8 | S | - | T13 |
| W2-L-16 | Zone loader preflight (3.6): 0x485724, CMap owner [0x484ce0,0x486353) | parser 0x461d99 silently truncates at 1024; numbering 0x48624c is too late | count recursive `<inst>` nodes before parser fills arrays; refuse by zone name and use native reader/array cleanup | S/C | implemented step 3 | T9 (1024 accepted, 1025 refused before parser), T3 (outside-hook frame mutation) |
| W2-L-17 | `save_format.cpp:984` `record_zone` (DLL consumer of the zone-string table): reads the static object **0xa26c30** directly with 2,048 slots and the retail displacements (+0x2004, +4 + slot*4, +0x2008) | hard-coded retail object | reads through the published backing pointer and the descriptor's slot count / arena-used / arena offsets (W2: the DLL backing, 0x2000 slots, +0x8004 / +0x8008) | S | **done** (`zone_strings_object`, 0xa26c30 until the W2 install publishes its backing) | T9 (a record whose name lives in slot > 2047 is named) |
| W2-L-18 | Profile file directions `old_profile_to_new` / `new_to_old_profile` (`lcsave.hpp:314-315`, `api_engine.cpp:102, 125`, `reverse.cpp:941`), `save_format` call sites | retail-only; refuses a build with > 160 definitions | layout overloads; `save_format` passes the installed layout | S | **done 6f543e6** | G1 (a 1024-definition build's `profile.lcsave` round-trips) |

### 15.2 Zone records, manager, strings, smart manager (E15 / E9)

| ID | Site | Retail | W2 | Kind | Status | Check |
|---|---|---|---|---|---|---|
| W2-E15-01 | SPEC-old 2.7 record offsets (49) + record size 0x55e0 (17) + 2.8 container offsets (43) = Phase A "Native saves" rows | E10A2 values | regenerated from `w2_layout` (3.6 / 3.7) | R | S | T1, T9 |
| W2-E15-02 | 0x46699c `83f805`; 0x487461 `6a05`; 0x487685 `6a05` | 5 | 8 | I | V | T9 |
| W2-E15-03 | 0x4669b0 `8d96b8ab0000` | record 1 time stamp 0xabb8 | 0x48708 | R | S | T9 |
| W2-E15-04 | 0x48733d `68dcaf0100` and the Phase A backing claim at 0x487344 / 0x487362 | 0x1afdc (E10A2 0x80c24) | 0x122190 backing | R | S | T9 |
| W2-E15-05 | 0x4666bd `81fb00010000` | capture scans handles < 0x100 | < 1024 | I | V | T9 |
| W2-E15-06 | scaled 3x8 addressing 0x465c2f/32 (+4 added at 0x465c3e), 0x465c87/8a, 0x4666ab/ae, 0x466736/739, 0x466838/83b/83f | stride 0x18 as 3x8 | stride 0x20 (shift or regenerated scale) | C/R | A | T9, T4 |
| W2-E15-07 | stride advances 0x465f17 `83c618` (V), 0x4660ee, 0x4662fd, 0x466415 | `add r, 0x18` | 0x20; 0x465f17 is in the whole record-clear replacement, 0x4660ee / 0x4662fd / 0x466415 in the copied whole GC owner; no external advance | I/F | implemented step 3 | T9, T4 |
| W2-E15-08 | 0x465ec0 record clear (0x465ec0..0x465f8f) | per-record clear, 256-slot map clear 0x465f32, disposal call 0x465ee7 | replaced whole: W2 record fields (slot 0xffff, floats 0), 4096 map clear, calls the disposal adapter (W2-E3c-10) | F | V | T9 |
| W2-E15-09 | constructors / clears 0x465e70, 0x4664a0, 0x487610; vector strides 0x48759a, 0x48764a, 0x48766a | 0x18 records | 0x20 records, new fields initialised | R/C | A | T9 |
| W2-E15-10 | smart-capture copies 0x46678b..0x46679f; restore copies 0x46686c..0x466882 (24-byte stack record); loop clear [0x466906,0x466932), funclet 0x673630; 0x4b7630 dead under E15-13 | 24-byte copies | 32-byte copies; stack frame grown | C | A | T9 |
| W2-E15-11 | 0x4656c0 packer, 0x4654f0 reader (`0fbf4e12` 0x46550c, `0fbf5614` 0x465526) | i16 round / clamp | F: store / load 3 x f32 at +0x14 bit-exact | F | V | T9 |
| W2-E15-12 | bounds 0x46667a, 0x46668a, 0x4666f3 and `save_core_rules::capture_bridges` | 1024 / 1024 / 1024 (E10A2) | 4096 / 2048 / 2048 from the profile | R | S | T9 |
| W2-E15-13 | `CSmartEntMgr` via getter 0x4b76f0; vtable 0x68e440 (8 slots: 0x4b76d0, 0x4b7100, 0x4b7000, 0x4b70e0, 0x4b6fc0, 0x4b6fb0, 0x4b6ff0, 0x4b75c0); SPEC-old 2.7 R2 block (45 rows) | static 768 x 0x18, count +0x4868 | DLL object: 2048 x 0x20 at +8 (vtable +0, cursor +4), bitmap +0x10008, count +0x10108, size 0x1010c; whole method family; append-copy 0x4b75c0 refuses at 2048 (retail has no bound); owner hash SHA-256[0x4b6e70,0x4b7744) = 1969cfd9...04a4b | D/F | A | T9 (differential <= 768) |
| W2-E15-14a | zone-string object layout (`OldZoneStrings`): generation +0, offsets +4, arena-used, arena | 0x800 x u32, used +0x2004, arena +0x2008, 0x5808 | DLL backing **0x28008**: generation +0, 0x2000 offsets at +4, **arena-used +0x8004, arena +0x8008**; generation encoding, 24-bit handle index, reserved slot 0 (value 1) and initial arena-used 2 preserved | D | A (RE-14) | T9 (strings) |
| W2-E15-14b | hash / probe masks: **0x419d70** `81e3ff070000` (collision-probe helper called by intern, V), 0x41a3fb `25ff070000` (V), 0x465816, 0x46582d, 0x4658d1, 0x4658df | `and r, 0x7ff` | `and r, 0x1fff` (six sites, same length) | I | V + A | T9 |
| W2-E15-14c | offset-table clears 0x46568d, 0x465fed, 0x4661d8, 0x466394, 0x6021b9 (`mov ecx, 0x800`); GC slot bound 0x466258 (`cmp esi, 0x800`) | 0x800 | 0x2000 (imm32 in place) | I | S + A | T9 |
| W2-E15-14d | arena bound 0x41a427 `81f900380000` - **already a cave of the LC seam**: `save_core_rules.hpp:138` `capture_compare` replays the retail 0x3800 | 0x3800 replayed by the existing capture bridge | the bridge replays the descriptor's `zone_arena` (0x20000): regenerate that cave, do not add a second patch on the site | R | V (source) + A | T9, T2 |
| W2-E15-14e | arena-used displacement 0x2004 -> **0x8004** at 0x41a414 `8b8304200000` (V), 0x41a41e, 0x41a446, 0x4656a7, 0x465ac5, 0x465b2f, 0x465b47, 0x465bb5, 0x465bc9, 0x46600a, 0x4661f4 (11) | +0x2004 | +0x8004 (disp32 in place) | R | V (one) + A | T9 |
| W2-E15-14f | arena displacement 0x2008 -> **0x8008** at 0x419d62, 0x41a42f `8dbc1808200000` (V), 0x46569b, 0x4657d7, 0x465882, 0x4658aa, 0x4658f2, 0x465af0, 0x465bfd, 0x465cc3, 0x465ce6, 0x465dd4, 0x465ffe, 0x4661e8, 0x466234, 0x46623d, 0x4668b0 (17); second reserved arena byte 0x2009 -> **0x8009** at 0x4656a1 `888209200000` (V), 0x466004, 0x4661ee (3) | +0x2008 / +0x2009 | +0x8008 / +0x8009 (disp32 in place) | R | V (two) + A | T9 |
| W2-E15-14g | offset-table accesses that keep `base + 4 + index*4` (no displacement change; they need the right backing and index): probe / intern 0x419d4b, 0x419d56, 0x419d7a, 0x41a41a; rehash / compaction 0x465820, 0x465844, 0x4658d8, 0x465913; lookup consumers 0x465bf9, 0x465cbf, 0x465ce2, 0x465dd0, 0x4668ac; GC 0x46622c, 0x466251, 0x4662ac, 0x4662d9, 0x4662f1, 0x466351, 0x46637b; clear bases / reserved-slot stores 0x465686 / 0x465695, 0x465fe8 / 0x465ff7, 0x4661d1 / 0x4661e0, 0x46638f / 0x46639d | retail backing | unchanged bytes; verified by the guard-page proof (W2-T-GP) that none still reaches 0xa26c30 | - | A | T9, W2-T-GP |
| W2-E15-14h | stream sizes 0x484ac1 `6808580000` (save) and 0x484ba6 `6808580000` (load) (V) | `push 0x5808` | `push 0x28008` (imm32 in place); the `[MAP: ZONESTRINGS]` block is 0x28008 bytes in the W2 stream (3.8) | I | V | T9, G1 |
| W2-E15-14i | static getter / initialiser **[0x6021a0, 0x6021f4)** (`test byte [0xa2c438], 1` V; backing addresses embedded at 0x6021c0 `bf346ca200` (V), 0x6021c5, 0x6021cd, 0x6021d7, 0x6021dd, 0x6021e3, 0x6021ee) | returns and clears the static 0xa26c30 object | F: returns and initialises the DLL backing (a larger clear against retail storage would overwrite adjacent globals) | F | V + A | T9 |
| W2-E15-14j | **GC frame 0x466020** (`sub esp, 0x110` V; releases 0x4661ff `add esp, 0x110` V, 0x466496): one bit per string slot: bitmap clear 0x46602e `mov ecx, 0x40` (V), scan bound 0x4661b7 `83f840` (V), bitmap base 0x466033 with indexed accesses 0x46607e, 0x466082, 0x4660b9, 0x4660bd, 0x4660da, 0x4660e5, 0x466154, 0x46615d, 0x46618f, 0x46619a, 0x4661b0, 0x466221; stack argument `[esp+0x124]` at 0x466052 (V), 0x466292, 0x4663b7 | 0x110 frame, 64-dword bitmap, bound 0x40, arg +0x124 | **frame 0x410, 256-dword bitmap (0x400 bytes at esp+0x20), scan bound 0x100 (`80fc01`, cmp ah,1; same length), stack argument [esp+0x424]**: a whole-frame replacement under a region hash of [0x466020, 0x46649f), as Phase A's caller-frame replacements; the copied body also regenerates every Phase A save operand for W2 | F | implemented step 3 | T9 (GC with slots above 2047), T4 (copied-body offsets/strides) |
| W2-E15-14k | **bounded probing and failure contract**: the probe 0x419d40 has **no full-cycle stop** (it loops until a match or a free slot), and the intern publishes the offset at 0x41a41a **before** the arena space check at 0x41a427 | unbounded probe; offset published before the space check | the intern / probe pair is replaced (F, region hash) so that a full table (8,191 non-empty names) and a full arena are **named refusals** that publish nothing: the probe counts tries against `zone_string_slots` and the offset is written only after the space check succeeds; the capture flag (incomplete) is raised and the write refused, as a full state list is today | F | A | T9 (the 8,192nd name refused by name; collision-full table) |
| W2-E15-14l | `OldZoneStrings` consumers in the library and DLL: `lcsave/internal.hpp:112` (fixed `OldZoneStrings` member), `oldsave.cpp:246, 281, 558, 661` (fixed copies / stream reads and writes), `reverse.cpp:288-309` (intern with `native.zone_string_slots` / `zone_arena`: already descriptor-driven, the struct is not) | fixed 0x5808 struct | layout-driven representation (slot count and arena from the descriptor; retail / E10A2 keep 0x5808) | S | A | G1 |

### 15.3 Item instances, world slots, attached pool, enhancement records (E3c / E3b / E3)

| ID | Site | Retail | W2 | Kind | Status | Check |
|---|---|---|---|---|---|---|
| W2-E3c-01 | instance pool [0x72a524]: SPEC-old 2.5 pool rows (47: capacity 0x100 / mask 0xff, stride 0x34, object 0x3c60) | 256 x 0x34 + tables | DLL backing 1024 x 0x34 + tables; all offsets regenerated | D/R | implemented steps 4/5 | T7 |
| W2-E3c-02 | ring init 0x483090 (12 rows of pool_omissions: +0x3824/+0x3828/+0x382c, ring +0x3420, 0x4830be `3d00010000`, 0x4830d7 `b8ff000000`, 0x4830e7 `81fa00010000`) | 256 / 0xff | 1024 / 0x3ff; offsets regenerated | I/R | implemented steps 4/5 | T7 |
| W2-E3c-03 | ring pop 0x483410 (5: +0x408 / +0x40c) | relocated fields | regenerated | R | implemented steps 4/5 | T7 |
| W2-E3c-04 | destructor walk 0x482a20 (6: bitmap +0x3400, 0x482a39 / 0x482a79 `81fa00010000`, helper call 0x482a89, 0x482a8e `3d00010000`) | 256 | 1024; helper re-aimed | I/R | implemented steps 4/5 | T7 |
| W2-E3c-05 | shared bitset helper 0x44d0d0 (bounds 0x44d0d4, 0x44d0de, 0x44d10b, 0x44d158, 0x44d15f) | 256-bit, shared with other callers | clone at 1024 bits; only the instance pool's calls re-aimed; original untouched | C | implemented steps 4/5 | T7 (clone vs original at 256) |
| W2-E3c-06 | eight-dword clears 0x48310b..0x48311f, 0x48313c..0x483150, 0x482a97..0x482aab | 8 dwords (256 bits) | 32-dword clears (block replaced as a unit) | C | implemented steps 4/5 | T7 |
| W2-E3c-07 | world-slot table manager+0x72a0 / bitmap +0x7750: 17 x imm 0x96 + 44 table / bitmap rows + unwind `add ecx, 0x72a0` (SPEC-old 2.5) | 150 x 8, 5 dwords | 1024 x 8 + 32 dwords at the manager's end (as `item_layout_for`); all regenerated | R/D | implemented steps 4/5 | T7 |
| W2-E3c-08 | `CInventoryEntity`+0x320 sentinel sites 0x479c64, 0x479d27, 0x479da9 `c68520030000ff` (V), 0x479f59, 0x47a117, 0x47aa1c; constructor init 0x47aa1c / 0x47aa23 | u8 slot, 0xff | u16 at +0x320, 0xffff (owner-class sites only) | I/C | implemented steps 4/5 | T7 |
| W2-E3c-09 | vt+0x120 callers 0x42ec6e, 0x479af5, 0x479d46, 0x47a017, 0x47a1f4; 0x47d390 | byte slot zero-extended | word | I/C | implemented steps 4/5 | T7 |
| W2-E3c-10 | **disposal adapter**: 0x479c10 (`push 1` 0x479c22, `call 0x4655d0` 0x479c2b) | reads payload byte +8 | reads u16 at record+0x12 (record pointer esi) via a DLL function | C | implemented steps 4/5 | T9 (disposal) |
| W2-E3c-11 | 0x479ad0 `0fb64c2410` (0x479aed) | movzx byte | movzx word | I | implemented steps 4/5 | T9 (disposal) |
| W2-E3c-12 | 0x479a70 slot write (`lea eax,[edi+0x320]; push 1` 0x479a88..) and 0x479fc0 slot read | payload byte | bridge: write / read the u16 field at +0x12; quantity bytes unchanged | C | implemented steps 4/5 | T7, T9 |
| W2-E3b-01 | `CPoolInterface<IAttachedEnhancements,375>`: SPEC-old 2.4 (87 rows), allocation 0x45fb0e | object 0x2984, 375 x 0x14 | DLL backing at 4096; all 68 layout rows regenerated | D/R | implemented steps 4/5 | T8 |
| W2-E3b-02 | second `findNext` clone of 0x45e5f0 (0x45e5f4, 0x45e5fe, 0x45e62b, 0x45e678, 0x45e67f); 0x45f309 `e8e2f2ffff` | 375 | clone at 4096; 0x45f309 re-aimed | C | implemented steps 4/5 | T8 |
| W2-E3b-03 | 0x45f920 full return -> 0x481938 skip | silent | logged error, load refused by preflight | C | implemented steps 4/5 | T8 |
| W2-E3-01 | `limits_rules.hpp:56` `max_item_enhancements` and the 41 `item_sites` | 1024 ceiling | 4096 | S | implemented steps 4/5 | T6 |
| W2-E3-02 | 0x47ad22 `663dff00` | `cmp ax, 0xff` | `cmp ax, 0xffff` (`663dffff`) | I | implemented steps 4/5 | T6 |
| W2-E3-03 | 0x47e690 (`81ecf4050000 55 56 8bf1`), 0x47ee90 (`81ecf4050000 53 55 8bd9`) pickers | 375-int stack arrays | F per the RE-4 specification, descriptor-sized arrays | F | implemented steps 4/5 | T6 |

### 15.4 Item definitions (E2)

| ID | Site | Retail | W2 | Kind | Status | Check |
|---|---|---|---|---|---|---|
| W2-E2-01 | definition array manager+4 (160 x 0x3c), state pairs +0x785c; allocation 0x480a25; constructors / destructors 0x480725-0x48073a, 0x48098f-0x4809a2, 0x673aa4; 18 `imul` + 6 loops (SPEC-old 2.2 groups) | inline, 160 | relocated to the manager's end at 1024 (same constructors on the new storage); stride 0x3c unchanged | D/R | implemented steps 4/5 | T5 |
| W2-E2-02 | 0x480430 `817c241ca0000000` | cap 0xa0 | 0x400 | I | implemented steps 4/5 | T5 |
| W2-E2-03 | count byte +0x2588: 31 sites; stores 0x4802b8, 0x480576, 0x4805ed | u8 | u16 in place (+0x2589 padding, LIKELY); word accesses, caves where longer | I/C | implemented steps 4/5 | T5 |
| W2-E2-04 | 14 accessor thunks (`cmp al`), 0x47c250 result, 3 external vt+0x18 callers, loader `inc cl` 0x480574 | byte index | word / dword | I/C | implemented steps 4/5 | T5, T4 |
| W2-E2-05 | picker 0x47b590 loop 0x47b732-0x47b769 (`inc al` 0x47b74e), sentinel tests 0x47d6a0 `3cff`, 0x47ecdf | byte counter / result, 0xff | cave: DLL loop; 0xffff | C | implemented steps 4/5 | T5 |
| W2-E2-06 | by-name lookup 0x47e320: 0x47e389 `0cff`, 0x47e393 `8ac3`; callers 0x49ed9d `88442408`, 0x49ee11 `88442404` | al result; byte stack slots | `or eax,-1` / `movzx eax,bx` (cave); the two stack stores widened with their readers; 10 other callers unchanged | C | implemented steps 4/5 | T5 |
| W2-E2-07 | Danger Room reward: 0x4d47da `884708`, readers 0x4c8fe1 / 0x4ca138, sentinels 0x4c8fd2 / 0x4ca129 `807e08ff`, reward constructor | u8 at +8, 0xff | u16 at +8 (+9 unused, LIKELY), 0xffff, readers / compares word | I/C | implemented steps 4/5 | T5 |
| W2-E2-08 | global 0x6d83bc (9 sites) | u8 | DLL u32, sites re-aimed | C/D | implemented steps 4/5 | T5 |
| W2-E2-09 | stream: 0x47c52c `6840010000` / 0x47f81f (0x140 block), 0x481622 / 0x482b98 (u8 definition) | retail head / instance head | W2 head (count-sized block, u16 count) and u16 definition | C | implemented steps 4/5 | T5, G1 |

### 15.5 Objectives (E14)

| ID | Site | Retail | W2 | Kind | Status | Check |
|---|---|---|---|---|---|---|
| W2-E14-01 | 0x4885c0 `817c241c2b010000` | 0x12b | 0x400 | I | V | T10 |
| W2-E14-02 | 0x4899e0 `83bf787d00004b` | 0x4b | 0x7f | I | V | T10 |
| W2-E14-03 | loaded-objective pool manager+8: stride rows (17), bitmap / count (16), unrolled clears (2) (SPEC-old 2.6); the allocation site shared with `objective_text` | 75 x 0x1ac | 127 x 0x1ac relocated; clears as caves; one union expectation with objective_text | D/R/C | S | T10 |
| W2-E14-04a | side table `wide_index[127]` (D); readers of +0x1a9 (13 sites, SPEC-old 2.6), registrar store 0x489a01 `8890a9010000` | u8 at +0x1a9 | readers -> `wide_index[slot]` caves; store writes both; pool allocation / release set 0xffff | D/C | V (0x489a01) + A (RE-6) | T10 |
| W2-E14-04b | **the heapsort 0x489680** (called at 0x489a60 `e81bfcffff` V; the only three callers of 0x488430 are inside it): nine record moves, each of a whole 0x1ac record - helper calls into **stack temporaries** `0x4896f5 -> 0x488430` (parent pool record -> temporary A at S+0x1c), `0x489795 -> 0x488430` (root -> temporary A), `0x489819 -> 0x488430` (selected child -> temporary B at S+0x1c8); and six `rep movsd` copies of 0x6b dwords at 0x489709 (child -> parent), 0x48971a (temporary A -> child), 0x4897a7 (last active -> root), 0x4897b9 (temporary A -> last active), 0x489829 (parent -> selected child), 0x489839 (temporary B -> parent); the six `mov ecx, 0x6b` sites 0x489704, 0x489711, 0x48979e, 0x4897a9, 0x489822, 0x48982b (V: exactly six in .text) | records permuted in place through two stack temporaries; the index byte travels inside the 0x1ac bytes | **the sort is replaced whole (F, region hash over [0x489680, 0x4898d1))** by a DLL heapsort that produces the identical record permutation and moves `wide_index[]` entries with the records (temporaries carry their wide index in a local). Alternative kept as a fallback only: bridge all nine moves with one shadow value **consumed solely on temporary-to-pool copies and scoped to one sort invocation** (a pool-to-pool copy copies `wide_index[src] -> wide_index[dst]`; a pool-to-temporary copy saves the source entry in the shadow; a temporary-to-pool copy writes the shadow into the destination). Hooking 0x4884d2 / 0x4884de alone never sees the copies back into the pool | F (or 9 x C) | V (calls, copies) + A (sequencing) | T10 (all three swap kinds with indices above 255; permutation identical to retail at <= 255 by differential) |
| W2-E14-05 | base table 0x72b590: 0x489252 store, 0x489bdf read | u8 x 25 | DLL u16 x 64 | D/C | S | T10 |
| W2-E14-06 | state array 0x72b118 (15 rows), 300-byte clears (4), snapshot copies (2: 0x4b dwords + 1) | 300 | DLL 1024-byte array; 0x100-dword copies | D/C | S | T10 |
| W2-E14-07 | snapshot constructor 0x487505 `682c010000`; capture 0x48a170 / 0x489a80; restore 0x489300 / 0x4892e0 | 0x12c / 0x26c layout | 0x400 / 0x540 layout | I/R | S | T10 |
| W2-E14-08 | side-mission stack 0x72b5b0 (frames +0 / +0x26c, pad +0x4d8, count +0x4dc; push 0x5f3630; save 0x46bcd4 / load 0x46e3ec copies) | static 0x4e0 | DLL block 0xa88 (frames +0 / +0x540, pad +0xa80, count +0xa84) | D/R | S | T10 |

### 15.6 Talents (E7)

| ID | Site | Retail | W2 | Kind | Status | Check |
|---|---|---|---|---|---|---|
| W2-E7-01 | registry object: allocation 0x4c0584 `6828320000`, constructor 0x4c0370, singleton 0x77339c; pools A +0x370 (0x2c; ring +0x1134, head +0x12cc, bitmap +0x12d4, count +0x12e4), B +0x1724 (0x24; ring +0xe20, head +0xfb8, bitmap +0xfc0, count +0xfd0, generations +0xfd4, mask / shift +0x1164 / +0x1168), C +0x289c (0x10; ring +0x644, head +0x7dc, bitmap +0x7e4, count +0x7f4); index tables +0x1658, +0x3094 | 0x3228, 100 per pool | DLL backing laid out from the descriptor at 256 per pool (B mask 0xff / shift 8); every pool-relative displacement regenerated | D/R | V (alloc, ctor) + A (layout) | T11 |
| W2-E7-02 | capacity / last-index immediates: A 0x4bdfe4 `83f964`, 0x4beabe, 0x4bead5, 0x4beae5, 0x4befa6; B 0x4be58f, 0x4be5ec, 0x4be62d, 0x4be650, 0x4be676, 0x4be6b8, 0x4be7e4 `83f964`, 0x4beb03, 0x4beede, 0x4beef5, 0x4bef05, 0x4bef60, 0x4bef7b, 0x4bf319, 0x4bf358, 0x4bfaf5, 0x4bfed1; C 0x4be044 `83f964`, 0x4be9ee, 0x4bea05, 0x4bea15; common 0x4c0115 `b864000000` | 0x64 / 0x63 | 256 / 255: imm32 forms in place where the instruction already is imm32 (`mov r, imm32`), caves where imm8 | I/C | V (listed bytes) + A (classification) | T11 |
| W2-E7-03 | generation loop 0x4bfa20..0x4bfaf8 (`add edx, 5` 0x4bfaec; bound 0x4bfaf5) | five entries per iteration, 100 | F: capacity loop over 256 (no overrun past the table) | F | V | T11 (canary) |
| W2-E7-04 | bitmap clears: pool C 0x4c042c..0x4c0434 (4 dwords); pools A / B in 0x4befa0 / 0x4bf2c0 and the resets | 4 dwords | 8 dwords (caves) | C | V (C) + A (A / B) | T11 |
| W2-E7-05 | 0x4bdc00 (vtable 0x68ec24 +0x30; caller 0x4c061e): tail 0x4bdc12 `6bc064 c20400` | `imul eax, eax, 0x64; ret 4` | 5-byte jmp + pad to a DLL `imul eax, eax, M; ret 4`; guard 0x4bdc00..0x4bdc18 | C | V | T11 |
| W2-E7-06 | 0x4c0644 `8d4864` | `lea ecx, [eax + 0x64]` endpoint | cave `lea ecx, [eax + M]` | C | V | T11 |
| W2-E7-07 | 0x4c05de `call 0x4c0460` (arg `push 0x63` at 0x4c05d9) | cap 99 | thunk: `[esp+0xc] = M-1`, preserve ECX, `jmp 0x4c0460` | C | V | T11 |
| W2-E7-08 | `save-guards.md` guards 0x4c011e / 0x4c04a8 | watch 100 / 9 | watch the raised values (no capacity change of their own) | S | - | T11 |

### 15.7 Gate rows (T)

| ID | Test | Section |
|---|---|---|
| W2-T-01..13 | T1..T13 of section 10 G2 (plus T9b) | 10 |
| W2-T-G1 | codec vectors, round trips with the explicit difference list, legacy vectors, rules test | 10 G1 |
| W2-T-G3 | real-save collection per census C3, offline runs, census table, in-game cold load / zone change / save / reload / co-op | 10 G3 |
| W2-T-G4 | boundary fixtures incl. the representability oracle | 10 G4 |
| W2-T-GP | guard-page proof for every relocated pool / object (W2-E15-13/14, E3c-01/07, E3b-01, E2-01, E14-03, E7-01) | 10, 14 |

A row is **done** when its guard bytes, its W2 bytes (or source) and its check are in the repository and the checker
has matched all three against this table. No row is open (revision 3.1 folded RE-14 / 15 / 16); rows marked LIKELY
rest on sweeps and are closed by the guard-page proof (W2-T-GP) and the union-host differentials.

### Implementation notes: steps 4/5

`item_capacity.cpp`, `item_capacity_rules.hpp` and generated `item_capacity_sites.hpp`
compose E2 / E3 / E3b / E3c with the step-3 transaction. All 469 SPEC-old item
rows and the 52 instance-pool omissions have explicit retail guards. Shared
256-bit / 375-bit helpers remain untouched; separate hash-guarded clones serve
the raised pools. Instance and attached objects have DLL-owned backing with an
inaccessible page immediately after each object, initialized by the retail
constructors through replaced singleton getters only after publication.

The aggregate item manager is 0x41480 bytes: enhancement records +0x7a00,
record bitmap +0x2fa00, definitions +0x2fc00, state pairs +0x3ec00,
world slots +0x3f400 and world bitmap +0x41400. The instance object is 0xf120
bytes and the attached object 0x1c420. LC preflight reads their object-relative
live counts from the same layouts and checks free world slots for W2.

W2's maximum is 4096 enhancement records. The existing ItemEnhancements ini
contract remains capped at 1024 until layout selection is enabled at step 8.
After a W2 transaction publishes, the legacy limits installer accepts the
published 4096 capacity and never patches that owner a second time. Startup
still calls prepare with E10A2; no SaveLayout key is read.

Additional ledger corrections found while executing the retail code:

- E2-01: constructor and destructor unwind bases at 0x673a43 / 0x673ad3
  (`83c104`, add ecx,4) must follow the relocated definition array. Merely
  widening the vector count at 0x47ddf5 leaves exception cleanup on old storage.
- E3c-07: the destructor's five unrolled bitmap stores at
  [0x47dbe7,0x47dbf5) must become a 32-dword clear, in addition to the
  constructor clear at [0x480827,0x480845).
- E2-07: the three reward sentinels are initialized at 0x4c891a / 0x4c894a /
  0x4c896f (course+0xc / +0x20 / +0x34, each reward+8). Their old bytes
  `c6400cff`, `c64020ff`, `c64034ff` become word stores of 0xffff.
  Reward+0xa is a separate existing u16 and is preserved, not repurposed.
- Width caves must not displace an incoming loop target. In particular,
  relocating the by-name base at 0x47e333 displaces the loop at 0x47e336.
  The generator promotes such edits to a whole copied owner under its region
  hash and relocates all internal branches. The definition picker also uses
  a complete copied owner; its index/result locals are u16 within the original
  frame, retaining every retail FP instruction and RNG call.
- The 1000-drop coverage requirement needs a table-size qualification: two
  random affixes per drop provide at most 2000 selections and cannot cover
  4096 distinct IDs. The executed 1000-drop fixture covers all 375 offered IDs
  (2000 calls, controlled exhaustive RNG). Separate full-capacity fixtures
  cover every index of 376- and 4096-entry tables in both picker ranges
  (8944 draws). This does not reduce W2's capacity.

The mapped-retail item fixture tests actual retail RNG state as well as a
recording RNG (16000 differential cases each, with duplicate-key chains),
32768 private-helper comparisons, 255/256/1023 boundaries, native definition
and instance stream headers, both reward consumers, pool exhaustion and named
LC preflight refusals. `xml2_save_manager_test` now commits the union of steps
3/4/5 before its 36 capture/restore/disposal/LRU/placement checks. External
engine dependencies use recording fakes; no game or emulator is launched.

The inventory slot-write bridge writes zero to payload +8 through the original
one-byte cursor operation, retaining quantity at +9..+0xa and the authoritative
u16 at +0x12. Whole serializer-owner hashes cover both call frames and their
arguments. Native registration refusal branches at 0x480438, 0x47ca4b,
0x47cb44, 0x47cd4b and 0x4804ab preserve flags/registers, report the 1024/4096
capacity, latch reconstruction failure and follow the original cleanup targets.
Their complete registrar-owner hashes include the final cleanup instruction and
ret at 0x4806e0 (owner end 0x4806e1). T9b executes every comparison/branch with
the largest valid index and count+1, and the native record accessor at 4095/4096.

Verification commands are in `scripts/README-gates.md`.


### Implementation notes: steps 6/7 (E14 / E7)

The W2-only `save_limits` contribution composes with the save/item contributions
inside `entity_capacity`; ordinary startup still requests E10A2. There is no
SaveLayout selector. ObjectiveDescriptions joins the same startup transaction
only for the explicit W2 profile seam, with exactly one allocation-site writer.
The standalone OFF installation retains its original four writes.

`generate_w2_save_limits.py` checks the retail executable SHA-1 and all 105
SPEC-old objective instructions, plus the 27 classified talent-capacity operands
and special E7 sites. It emits instruction guards, authored edits, SHA-256 owner
hashes and branch-relocation metadata. Copied executable bodies are read at
runtime under those hashes, never embedded in source. The native fixture and
`check_w2_save_limits_text.py` independently check the patched text and caves.

Additional ledger corrections from retail execution and the whole-text sweep:

- E14-03/04: retain stride 0x1ac and live byte +0x1aa. The whole DLL-owned
  mission manager is 0xd750 bytes; records remain manager+8. Pool-relative
  bitmap/count are +0xd45c/+0xd46c (four bitmap words); manager-relative live
  count is +0xd474. The byte at +0x1a9 remains the low byte. The u16 side entry
  is FFFF between allocation/release and registration. All nine sort moves are
  owned by the replacement, with invocation-local wide temporary values.
- E14-04b: the retail 0x488430 helper is field-wise, not memcpy: it copies five
  strings with terminators and preserves destination +0x1aa bit7. The sort
  uses its complete hash-guarded private clone for pool-to-temporary moves and
  raw 0x1ac copies for moves back. A wrapper also handles direct pool-to-pool
  calls. Its complete region is [0x488430,0x488514).
- E14-05: file names at 0x72b248 and file acts at 0x72b570 grow along with the
  u16 bases at 0x72b590. Their three count fields move too. The shared +0x1c
  count initializer reached through 0x67a875/0x67a895 cannot initialize the
  enlarged acts/base arrays; those two objective-owned edges get private
  descriptor-sized initializers. The name-table count was +0x324, including
  its four-byte pad; it becomes +0x804.
- E14-07/08: the abbreviated ledger omits the objective-byte copy/clear helper
  [0x5f3fb0,0x5f3fd3), the snapshot copy [0x5f4400,0x5f4497), and the cancel /
  pop / restore consumers at 0x5f2ce0 and 0x5f44a0..0x5f48f5. Regenerate their
  count, positive/negative frame strides, all six vector components, and the
  three local snapshot stack frames (including arguments and cookie slots).
  The push frame at 0x5f3630 also grows. State copies are exactly 1024 bytes;
  the retail extra 301st byte copy is removed, leaving the three pad bytes.
- E14-03: four external journal/menu readers of the pool-relative count were
  absent from the abbreviated ledger: 0x5cce8c, 0x5ccf4c, 0x5cd512 and
  0x5ce138. Regenerate all four +0x7d70 operands; the text gate now rejects
  the old objective-pool displacements across the complete executable.
- L-12: test_state's live count still used manager+0x7d78 after its initial
  view conversion. `objectives_view.count_offset` now supplies that offset,
  as well as the already-view-based index/state readers.
- E7-01: allocation 0x4c083c is a second 0x3228 allocation, in addition to
  0x4c0584. Both now request the complete 0x7a8c DLL registry. The native
  constructor runs on it; its singleton free edge 0x4c0907 retains DLL-owned
  backing. Pools A/B/C are at +0x370/+0x35c8/+0x6234. A's u16 index table is
  +0x33c4; C's u32 index table is +0x7688. B mask/shift are FF/8.
- E7-01/02: the talent-owned calls 0x4befbf, 0x4bfba2 and 0x4bfc32 reach
  shared 100-entry ring helpers 0x460310/0x460250. Redirect these edges to
  private 256-entry helpers; preserve the shared functions for other owners.
- RE-11 census: there are 30 immediate 99/100 instructions in
  [0x4bd000,0x4c1000): 27 capacity/last-index operands, M at 0x4bdc12, the
  shared push at 0x4c05d9, and unrelated stack allocation 0x4c07c5. The latter
  stays unchanged; the cap push stays 99 because the thunk substitutes 199
  at [esp+0xc]. The LEA endpoint at 0x4c0644 is a displacement, not an immediate.
- E7-03: the complete generation-loop replacement includes its backedge,
  [0x4bfa20,0x4bfafe), rather than ending at the start of the Jcc 0x4bfaf8.
- E7 position rule: position < M means a hero file may contain exactly M
  dense entries (last position M-1). Shared positions stop at M-2. A public
  build-table validator checks every position before play under W2, including
  talents absent from a save; the converter retains its per-value checks.
  The builder key/V44 integration belongs to the external builder and step 8;
  no key is made selectable here.


Step 6/7 verification on the retail executable: 52 objective/talent checks,
2500 retail/DLL sort differentials, 36 combined-union checks, 170 native
invocations with zero failures/skips, and 77 full-byte parity vectors. The new
text gate verifies 138 reviewed ledger guards, 48 owner hashes, 39 caves and
899323 decoded instructions; the existing save text gate also passes. OFF
matches the b1e26ac baseline on all 50 write pages (204800 bytes), with 133
matched relocated targets, one identical generated allocation, and zero other
byte differences; agreement and registry exports are identical. Content guard:
627 indexed files plus all 25 changed/new files clean. Synthetic guard self-test
30/30 (temporary Git plumbing excluded under the task's no-Git-write rule).
No game or emulator was launched. Live G3/G4 and step8 remain separate gates.


### Step 8: DLL selection, admission and builder contract

The DLL now reads `[Limits] SaveLayout=w2` through `native_save_layout` before
preparing the capacity union. The absent-key path still calls the original
E10A2/retail selector. A non-LC save mode ignores SaveLayout; unknown explicit
values under LC, missing EntityCapacity, and conflicting ItemEnhancements are
refused before any startup-union write. Failed requested W2 preparation adds a
transaction veto; it cannot silently commit the LC seam on another layout.
A failed transaction registers rule47 unverified, leaves the affected image
stock, and refuses DLL initialization. The pre-existing socket exclusion may
already be installed before this union; the fault gate measures image bytes
immediately before preparation and after the union, independently of that hook.

The save seam's public `installed_profile()` exposes its committed profile,
not the profile staged to generate its bridges. Rule47 is revision1 with
structural revision1 and effective values:
`{0x00010802,0x10000400,0x04000400,0x00c80400}`. Rule34 ItemEnhancements is
also registered as `{4096,0,0,0}` under W2, even when that key is implicit.
E10A2 and W2 retain the same rule1 actor values but refuse one another via47.
This DLL names47 NativeSaveLayout in either direction; the preserved pre-step8
DLL calls the unknown rule `a rule this PC does not have`.

Builder output required for W2 (the external builder is maintained separately):

```ini
[Save]
Format=lc
[Limits]
EntityCapacity=1
SaveLayout=w2
ItemEnhancements=4096
```

ItemEnhancements may be omitted because W2 owns4096; if present it must be4096.
Write canonical values. Preserve the coordinator-assigned existing online
GameVersion/release token; this step assigns no release identity. The DLL checks
these prerequisites, supported retail bytes/whole-owner hashes, startup owner
quiescence, every composed write and page protection/readback/rollback, and the
committed layout descriptor before publishing W2/admission. The builder must
still validate its data:1024 definitions/instances/world slots/objectives,
4096 enhancement/attached records,127 loaded objectives per act,64 mission files,
256 registrations per talent pool, hero position<200/shared position<199,
(saved talent id<=65535), and recursive zone placement count<=1024. Talent-file
slots remain9; M=200 is the descriptor's constant, not an independent INI key.
There is no separate switch for one W2 engine raise. Feature bit1 remains the
representability probe's decision, never a builder flag inferred from SaveLayout.

The test pipe's ordinary `status` includes the W2 status segment. `save layout`
returns exactly the standalone line (without an `ok` prefix):

```text
layout W2; defs n/1024; enh n/4096; att n/4096; inst n/1024; world n/1024; obj n/1024; talents n/256; zones n/8
```

Counters are live definitions, total prefix+suffix enhancement records,
attached/instance occupancy, occupied world slots, globally registered objective
definitions, maximum occupancy across the three talent pools, and resident zone
records. Unconstructed owners report0. The new guarded observer at0x48864b
preserves the global registrar's EAX/flags/ret8 and records its cumulative return
atomically; this avoids confusing the per-act loaded count (<=127) with the global
objective ceiling1024. With W2 off, `save layout` returns `layout RETAIL` or
`layout E10A2`; ordinary status/engine bytes/agreement stay at the configured
baseline.

`check_w2_startup.py` runs complete production/fault DLL initializations on
private desktops in the owned union host. It checks configurations, explicit/
implicit4096, ObjectiveDescriptions composition, guard mutation, backing failure,
an actually RX page whose protection change is refused, rule47 values/digest,
nonzero status including the real pipe handler, and current/pre-step8 compiled
refusal wording. Fault-only snapshots compare the entire7MiB owned image before
preparation and after refusal; faults are excluded from production dinput.dll.
No game or emulator is launched. The external builder integration and live G3/G4
validation remain separate work.


Step8 final gates:20 startup/T12/T13/evidence-control cases,70 legacy startup
cases,170 native invocations,77 parity vectors,52 objective/talent checks,
36 full-union checks and47 item checks, all with zero failures. The new
objective text gate checks138 reviewed guards,49 owner hashes,40 caves and
899333 instructions; save and item text gates pass too. Absent-key OFF proofs
compare the native, RETAIL-LC and E10A2-LC configurations against68cd9c0 on
50 pages/204800 bytes each:zero other byte differences,identical agreement and
registry exports. Each startup run uses a fresh case directory so stale
snapshots/markers cannot certify a rerun; a missing-evidence negative control
proves that check. A fresh read-only review found no production defects; its
stale-evidence observation was reproduced and fixed. No live-game gate ran.
