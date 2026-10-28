# Test state API (schema 1)

`PARTICLE_SCRATCH` returns the [particle scratch](particle-scratch.md) counters:
`installed`, `requested`, `capacity`, `logged_arrays`, and an `operations` array
with `va`, `peak` (unclamped demand), and `clamps` for each of the eleven methods.
Counters are session totals and are collected when `ParticleScratch=40` installs.

`script_pools` returns W3-8's installed/requested state and four counter objects
(`statements`, `values`, `scripts`, `contexts`): `cap`, `live`, `peak_zone`,
`peak_session`, `refused`, `refused_zone`. `null_values` counts compiler null
refusals; `refusal_observers=false` means OFF occupancy sampling only.
`cached_scripts` reports the separate compiled filename-cache counters.
See [script-pools.md](script-pools.md) for sampling and integration limits.

With `[Test] InputPipe=1`, the existing pipe accepts `state`, `objectives`, and
`events` (no arguments, case insensitive). Each returns one JSON line, without
an `ok` prefix. Existing input commands retain their protocol. With InputPipe=0
no keyboard state sampling runs, no pipe is opened, and no new engine hooks are
installed. ScriptPools' separate read-only sampling uses an existing Present
hook independently of InputPipe.

State/objective/event snapshots run on the game's keyboard thread at most ten times per second.
`sampled_ms` is Windows monotonic uptime. Clients must detect an unchanged sample
stamp: a responding pipe does not prove that the game thread is running.
An unsupported executable returns an explicit JSON error. Unreadable individual
fields are null. Four party entries are always returned; a named slot without a
live body has null health/position rather than a fabricated death.

- `mode`: menu (main-menu zone), loading, in-zone, or movie; null when unknown.
  `menu_open` can be true during in-zone play. `menu` is the internal menu name.
- `zone` is the requested/current path, named before loading finishes. `loading`
  distinguishes that interval. `act` is the engine's current act number.
- `conversation`: active flag, current line id, visible response count, selected response index, and
  `speaker` (the current node's internal speaker key, possibly the X-Team
  placeholder; null when the current file/node cannot safely be resolved).
- `popup`: current popup's active flag. Ordinary menus use `menu_open`.
- `party`: stats identifiers, generation-checked entity ids, origin x/y/z,
  current and maximum health, alive (health > 0), AI control routing flag.
  `actors` provides the same fields for observed characters, including enemies,
  for fight-health monitoring. Names are stats identifiers, not entity-use names.
- `z_velocity`: sampled position delta per wall-clock second, only across the
  same zone and entity generation with no load and a gap at most one second.
  This is not a ground-contact sensor. `on_floor` and `airborne_ms` are null;
  clients must allow for flight, lifts and cutscenes. Velocity is on party entries.
- `objectives`: current act records (null when unavailable), internal name,
  count, count_goal, complete, enabled, shown. Enabled and shown both mean the
  inverse of the engine's hidden/disabled bit; they are not independent flags.

Carry diagnosis adds read-only fields to each observed actor: `energy` (+0x288,
normal current energy), `max_energy` (+0x314), `pending_energy` (+0x6ac, queued
recovery; not proof of a refill), `owner_entity` (+0x178), `selected_entity` (+0x76c), `ground_contact`
(+0xbc), `physical_flags` (+4), `actor_flags` (+0x3d8), `status_byte` (+0x579),
`flying` (actor_flags bit 2), and current `move_pointer`, `move_handler`,
`move_animenum`, `move_name`. The last fields read CMove +0xc8/+0xbc/+8 and
the bounded common-name pool; they never invoke an actor or move function.
In native 0x434e4e, status byte bit 0x40 skips the input/frame callback block.
These are sampled raw engine values, not an assertion that a carry succeeded.
The special/infinite-energy branch of native 0x41d5d0 returns the maximum
instead of stored current energy; this observer deliberately exposes both raw
fields and the live probe requires stored current energy to cover the trial.

For the next live round, enable `[Test] CarryTrace=1` alongside InputPipe.
The log records carry entry, input/frame state (250 ms per actor/stage), and
the actual animated world-space hand midpoints already queried by attachment,
using selectors 6/5. It adds no bone or gameplay predicate calls. No hand line
means the hand branch did not execute during that sampled interval. Compare
the victim enum, current node, status byte, owners and energy before blaming a
different bone index. Leave this diagnostic switch absent for ordinary play.
`valid_mask` bits 0/1/2/3 report carrier RHand/LHand and victim RHand/LHand
query success; F means both complete pairs, lower masks include a fallback.

`scripts/carry_live_probe.py --device pad --actor ENTITY_ID` prints a plan.
Only a live operator should add `--execute` against an already running test copy.
It measures fresh state after the registered `restoreEnergy("ENTITY_ID",999)`
script, holds Guard for 8 s, repeats the refill every 500 ms, records state,
and releases in a finally block. `--device keyboard` uses E. `--cost 20`
is Flight 3; ranks 1/2/4/5 use 40/30/10/5. Redirect its output to live evidence.
A queued script acknowledgement is insufficient: stale snapshots or energy
below that cost abort the trial. `setEnergy` is not registered in the retail PC
script table. Run a separate no-refill exhaustion trial when testing depletion.
The helper does not launch a game or activate a window. Offline protocol checks:
`python -B test/carry_live_probe_test.py`.

`events` drains a queue of at most 512 sampled transitions. `dropped` reports
old records discarded since the previous drain. Events include observer start,
load start/end, zone change, conversation start/end, objective changes, hero
health death/revive, and recognized team/game-over menus. Death/revive detail is
the hero identifier. Events are sampled observations, not an exhaustive hook
trace: short transitions between polls may be missed. First observation is a
baseline, not a fabricated load or death. Script-error capture is unavailable
(`script_errors_available=false`); a debugger log can supplement the pipe.

Addresses and their Ghidra evidence are in `test_state_rules.hpp`. All engine
reads go through the SEH adapter; bounds, object types, finite floats, and entity
generations are checked. Synthetic fixtures in xml2_test cover corrupt/missing
reads, generations, objective bits, JSON escaping, nulls, transition history,
queue overflow/draining, and vertical-speed reset at zone boundaries.

This API is a companion dependency for the Legends Classic autopilot draft.
No campaign acceptance result follows from the synthetic tests.

Party binding uses a unique stats-name candidate passing the engine party collector's
membership flag and controller-assignment tests. An NPC double is excluded even
if it shares the hero's stats name. Multiple qualifying actors return
`binding="ambiguous"` and null body fields; no match returns `unresolved`.

The `gameover`/`game_over` menu aliases are **unverified in live play**. Their
transition event must not be treated as a verified game-over-screen detector.
Clients can separately report observed party deaths, with that evidence labeled.

For cold saved-campaign tests, use the normal main-menu Load Game action. The
`loadgame` console command from a fresh main menu bypasses controller setup;
successful file/native loading can then leave spawned bodies without controller
assignments. Such bodies are excluded from party binding and saved-position
placement. This reproduces with otherwise healthy saves in multiple zones.
An in-campaign console reload is a different entry path. When diagnosing an
unresolved party, inspect case-insensitive name matches in `actors`: null party
health is not proof of death, and `on_floor=null` is not proof of a fall.

`script_controls_locked` reads lockControls' game-time deadline (negative means
indefinite). Navigation and assists must wait while it is true, and may not treat
an unreadable value as permission to assist. Menu/popup locks remain separate.

Round 2 live checks verified the current menu name `team` during a generated
mission-start team selection, and active conversation speaker/cursor fields in
stock tutorial/Genosha conversations. The game-over aliases remain unverified.

`xml2_test.exe --state-rules` runs the observer and input-parser fixtures without
requiring a connected controller or a renderer. The default full suite also checks
physical controller state; a non-centered pad can fail its unrelated idle assertion.

`CHAR_SOUNDS` observes the installed character sound cache: JSON fields `installed`, `requested`, `capacity`,
`peak`, `refusals`, `empty_sounddir_actors` and `allocations`. The exact allocation observer updates counters on the
game thread; the pipe reads atomics. Absent/16 leaves retail bytes uninstrumented. See [char-sounds.md](char-sounds.md).

`MOTION_POOL` reports the installed motion descriptor and exact allocation/clip/reader refusal counters,
including peak occupancy and per-owner counts. See [motion-pool.md](motion-pool.md).

`RESIDENT_STATS` reports resident/hero/NPC counts and peaks, refusal counters (outer/hero/NPC), and zone invariant failures. `RESIDENT_CENSUS` queues a game-thread census; polling after the next sample returns cached definition indices, handles, flags and payload classes.
