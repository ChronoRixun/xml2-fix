# Shop diagnostics and E4 live verification

This procedure is pending human execution. The code and host fixtures do not
establish a live-game pass. Use an isolated copy of the human test kit, its
unchanged content, and this task's DLL. Keep the kit INI's gameplay/limit values.
Record the DLL SHA-256, INI, logs and seed/save used for every run.

Retain `[Test] InputPipe=1`, the human-kit `PipeName`, and `NoActivate=1`.
Use a unique pipe name per process. Respect the game-slot hold and registry
export/restore rules before any future authorized game launch. This task does
not authorize a launch or desktop input.

## Forge and Healer, G1 through G5

For each of the five loot groups, use the corresponding kit seed and confirm
the highest hero level/group with the kit answer key. Use the kit's normal
Visit Forge and Visit Healer shortcuts (InputPipe stays enabled).

1. Open Forge twice, close between opens, and repeat with Healer. Record the
   initial tab and `economy shop` / `economy events 0` replies immediately.
   Both vendors must open, remain responsive and close without an AV. A valid
   open has nonzero definition/stock hashes; an incomplete view may report an
   error and skip its event, but must not publish zero-filled partial evidence.
2. Include the reported sell-first state with no sellable inventory and zero
   money. Do not inject malformed pointers into a live game. Verify empty rows
   are safe, then switch to Buy and back. Record the focused-list identity and
   visible row count for each tab; list names are canonical buy/sell identities.
3. Compare Forge stock, group eligibility, listed buy/sell prices, currency and
   obtained/equipped state with the kit answer key. Buy and sell one legal item
   when the seed has funds; attempt an unaffordable purchase in the zero-money
   case and confirm no debit. No interaction should produce a diagnostic crash.
4. Check Healer's synthetic health/energy packs and bags; make one funded
   purchase and one refused purchase where applicable. Confirm exactly one
   native debit for a successful transaction and no diagnostic RNG draws.
5. Capture logs/replies before closing. Record PASS/FAIL separately for each
   vendor and G1-G5 combination (10 rows), including both open repetitions.
   Repeat one seed with InputPipe absent to compare native economy outcomes.

## E4 accounting on/off

Use the same kit scene, team, seed, route and duration as the prior E4 test
(Master Mold reinforcement scene, 90 seconds, then the same teardown).
Run E4 OFF, ON, ON, OFF; InputPipe=1 and all other kit values stay identical.
Record frames, E4 `counts[7]`, `accounting_suppressed`, `counts[3]`,
`probe_faults`, published/constructing/destroying/orphaned/reserved counts and
full `event_callers` before the fight, after 90 seconds, and after teardown.

- OFF: `enabled:false`, `installed:false`, no E4 observation/accounting output.
- ON: ordinary stable accounting produces zero new kind-7 events, even with
  controllerless actors; published controller ownership must remain consistent.
  There must be no reset-induced transitions at caller `0x523977`.
- A real mismatch remains a failure to investigate. Identical repeats are
  limited to one per 120 client steps per accounting source; the suppressed
  counter explains the omitted repeats. Host fault fixtures test the limiter;
  do not manufacture live memory corruption to trigger it.

Compare frame-time percentiles with the previous ABBA measurements. Archive
all raw evidence; do not infer a pass from log rate limiting alone.

## Newer-save refusal

On an isolated copied save, use the future-feature host fixture to verify the
message: "This save was made by a newer Legends Classic build - update to load
it". Keep required feature bits in the log. The named W2-layout refusal remains
available when that known layout is disabled. Older builds refusing W2 saves
is intended; never modify or overwrite an original save to bypass the refusal.
