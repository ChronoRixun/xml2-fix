# One-pass limits, wave 2: the save-bound raises (design only)

Status: design, 2026-10-25 (limits wave 1 agent). No code. Wave 1 (branch `limits/wave1`) shipped the runtime-only
raises (IGB cache + names + model handles, the LAA log line). This page is the implementation design for the rows of
gap-audit PLAN 2.5 whose capacity is written into a save, so that they land in **one** save-layout change (Owen's
one-pass rule) with the agreed targets of decision 7 (limits-check REPORT). Every address is retail XMen2.exe
(SHA-1 `7d95cdb4...`); "SPEC-old-save" is agent-work/save-step0/SPEC-old-save.md revision 2.

## 1. The split (why these rows are save-bound)

| Row | Target | What is saved | Evidence |
| --- | --- | --- | --- |
| E2 item definitions | 160 -> 1024 | each item instance's definition as a **one-byte index** in the native stream (`OldInstanceHead`, 5 bytes) and three single-byte sites | SPEC-old-save.md:501 (164 sites); limits-check E2 (Danger Room reward byte, by-name lookup byte, global 0x6d83bc) |
| E3 enhancement records | 512 -> 4096 | instances store their own and random affixes as **u16 record numbers** (0x481610 / 0x482b40) | limits.hpp (ItemEnhancements); 0x47ad22 tests 0xff as "none"; `lcsave/convert.cpp` `item_pools {375, 512}` |
| E3b attached enhancements | 375 -> 4096 | the native load rebuilds every instance's attached-enhancement list into a 375-entry pool; the converter refuses above it | `lcsave/api_engine.cpp:453`, `lcsave/reverse.cpp:909` (`attached_enhancements > 375`) |
| E3c item instances / world items | 256 / 150 -> 1024 / 1024 | zone records carry a **one-byte world-item slot**; instances counted the same way | `api_engine.cpp:453` (`instances > 256`, `world_items > 150`) |
| E7 talents | pool 100 -> 256; shared-ID range | hero talent IDs are `(stats index + 1) * 100 + position`; shared IDs 0..98 (`TALENT_CAP = 99`, 0x4c05d0) - **the IDs are saved** | limits-check E7 (63 of 99 used) |
| E9 zone memory | 5 -> 8 | the native save manager holds exactly 5 zone records (`native_manager_records = 5`, `lcsave/layout.hpp`) | D13 (XML1 keeps 7, no eviction) |
| E14 objectives | 299 / 255 -> 1024 / 127 per act / 64 files (R4) | the objective index is **one byte** in the native stream | limits-check "Objectives" row |
| E15 zone record caps | R6 values, float positions | state records keep **16-bit positions** (`OldStateRecord`, 0x18 bytes) | limits-check "Zone record caps"; E10A2 already raised the counts (4096 states, 1024 ordinals / dynamic / smart, `entity-capacity.md`) |
| E8 character definitions | unchanged | the 21-entry herostat contract | no raise (owner decision 7); stated so the pass is complete |

The LC container itself does not change layout for any of them: it is keyed by catalogue ID and its readers' ceilings
are far above the targets (limits-check "Save format"). What changes is the **native layout** the DLL builds from the
LC file and reads back (the converter's target) and, for E9, where records 6..8 live.

## 2. One layout: native layout W2

Extend `lcsave::NativeLayout` (layout.hpp) the way Phase A added `E10A2`: a third descriptor `W2` = E10A2 plus

- `item_index_bytes = 2` (instance head 6 bytes instead of 5), `item_definitions = 1024`;
- `enhancement_records = 4096`, `attached_enhancements = 4096`, `instances = 1024`, `world_items = 1024`,
  `world_slot_bytes = 2`;
- `objectives = 1024`, `objectives_per_act = 127`, `objective_files = 64`, `objective_index_bytes = 2`;
- `state_position = f32` (state record grows from 0x18 by the three position words: 0x1e, padded to 0x20);
- `zone_records_native = 5` unchanged (see E9), `zone_records_lc = 8`;
- `talent_multiplier` (see E7).

`make_native_layout` computes offsets from these the same way; the payload (0x200000) must still hold the largest
zone with the new state size: 4096 x 0x20 x 5 records = 0x280000 > 0x200000, so either the payload grows to
0x300000 (wrapper +0x78) or the state count per record stays 4096 with the records counted, not reserved. **Decision
needed in review:** grow the payload (simple; the manager allocation at 0x487344 is already a claimed DLL backing in
Phase A, so its size is ours).

The converter (`old_stream_to_new`, `reverse.cpp`, `oldsave.cpp`, `convert.cpp`) takes the layout explicitly, as it
does for E10A2 (never inferred from a size). All demand checks (`api_engine.cpp:453`, `reverse.cpp:909`) read the
capacities from the descriptor instead of literals.

## 3. Per-row implementation

**E2 item definitions (L).** Engine: widen the definition index at the 164 sites of SPEC-old-save.md:501 (byte loads
`movzx r, byte [..]` -> word loads, byte stores -> word stores, `cmp r8, imm8` bounds -> imm32 caves as ModelPools
does), the Danger Room reward byte, the by-name lookup byte and the global at 0x6d83bc; raise the definition table
(160 -> 1024) with the item manager's existing relocation (`item_layout_for` already moves the enhancement pool to the
manager's end: the definitions move the same way). Native stream: `OldInstanceHead` gets a u16 definition. Converter:
read/write per layout; the LC file stores definitions by catalogue ID (unchanged). Tests: a whole-.text sweep in the
style of `check_model_pools_text.py` for every reader of the definition index field (offset in the instance and in
the definition records), plus a native round trip with an index > 255.

**E3 enhancement records (M).** Raise `max_item_enhancements` 1024 -> 4096 (layout already generic); fix 0x47ad22's
0xff "none" test (the record number is u16: the sentinel becomes 0xffff); the two random affix pickers (0x47e690,
0x47ee90) keep 375-int stack arrays: replace each picker's frame with a DLL function that uses a heap array of the
pool's size (or bound the prefix/suffix counts at 375 and keep the warning - **review decision**; the 4096 target and
the full XML1 table need the first). `item_pools {375, 512}` in convert.cpp becomes the descriptor's value.
Record numbers are append-only (saves store them), so old saves keep meaning.

**E3b attached enhancements (M).** Locate the attached-enhancement pool (the second 375-slot pool, "bonuses silently
lost on load", save-format decisions 3) with its sites (RE task: the load path behind `native_load_demand`), relocate
it as the enhancement pool is (record + bitmap at the end of the manager), 4096. Converter preflight uses the
descriptor. Lands with E3 because the preflight blocks loading.

**E3c item instances / world items (M).** Raise the instance pool 256 -> 1024 and the world-item slots 150 -> 1024;
the zone record's world slot becomes u16 in W2. Shares the zone-record redesign with E15 and E9.

**E7 talents (M).** Pool 100 -> 256 (runtime, registered talents). Shared-ID range: widen the hero ID multiplier from
100 to 200 (`(stats index + 1) * 200 + position`, 21 heroes -> max 4400 + 200, inside 16 bits), shared IDs 0..198.
Sized by the G11 team-bonus and boss-immunity census (to run before the build). The LC file is keyed by catalogue ID
(limits-check: the widening "changes the converter and online only"): the converter maps catalogue IDs to
`k*200+p` for W2 instead of `k*100+p`; verify in review that no LC section stores a raw native talent ID (if one
does, it gets a minor version and is re-based on read). Online: the talent ID formula is part of the agreement (a new rule
value or the save-layout rule below).

**E9 zone memory (M).** Keep the native manager at 5 records (no engine change); the LC container keeps up to 8
zone records (the ZONE section already counts records). On a zone change the DLL swaps the least recently used of
the 8 out of the native manager and the requested one in (all inside the LC seam: the native code still sees 5).
Eviction order and the "update that changed a zone forgets it once" rule (D2) unchanged.

**E14 objectives (M).** Widen the objective index (one byte, 255 addressable) to u16 at its sites (RE: the objective
manager's loader and the journal readers; `objective_text.cpp` already hooks the journal renderers), raise the table
to 1024 / 127 per act / 64 files (R4). Native stream: u16 index in W2.

**E15 zone record caps (M).** E10A2 already gives 4096 states and 1024 ordinals / dynamic / smart per zone record. W2
adds float positions (the state record's three 16-bit coordinates -> f32) and the R6 counts where they exceed E10A2.
Native sites: the state record writer/reader (the two serializer functions behind the 0x18-byte records) and every
reader of a state's position.

## 4. The "real saves load" gate

Before any W2 build is offered: (1) the 82-save corpus (round5 SHA-1s) and every real save Owen and the tester have
(0.2.x / 0.3.x / 0.4.x `.lcsave`, collected read-only) convert LC -> W2 native -> LC with the LC bytes identical except
the writer version (`lcsave_durable_parity` gets a W2 column); (2) the legacy test vectors (`XML2_FIX_LEGACY_VECTORS_DIR`)
pass on all three layouts; (3) synthetic saves crossing each old cap (item index 256 and 1023, enhancement 4095,
attached 376 and 4096, instance 257, world item 151 and 1024, objective 256, zone record 6..8, talent ID in the new
range, a position past +/-32767) round-trip; (4) live: each real save loads in game and plays one zone change (the
wave-2 live runs, as `limits-wave1-live-runs.md` does for wave 1).

## 5. Older builds refuse newer saves, by name

`required_features` bit 1 = `feature_native_w2`, set by the writer **only when the save uses a capacity beyond the
pre-W2 layout** (any item definition index > 159, enhancement record > 511, attached > 375, instance > 256, world
item > 150, objective index > 254, a 6th..8th zone record, a re-based talent ID, a position outside int16). A save
within the old caps stays loadable by 0.4.x builds (D10: refused only when the extra data is not droppable). A
released build (any 1.8.x) refuses a bit-1 file already: container.cpp:602 "required feature bits 0x00000002 not
implemented (needs a newer version)", never stripping the marker. W2 builds add the name: the save menu says "This
save needs Legends Classic 0.4.2 (XML2 Fix 1.9) or newer" and the log names the first capacity that set the bit.
The LC file also keeps `.lcsave.newer` handling (limits-check "Save format") for files from a newer format major.

## 6. Order and routing

S4 recomp facts -> census (items, enhancements, attached, objectives, talents, zone records) -> this design reviewed by
a second model (Astra or Sol, before code; the one-pass rule) -> E2 + E14 RE sweeps (Astra) -> implement W2 descriptor
+ converter (Sol) -> engine sites per row with whole-.text gates (Sol / Astra) -> real-saves gate -> one build.

## 7. Runtime rows not in wave 1 (and why)

They change no save byte, so they are not part of the W2 layout, but they belong to the same pass:

| Row | State | Next step |
| --- | --- | --- |
| E4 live actors 32 -> 96 | Not implemented. Entity-capacity Phase B (`agent-work/entity-capacity-impl/PHASE-B-BACKLOG.md`): the 0x848-stride actor arena and its actor-only imm8 bounds, the primary CAI controllers (stride 0x61c, six constructors, allocation 0x522a10 has no full check), the CAITeam 4 x 8 = 32 membership table and the pattern-manager aliases. PLAN E4 requires the AI-team membership policy as a reviewed design **before code** (and an owner decision on Fable). | Opus design of the CAITeam policy + Phase B ledger, second-model review, then implement on top of Phase A's transaction with online layout rule 1 revision 4 `{96,768,1024,10}` (actor + generic <= world still holds). |
| E5 generic entities / world handles | Already 768 / 1024 (Phase A). | Census only; raise with E4 if a zone has < 2x headroom. |
| E10 emitter objects (40) | ModelPools x4 keeps 40 emitter objects (indexed by emitter). | Census the 5 heaviest fights; raise only past 50 %. |
| E11 curves, script functions, move triggers, styles | Already keys (3600 / 1024 / 64 / 32 in the builder). | Census only. |
| E12 LAA | xml2-fix logs `LAA on/off` and the highest reservable address (wave 1). The flag itself is set by the builder on its own exe copy (one word at file offset 0x146, 0x010f -> 0x012f). | Builder change + `test_builder_cli.py:193` / whole-file hash updates; controlled high-address test incl. Proton. |
