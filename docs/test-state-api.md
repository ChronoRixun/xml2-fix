# Test state API (schema 1)

With `[Test] InputPipe=1`, the existing pipe accepts `state`, `objectives`, and
`events` (no arguments, case insensitive). Each returns one JSON line, without
an `ok` prefix. Existing input commands retain their protocol. With InputPipe=0
no sampling runs, no pipe is opened, and no new engine hooks are installed.

Snapshots run on the game's keyboard thread at most ten times per second.
`sampled_ms` is Windows monotonic uptime. Clients must detect an unchanged sample
stamp: a responding pipe does not prove that the game thread is running.
An unsupported executable returns an explicit JSON error. Unreadable individual
fields are null. Four party entries are always returned; a named slot without a
live body has null health/position rather than a fabricated death.

- `mode`: menu (main-menu zone), loading, in-zone, or movie; null when unknown.
  `menu_open` can be true during in-zone play. `menu` is the internal menu name.
- `zone` is the requested/current path, named before loading finishes. `loading`
  distinguishes that interval. `act` is the engine's current act number.
- `conversation`: active flag, current line id, visible response count, and
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
