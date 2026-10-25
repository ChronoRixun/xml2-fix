# Local gate scripts

These scripts run the native, startup and OFF-proof gates on a developer PC. They never launch the game. Machine-specific
inputs come from environment variables; the scripts stop with a message when a required value is missing (the proof scripts use Python environment lookup):

| Variable | Used by | What it points at |
|---|---|---|
| `XML2_FIX_TEST_EXE` | run_native, prove_off_wire, prove_combined_install | a retail XML2 PC `XMen2.exe`, read only |
| `XML2_FIX_TEST_GAME_DIR` | run_native | an XML2 PC install folder, read only |
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
