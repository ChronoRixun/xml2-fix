# Actor-scaled pools (E4 stage 1)

`[Limits] ActorPools = 124` raises CPath, shadow and delayed-damage pools from 32 to
124. The key is independent of EntityCapacity. Actor/controller counts remain 32,
CAI retains four teams, and the save layout is unchanged. Leave the key absent for
the released behavior. Other values are refused, leaving the pools stock and
online agreement unverified.

The stage installs five null-team guards in `0x50dee0`. A missing team follows the
native activation/no-team/cleanup continuation. It also clears NpcPowerRule's four
slot tables when the retail controller FIFO pops a slot, before brain construction.

Every operand has its original-instruction guard. Owner-function SHA-256 guards
cover caved/replaced code; one `patch_transaction` publishes the complete stage.
All backing and executable caves are prepared before any retail write. A certified
failure restores/leaves the original bytes and closes online admission. An
uncertifiable rollback retains referenced storage and refuses process startup.
Installed delayed-damage storage lives until process exit. Its old static range and
the adjacent object at `0x753f64` are not repurposed.

ActorAI agreement rule **44**, revision 1, is registered only when the key is set.
Its values are `{32, 4, 124 | (124 << 16), 1}`. Paths and delayed damage are simulation
state; shadows are drawing-only and excluded from the record. Peers with different
values, failed installs or a missing rule cannot join. A pool refusal can occur for
props even with fewer than 32 actors. At the physical delayed-damage caller
`0x451028`, the returned boolean is ignored: stock drops a refused delayed hit.

## Descriptor and stock compatibility

CAI consumers obtain their receiver/base/stride/count and guard bytes from
`actor_ai_layout.hpp`. Each loaded consumer registers, including disabled modules.
Stage 1 publishes the retail layout before any consumer installs. The descriptor
does not enable stage 2.

The design's W7 asks for four 96-entry tables immediately, while T5 requires
byte-identical stock caves, including their embedded table addresses. Adjacent
table growth conflicts with that byte requirement. This implementation preserves
the 32-entry table spacing for stage 1 and selects four separate 96-entry tables
for a future 96-controller descriptor. Both fit the existing writable page. T5
compares the complete 4096-byte emitted code page against base `b6a1c8c`; this is
an explicit interpretation/deviation from “96 entries always.”

## Game-free verification

Build below normal priority with MSBuild node reuse disabled:

```powershell
(Get-Process -Id $PID).PriorityClass = 'BelowNormal'
cmake -S . -B build -A Win32
cmake --build build --config Release --target xml2_fix xml2_actor_pools_host xml2_actor_ai_descriptor_test xml2_patch_transaction_test -- /nr:false /m:2
python -B scripts/test_actor_pools.py --exe <retail-XMen2.exe> --db <round3-native.sqlite> --audit <stage0-audit.md>
```

The retail executable must have SHA-1
`7d95cdb4a9a599b982147a3389ea7aff211cb82b`. It is a read-only input. The test host
loads its sections into its own reserved image and executes the retail pool/guard
instructions. It never executes the game's entry point, loads its engines, or
opens a game window. Heap/payload construction/scheduling services are synthetic.

The generator uses inventory boundaries, verifies their bytes against the retail
image, and decodes candidate operands with Capstone. The audit is supplied only
to the independent comparison test. T1 matches all field/role/address sets;
T2 matches the original constructors' layouts. T3 rejects missing/corrupt ledger
rows. T4 checks every image byte and the absent-rule record. T5 checks the complete
stock code page and descriptor guards. T6 executes retail lifecycle/division,
multiword clears, handle generations and capacity refusals. T7 compares complete
per-operation accept/refuse/live-count traces for 4000 churn steps per pool.
T8/T9 execute the three affected retail paths with synthetic lookup receivers,
including each null combination and non-null register/flag/write comparisons.
T10 exercises agreement in both directions. T11 injects wrong guards, allocation
failure and a protected page that cannot be acquired, checking unchanged bytes
and unverified agreement; shared transaction tests cover rollback failures.

## Telemetry and live handoff

Counters update synchronously on every PC at retail take sites. `peak` is the
highest accepted/live count. `would_refuse` counts eligible take attempts made
with at least 32 already live, including full-pool attempts; `refused` counts
attempts at 124. An already-busy delayed-damage entity is rejected before those
sites and does not count as pool exhaustion.

Enable `[Test] InputPipe` for live collection. `ACTOR_POOLS` returns JSON for all
three pools, and the sampled telemetry is logged on changes. The ordinary `STATE`
and `EVENTS` commands retain their existing formats.

Live acceptance remains the game agent's work. Use `slot.py --wait`, check
HOLD-GAMES and outside-workspace XMen2 processes before every launch, preserve and
restore the registry byte-for-byte, and use private/background copies of
`_candidate_040f` with no foreground/focus calls:

| Run | Required evidence |
|---|---|
| S1-L1 | Key absent; smoke three zones; candidate-equivalent logs/checksums. |
| S1-L2 | Key 124; walk ten densest census zones; no crash; report all pool peaks, would-refuse and actual-refusal counters. |
| S1-L3 | Teamless fifth enemy cluster on a ledge; stage 1 avoids the `0x50e1d8` fault; reproduce baseline once on the agent's own copy. |
| S1-L4 | Two stage-1 instances for 15 minutes without desync, then stage-1 versus beta mismatch; refusals name ActorAI on both sides where supported. |
| S1-L5 | NpcPowerRule logs on the dense scenes match beta 7 at the unchanged controller count, accounting for the slot-reset correction. |

Do not ship a test candidate until the code review and live runs pass. The
coordinator assigns its identity and commits the worktree.
