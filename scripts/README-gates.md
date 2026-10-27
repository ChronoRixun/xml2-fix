# Local gate scripts

XML1 economy foundation adds `xml2_economy_rules_test` and the mapped-retail
`xml2_economy_test` modes run by `run_native.py`. The read-only constructor
census is `python scripts/check_xml1_economy_text.py <retail exe>`. The existing
`prove_w2_step3_off.py` comparison accepts `XML2_FIX_OFF_CASE=w2` or `w2zero`
to compare an exact baseline DLL with this DLL while W2 remains enabled and
Xml1Economy is absent or explicitly zero; include pages `47b000` and `480000`
in its page list. Both configurations must retain the baseline agreement and
every watched byte after named relocation normalization. See
`docs/xml1-economy.md` for the rule and constructor contract.

`python scripts/check_xml1_economy_startup.py <union_host.exe> <DLL directory>
<retail.exe> <engine directory> <output directory>` checks the actual DLL's
missing-W2, missing-sidecar and valid-sidecar-before-table cases. It requires
the DLL's `/MAP` file, checks both hook sites and unavailable admission, keeps
Discord off, and verifies that the registry stayed unchanged. In particular,
economy preparation must run before `entity_capacity::contribute()` consumes
the W2 prepared-state marker.

With `UNION_CODE_OUT`, the host also dumps W2's guarded RW backings. The OFF
comparison pairs them by referring instruction and compares every initial
byte before accepting a relocated data operand; indexed absolute operands
and conditional refusal branches are included. This allows the W2-enabled
comparison to cover the full supplied page manifest as well as the economy
pages.

The host's `UNION_IMAGE_OUT` pre-patch capture supplies all four bytes of a
touched import slot before sparse write-log bytes are applied. This handles a
callback byte that already equalled the resolved system import byte; untouched
imports retain the raw retail bytes to avoid unrelated loader relocation.
Named callback/addend checks still apply. The synthetic regression is
`python -B scripts/test_prove_w2_step3_off.py`.

`xml2_economy_rules_test --builder <build-folder>` additionally checks a final
loot builder output through the C++ sidecar and catalogue/bind readers: 131
rows (including inert flags-zero rows), absent pack costs, equipment vendors,
the noaward helmet, PC definition order, and the catalogue's 19 ITEM attribute
mask-0x1 markers against sidecar rebuilt mask 0x4. This mode only reads the build.

`xml2_economy_test <retail-exe> shop` covers Forge reconcile/catalog/pool budgets,
Healer synthetic rows, two-tab navigation/setup, Training refusal before debit
and at all three service calls, rare style, stash preservation and rule-52 parts
4/16 mismatch refusal. `run_native.py` includes this mode, 25 guard mutations,
preparation refusal and full-image rollback. The read-only static shop gate is
`python scripts/check_xml1_economy_shop_text.py <retail-exe>`. The production
startup script also checks all 14 shop hook states.
These scripts run the native, startup and OFF-proof gates on a developer PC (`run_native.py --list` prints its command matrix without running anything). They never launch the game. Machine-specific
inputs come from environment variables; the scripts stop with a message when a required value is missing (the proof scripts use Python environment lookup):

| Variable | Used by | What it points at |
|---|---|---|
| `XML2_FIX_TEST_EXE` | run_native, prove_off_wire, prove_combined_install | a retail XML2 PC `XMen2.exe`, read only |
| `XML2_FIX_TEST_XBE` | run_native shared handler and NPC differential tests | the owner's XML1 `default.xbe`, read only |
| `XML2_FIX_TEST_BIN_DIR` | run_native | optional native binary directory; defaults to `native/bin/Release` |
| `XML2_FIX_TEST_GAME_DIR` | run_native | an XML2 PC install folder, read only |
| `XML2_FIX_TEST_XBE` | run_native | the owner's original-Xbox X-Men Legends `default.xbe`, read only (the weapon brain and AI query scheduler suites execute XML1's routines from it) |
| `XML2_FIX_LEGACY_VECTORS_DIR` | run_native | the legacy save test vectors folder |
| `XML2_FIX_UNION` | prove_off_wire, prove_combined_install | the folder prepared by `tools/union_host/prepare_union.py` |
| `XML2_FIX_UNION_HOST` | prove_off_wire, prove_combined_install | optional path to `union_host.exe`; defaults to `native-map/union/Release/union_host.exe` in this repo |
| `XML2_FIX_OFFWIRE_REF` | prove_off_wire, prove_combined_install | the reference DLL build the OFF records are compared with |
| `XML2_FIX_STARTUP_RUNNER_DIR` | run_startup | the isolated-desktop startup runner folder |

The synthetic harness is committed under `tools/union_host` and built by the CMake
`xml2_union_host` target with the Win32 MASM toolchain. Prepare its folder with
`python tools/union_host/prepare_union.py <union folder> <game build folder> <base xml2-fix.ini>`;
the base ini is the 0.2.3f reference configuration. The helper module docstring describes the inputs. It lays out a
retail image for DLL installation proofs and does not start a game session. The proofs
load the production DLL and temporarily export/restore the user's game registry key;
run them only in the owner's gate environment. Release preparation does not execute them.

`zone_records_gate.py --tool <bin>/lcsave_zone_records.exe --list SAVES.tsv --baseline STABLE --candidate CANDIDATE
--out DIR [--require-zone ZONE]` is the per-zone save-compatibility release gate (no game): it loads copies of real
saves (LC campaigns; retail slots are imported first) on the stable build's and the candidate's tables, both native
layouts, and fails when any zone applies fewer saved records on the candidate (per zone and per save), when a
record the candidate applies is gone after its resave, or when a save that loads on stable does not load on the
candidate. Run it after the candidate's zone-prefix ledger is generated (legends-classic `tools/lcsave/README.md`);
it writes `zone-gate.md` / `.tsv` / `.json`. `lcsave_zone_records` is the CMake target; `run_native.py` skips it.

`check_model_pools_text.py <XMen2.exe>` (needs capstone and pefile) decodes the whole retail `.text` for `[Engine] ModelPools`:
every reference to the moved model aggregate / particle-system manager must be a site row, nothing may branch or point
into a span the plan overwrites, and a findNext helper shared outside the pool destructors must be copied, not raised.

`check_igb_cache_text.py <XMen2.exe>` (capstone, pefile) is the same kind of gate for `[Limits] IGBCache`: every
reference to the moved `CIGBInfoCache2` is a reference row, every pool field operand of the cache code is a site row,
no data pointer into the object or at a pool method, the class vtable stored only by the cache, and the shared
`bitset<200>::findNext` keeps exactly its two outside callers.

`prove_limits_wave1.py` runs the reference DLL (`XML2_FIX_LIMITS_REF`: the previous line's folder with dinput.dll and
dinput.map) and this tree's /MAP DLL (`XML2_FIX_LIMITS_DLL`, default `native-map/bin/Release`) in the union host with
the run023 ini: OFF and `ModelPools=x4` alone must give the reference's agreement record and limits log lines; with
`IGBCache=1024`, `ResourceNames=2048` and `ModelHandles=8192` the three raises must install and rule 31 must register.
It exports and restores the game's registry key around each run.

W2 step 3 has an additional mapped-retail union test and text gate. It keeps W2 unselectable:

```powershell
native/bin/Release/xml2_save_manager_test.exe $env:XML2_FIX_TEST_EXE build/w2
python scripts/check_w2_save_text.py $env:XML2_FIX_TEST_EXE build/w2-text.bin build/w2-gc.bin
```

For the OFF proof, build the pre-change checkout and this checkout with `/MAP`, then run
`scripts/prove_w2_step3_off.py` with their DLL directories, the union host, retail exe,
engine DLL directory, Data input directory, a workspace output directory and `build/w2-pages.txt`.
It compares every byte of the W2 write pages and the agreement record. DLL symbols and ASLR
addresses are normalized explicitly; referenced generated-code allocations must match too.
The registry export must succeed and its before/after bytes must match. All copies and dumps
stay under the output directory; no game starts.


W2 steps 4/5 add the item-capacity fixture and a separate whole-text gate:

```powershell
native/bin/Release/xml2_item_capacity_test.exe $env:XML2_FIX_TEST_EXE build/items
python scripts/check_w2_items_text.py $env:XML2_FIX_TEST_EXE build/items
native/bin/Release/xml2_save_manager_test.exe $env:XML2_FIX_TEST_EXE build/w2-items
python scripts/check_w2_save_text.py $env:XML2_FIX_TEST_EXE build/w2-items-text.bin build/w2-items-gc.bin
```

The item fixture emits every copied instruction owner before installing its
external dependency fakes. Its gate checks the 469 reviewed item rows plus all
52 omissions, every complete-owner hash, and the entire patched text and copied
owners for old class-owned operands. The generator can be rerun with
`python scripts/generate_w2_items.py <retail exe> <SPEC-old sites.json>`; the
reviewed `funcs.txt` must sit beside that research ledger. The retail SHA-1 is
required. No extracted function bodies are emitted into source.

Use `build/w2-items-pages.txt` as the page list for the same OFF proof described
above. W2 remains unselectable; ordinary startup still selects E10A2. Full
capacity affix coverage uses exhaustive draws separately from the 1000-drop,
375-offered-ID fixture (see the implementation notes in limits-wave2.md).


W2 steps 6/7 add the objective/talent execution fixture and text gate:

```powershell
native/bin/Release/xml2_save_limits_test.exe $env:XML2_FIX_TEST_EXE build/save-limits
python scripts/check_w2_save_limits_text.py $env:XML2_FIX_TEST_EXE build/save-limits
native/bin/Release/xml2_save_manager_test.exe $env:XML2_FIX_TEST_EXE build/w2-complete
```

The save-manager test now commits steps 3 through 7 together. The new fixture
compares 2500 retail sort permutations, executes high objective indices and
snapshot/side-stack copies, all 256 talent slots, generation rollover and the
M/cap/endpoint stubs. Its dump precedes dependency fakes. Recreate the instruction
plan with `generate_w2_save_limits.py <retail exe> <SPEC-old sites.json>` (the
reviewed funcs.txt must sit beside the ledger). No game is launched.

Use the union of `build/save-limits-pages.txt` and `build/w2-complete-pages.txt`
for the OFF proof, with `build/step67-baseline` as the pre-change DLL/map folder.
W2 remains unselectable; the builder key and rule47 are step8.


The durable-parity test writes `actual.*` next to each vector. Point
XML2_FIX_LEGACY_VECTORS_DIR at a writable copy inside the workspace when the
source vector folder is read-only; this task used build/native-vectors67.


Step8 startup/admission gate (Win32 Release, no game launch): configure
`XML2_SAVE_STARTUP_FAULT_TEST=ON` for the opt-in fault DLL, build ALL_BUILD below
normal priority with `/m:1 /nr:false`, then run:

```powershell
python scripts/check_w2_startup.py native/bin/Release build/step8-startup
```

The script expects the owned engine/Data copies produced by the prior OFF proof
under build/off-proof67-final and the pre-change DLL/map under
build/step8-baseline. All run folders are workspace-owned, all host desktops are
private, and processes exit after collecting evidence. Optional final CASE runs
just that configuration. The production DLL contains no fault hooks; the fault
DLL snapshots the complete owned image before/after the union for T12. Rule47
peer checks call the current and baseline DLLs' compiled description routines.
`save layout` is the standalone pipe telemetry command; ordinary `status` also
includes its exact W2 segment. Builder output/validation contract is documented
in limits-wave2.md. Live-game gates are not executed by this task.


Every step8 case uses a fresh UUID directory under OUTPUT_DIR; results.json
names the case artifacts. Unknown CASE arguments fail. The default20-case run
includes a negative control that must reject missing current snapshots.
For all three absent-key OFF configurations, run the existing proof with
XML2_FIX_OFF_CASE=e10a2 (default), retail, or native. Use build/step8-pages.txt;
the reference is build/step8-baseline. The proof's union hosts use private
desktops and compare symbols/relocations plus complete generated allocations.

XML1 economy drops (step3b, no game process): build `xml2_roll_rules_test`,
`xml2_roll_test`, `xml2_dr_reward_test`, `xml2_economy_test` and
`xml2_x1_leader_test`. The first is the 100,000-roll XBE oracle differential;
the mapped-retail fixtures receive the owned executable as their first argument.
Run the leader fixture both without arguments and with `economy` to cover both
installer modes. `scripts/run_native.py` includes these cases and requires the
usual EXE/XBE/game-directory/writable-vector environment variables above.
Run `python -B scripts/check_xml1_roll_text.py <owned-retail-exe>` for guards,
static caller census and unchanged placement/DR branch contracts.
The production DLL startup check and full W2 OFF proof remain applicable.
The latter must compare against the committed 3a DLL/map, include all roller
pages, and run absent-key and explicit-zero cases. Outputs belong in a local
work folder; never add binary fixtures, source game assets or listings to git.
