# Pool diagnostics

The compatibility DLL samples 44 shared/aggregate counters plus every style's move-list counter by default (63 counters at retail or 76 with FightStyles=32). It reads them on the
game thread at the existing Present boundary, between simulation steps, and skips
pool reads while the zone-loading flags are set. It never patches the executable
for sampling. Every read checks the memory range and catches access faults; null
heap owners and unbuilt static managers remain unknown.

The sampler uses fixed storage, including its zone-summary queue. It allocates
nothing during a frame or zone transition. A worker formats and writes completed
snapshots. If the queue fills, the zone boundary waits for a free slot rather than
discarding a summary. The worker starts only after successful DLL initialization.

At an observed zone unload, one summary is written to `xml2-fix.log`:

```text
pools example_zone: fxA 120/374 peak 201 refused 0; fxB 0/374 peak 10 refused 0; prims 290/336 peak 336 SAT refused 0; ...
```

Every counter appears as `name live/cap peak zone_peak [SAT] [CORRUPT] refused
zone_refusals;`. A dash means unavailable. `SAT` means the observed peak reached
capacity. `CORRUPT` identifies an impossible occupancy, including the unchecked
FightMove pool's negative free count or live count above 332. These conditions
are observed without changing the engine's allocation behavior.

Zone peaks reset when loading begins or the zone name changes. A same-name reload
also resets them when its loading interval is observed. Session peaks persist.
The values are sampled high-water marks: a transient allocation and release
between two safe points can escape the sampler. No game or live-fight claim is
implied by the native fixture tests.

## Test pipe

With the existing `[Test] InputPipe=1`, `pools` returns the latest snapshot:

```json
{"_meta":{"zone":"example_zone","active":true,"epoch":1,"refusals_observed":false,"dropped_zone_logs":0},"fxA":{"cap":374,"live":120,"peak_zone":201,"peak_session":250,"refused":0,"refused_zone":0,"corrupt":false}}
```

The actual reply contains every shared and per-style pool object. Unreadable live/peak fields are
`null`. `refused` is the session total and `refused_zone` is the zone total.
`refusals_observed:false` means refusal instrumentation is off: zero refusal
counts do not establish that no request was refused. Pipe replies and log workers
read copied DLL state, never game pointers.

## Optional refusal observers

```ini
[Test]
PoolCounters=1
```

The absent key defaults off. With the key, 61 guarded instruction spans install
in one transaction. Eleven additional D1 counters record particle-array counts (74 total counters at retail, 87 with FightStyles=32). Seventeen separate primitive factories account for the
larger-than-estimated observer count. The observers retain native comparisons,
branches, results and calling conventions, and count capacity refusal paths.
The first observed refusal for each pool in a zone adds a log notice.

The model observers use the common exit of the native/ModelPools preflight and
the existing preflight call; they do not overwrite ModelPools operands or caves.
Component refusal counts name the component that stopped allocation. `models`
counts all failed model preflights, including component failures; these counts
must not be summed as independent requests. The particle observer counts the
constructor's NULL free-list pop. FX A and B have separate counts.

Styles count the registry's native NULL returns. Curves distinguish a legitimate
preset-zero result from the native full-pool zero using the native return flags.
The curve entry wrapper copies its by-value arguments; its scratch ECX argument
pointer therefore moves. The generator verifies all 15 direct callers overwrite
ECX before using it (allowing only the ECX-independent pool getter in between),
and verifies that no absolute allocator pointer escapes that caller census.
Native fixtures compare zero-preset, exhausted and successful-new-curve paths,
including pool memory, return value, flags, stack cleanup and preserved registers.

The unchecked FightMove and per-style move-map pools have no native refusal to
observe. Their excess occupancy is reported as corruption. Diagnostics add no
gameplay-agreement rule; the agreement record is identical with observers on/off.

For an installation proof, `[Test] PoolSampling=0` disables D0. Its default is on.
This key changes no engine capacity or agreement value.

## Revision 2 script and particle measurements

`values.null_returns` and `null_returns_zone` explicitly name the existing value
allocator refusal counts. Both native value-factory full-pool branches (including the check at
`0x4d6578`) return NULL; each is counted once.
The later compiler dereference at `0x4d89f1` remains unchanged. Diagnostics do not
prevent or recover from that crash.

With D1 enabled, `particle_array_<operation VA>` identifies each of the eleven
operations at `0x578d20`, `0x578e10`, `0x578ec0`, `0x579000`, `0x579290`,
`0x5794e0`, `0x579600`, `0x579800`, `0x579910`, `0x5799a0`, and `0x579ae0`.
Each observer executes the original virtual `+0x74` count call exactly once and
retains its result. No clamp or scratch-storage raise is installed. ModelPools
still owns the scratch-base relocation: 40 records of `0x90` at retail manager
`+0x470`. The count call at `0x578d25` and selection at `0x579b13` are examples.

For these operation counters, `live` is the largest observed count in the most
recent sample batch; `peak_zone` / `peak_session` retain the largest count across
all calls, including calls between frames. `exceeded` / `exceeded_zone` count how
often the original count exceeded 40. Negative signed counts are ignored, as the
native operations do not enter their loop for them. Zero is a valid observation.
Counts of 41 and 300 remain 41 and 300. The operations are not refusals, so their
`refused` fields remain zero. Loading peaks/excesses are retained for the next
zone; an aborted load does not charge them to an unrelated later zone.

The additional log suffixes are `values ... null N` and
`particle_array_578d20 ... peak 300 ... exceeded N`. These fields, like other D1
counts, cannot establish absence of demand while observers are disabled. If the
process crashes before a snapshot/log write, unflushed counters may be lost.

## Counter coverage

| Identifiers | Retail capacity | Source |
|---|---:|---|
| `fxA`, `fxB` | 374 each | Separate managers at `0x6f3c50`, `0x6f8ee8`, live `+0x17e0` |
| `fxA_128`, `fxB_128` | 128 each | Manager live `+0x401c` |
| `fxA_64`, `fxB_64` | 64 each | Manager live `+0x4584` |
| `fxA_300`, `fxB_300` | 300 each | Manager live `+0x4dd4` |
| `prims` | 336 | `[0x6f3c38]+0x23228` |
| `particle_systems` | 40 | ModelPools' published free-count descriptor; capacity minus free |
| `transforms`, `attributes`, `shader_fx`, `igb_models`, `actors`, `proc_geometry`, `particle_clouds`, `movie`, `models` | 692, 260, 432, 400, 32, 220, 40, 1, 692 | ModelPools' installed addresses and capacities, including x2/x4 and ModelHandles |
| `affecters`, `powerups`, `primary_fx`, `primary_data`, `fx_core`, `scopes`, `boltons`, `attached_boltons`, `vf_nodes` | 384, 128, 128, 96, 128, 128, 32, 64, 192 | Nine heap owners in the generated read ledger |
| `statements`, `values`, `objects` | 620, 1556, 120 | `[0x787740]+0x15914`, `+0xc090`, `+0x261a8` |
| `contexts` | 30 | Running contexts: records `[0x787740]+0x2f2f4`, stride `0x5c4`, live `+0x3a07c`; native cap check `0x4d8711` |
| `zone_globals`, `game_globals` | 32, 100 | Script owner `+0x26550`, `+0x26dd8` |
| `styles`, `events`, `curves` | 19, 1064, 900 | Limits' published installed descriptors; styles/curves follow their configured raises |
| `moves` | 332 | `[0x789e1c]+0x1aef8`; free `+0x1aec8` checked for aliasing |
| `moves_per_style` | 150 | Maximum across active styles, using the installed style layout |
| `style_moves_0` through `style_moves_N-1` | 150 each | Each active style slot's own move list; N follows the installed 19/20..32 style capacity. Inactive slots have unknown live values; high-water marks persist for that physical slot within the zone/session |
| `fx_files`, `fx_defs` | 220 each | `[0x6f3c3c]+0x44fc`, `+0x869c` |
| `prim_defs`, `vectors` | 816, 710 | `[0x70b64c]+0x38a20`, static vector pool `+0x4e70` |
| `particle_array_<operation VA>` (D1 only) | 40 scratch records | Eleven unclamped original particle-array counts; independent maxima and excess totals |
| `prims_per_effect` | 10 | Maximum across active definition records, excluding freed slots |

## Native verification

Generate/check the retail ledger with an owned executable:

```text
python scripts/pool_counter_sites.py <XMen2.exe> --check --verify --external-src <charsounds/src>
```

This verifies the executable hash, read offsets, instruction boundaries, every
guarded observer byte, incoming code/data references, curve-caller liveness, and
overlap with source guards and the separately supplied R1/R3 tables.

Build/run `xml2_pool_counters_test`, `xml2_pool_counters_runtime_test`, and
`xml2_pool_counter_observers_test <XMen2.exe>`. They use owned fixtures only.
The runtime fixture checks every guard-byte mutation, all-or-nothing publication,
rollback, exact D0 image identity and zero sampler allocations. The optional
union-host environment variable `UNION_POOL_SAMPLE_RVA` invokes the production
sampler 16 times after installation and requires the complete game image to
remain byte-identical. Neither fixture starts the game's entry point.
