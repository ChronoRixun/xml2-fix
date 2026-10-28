# Script pools

`[Limits] ScriptPools=x1` restores headroom for converted scripts and future mods:

| Pool | Retail | x1 | Record size |
|---|---:|---:|---:|
| Statements | 620 | 4,096 | 0x38 |
| Values, including strings | 1,556 | 8,192 | 12 |
| Compiled scripts | 120 | 512 | 0x230 |
| Running contexts | 30 | 128 | 0x5c4 |

The owner at `[0x787740]` grows from 0x3a284 to 0x10bb74 bytes. All four raised
pools live in an appended tail of that same game-heap allocation. The function
tree, zone/game globals, save-image layout, record strides and native lifetimes
stay unchanged. `script_pools::descriptor()` publishes base offsets, live-count
offsets and capacities for diagnostics; consumers must use it instead of retail
pool offsets. The old embedded pool storage remains unused.

The retail code still performs allocation, release, ring wrap and teardown. The
300-site generated ledger covers their displacements and bounds, bitmap clears
and eight exception-unwind references at 0x674803..0x6748b9. Short compares use
DLL caves; both bitmaps are fully cleared at the raised word counts during
construction, reset and teardown. No incoming branch enters a cave interior.

The compiler checks ECX before the virtual call at 0x4d89f1. A null literal takes
the existing failure epilogue at 0x4d8aa7, so its line is refused without a null
dereference. Statement exhaustion retains the retail behavior: each attempted
line is refused while the pool stays full, including control directives. Each
refusal is counted; logging is limited to the first of each kind per compiled
script. Compiled-script/context exhaustion retains the native NULL result.
This row does not claim to fix every caller of those other NULL results.

The install requires an unconstructed owner and matching retail guards. One
`patch_transaction` owns every write. Cave code is prepared and made executable
before the transaction; publication follows its verified commit. Certified
failure releases the caves; an uncertified rollback retains them and refuses
startup. ForcedTeams checks the published value-maker bytes, including the
relocated operands and refusal caves. ScriptFunctions' lookup/tree guards and
redirects are untouched.

The key requests the shared agreement before the startup transaction, including
in plain XML2 with no LC or other participating keys. The pool transaction runs
after ScriptFunctions and before ForcedTeams consumes its published guards.

The implementation also closes an omission in the original design: the compiled
objects have a separate **120-name filename cache** at owner+0x26f70. Its node
allocator 0x4d57b0 has no full check; the 121st name would reuse live node zero
after enlarging the compiled-object pool. Guarded adapters at 0x4d6980,
0x4d71f0 and 0x4d94d0 use bounded 512-entry DLL metadata (about 130 KiB), leaving
the native cache's offset and every save-adjacent displacement untouched. Keys
retain native 255-byte truncation and compare through the game's `_stricmp`
thunk. Duplicate names keep the first object. Unload retains native statement
release and compiled-slot ownership; the observer at 0x4d7fc8 clears metadata at
the native reset phase, after the original globals-clear call. The four raised
object pools remain in the same appended game allocation.

Absent, `620`, `0` or `stock` writes no game bytes and adds no agreement rule.
Other values request an installation; only `x1` is accepted. A successful
installation registers rule **59 ScriptPools**, revision 1, `{4096,0,0,0}`.
The first word identifies this fixed profile's effective statement capacity;
the four-capacity payload in the earlier design is superseded by the assigned
brief. A failed request registers unverified `{0,0,0,0}`, closing online play.

The existing Present hook samples guarded counters on the game/render thread,
including with the input pipe off. It adds no new game-code hook. If no existing
Present hook is active there are no frame samples. Zone changes reset zone peaks
and produce a log summary; session peaks remain. The input pipe's `script_pools`
command returns capacities, live counts, zone/session peaks and refusals, plus
`null_values`. `refusal_observers` says whether W3-8's observers are installed:
OFF reports sampled occupancy only. The separate diagnostics row D can consume
the descriptor for stock refusal observers; its observers must compose with
these allocator caves instead of overwriting them.
`cached_scripts` separately reports the filename-cache capacity/live/peak/refusal
counts, preventing the 120-name metadata ceiling from being hidden by the larger
compiled-object capacity.

Verification (no game launch):

```text
python tools/generate_script_pool_sites.py <owned-XMen2.exe> --check --verify
python scripts/check_script_pool_guards.py
cmake --build build --config Release --target xml2_script_pools_test
xml2_script_pools_test.exe <owned-XMen2.exe>
```

The native fixture executes retail constructor segments, allocators, free paths
and teardown at stock and x1. Inert invented record constructors/copies replace
engine services. It checks N/N+1 refusal, recycle, bitmap word boundaries,
canaries, preserved owner bytes, every mutated ledger byte, ScriptFunctions
composition, published value-maker guards, telemetry and agreement mismatch.
The real line parser is also called while the statement pool is full: ordinary
lines, `else` and `endif` all refuse without changing compiler bookkeeping.
The cache fixture checks the real retail comparison/truncation/duplicate rules,
proves the old 121st-pop alias, resolves all 512 names through production
adapters, refuses name 513, unloads/recycles a compiled slot, and checks reset
invalidation and untouched native cache bytes.
Separate `invalid`, `owner`, `guard` and `functions` modes exercise installer
paths. `mutate-guard` restores the unchecked dereference and must fail with exit
42. Actual zone demand, nested-script peaks and save/load gameplay still require
the coordinator's live rounds; no game or xemu is needed for the fixture.
