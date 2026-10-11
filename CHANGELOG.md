# Changelog

## Unreleased

- Added `[Limits] MoveTriggers` (20..127, port default 64): preserves full gun bursts and authored model events
  through guarded handle side storage; native move layout and save data remain unchanged.
  Scope timing packets to enclosing invocations, guard the fixed native packet producer, retire timing
  cache entries with the actor arena and fall back to native nineteen on cache exhaustion.
  Share fallback timers during same-actor reentry so a trigger-started replacement move retains its future
  deadlines instead of dispatching stale timers; enclosing packet ownership stays separately scoped.

- Fix legacy imports whose equipped items were written in a different era from the campaign's talents.
  Translate hero and restore-bank equipment with the detected item era, including semantic dumps.
- Key talent-era evidence by actual stats index so restore-bank records belong to the correct hero.
  These corrections preserve the LC format and wire revision and match the Python reference.
- Keep the corpus loader/saver gate strict while recognizing documented legacy placement promotions.
  Expected ordinals come independently from unique names in the build's numbered placements;
  loader metadata is checked against build ordinals, names and placement indices. Synthetic negative
  controls reject wrong associations even when returned metadata agrees with the wrong saved states.
  All other semantic fields and retained payloads must still match.
