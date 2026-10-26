# Revision 8 matched weapon trial

The final serial rerun restores the officer's firing behavior in the tested
visible, initially energy-empty window. Original and revision 8 each fired 49
shots, with median gaps of 0.912 and 0.910 seconds. The released PC build instead
closed to melee. This supports the weapon-brain/input restoration; it does not
resolve the separate unarmed-enemy frequency report.

## Results

| Measurement | Original Xbox | Released PC | Revision 8 |
|---|---:|---:|---:|
| Officer median shot gap, seconds | 0.911530 | No shots | 0.910038 |
| Officer shots in approximately 45 game seconds | 49 | 0 | 49 |
| Officer melee entries in that window | 0 | 24 | 0 |
| Officer window, game seconds | 45.063412 | 45.009224 | 45.021713 |
| Officer offensive entries per enemy-minute | 65.241 | 31.993 | 65.302 |
| Separate melee NPC entries in approximately 12 game seconds | 8 | 6 | 7 |
| Melee control window, game seconds | 12.036514 | 12.039890 | 12.033321 |
| Mean sampled host CPU during officer window | 91.4% | 90.6% | 92.4% |

Counts are actual events inside the measured windows, not normalized predictions.
Shot gaps are between firing sound events; the original range was 0.886–0.950
seconds and revision 8's was 0.843–1.031. Animation-event timing is distinct from
recoil admission. Revision 8 had 49 handler entries and 49 fire requests; the
smallest handler-entry interval was 0.900116 seconds, with none below 0.9.
The original had 49 recoil arms, all equal to binary32(now + binary32(0.9)),
and no request before its deadline.

The one-window melee counts are descriptive, not a statistical parity or
regression verdict. The compatibility metadata selects only the officer.

## Matching and load

All four shared game slots were held by this investigation. Exactly one owned
PC game or emulator ran at a time. Call of Duty was explicitly permitted by the
owner. Host CPU was heavily loaded; the sampler did not identify a `cod.exe`
process during these windows, so the load is not attributed to that game.
The coarse load samples (five per officer window) peaked at 92.6%, 95.2% and
96.4%, respectively. Observed wall durations were 45.027, 45.129 and 45.453
seconds. No other test game overlapped. This is not an idle-host benchmark.

Each officer run used the same room, level-5 player, level-6 officer, positions,
activation procedure and fixed camera. Both actors passed the native camera
predicate; the officer stayed in view in every sample. Officer HP/max was 115,
initial energy 0/max103, with natural regeneration afterward. Player HP/max was
staged to 10000 and energy/max to 71. Other enemies were inactive and relocated.
No invulnerability or attack injection was used. A short movement settled the
actors before measurement. The independent melee control used player HP100,
energy71 and a level-10 enemy at close range for twelve seconds.

The PC cap was 60 FPS; intervals use each engine's native clock. Ranged source
and candidate both had range550, ordinary AI class0, and gun capability types
removed from slots1/3 while the authored grenade slot remained. Released PC
had the basic brain, range90 and converter-added gun capability flags.

## Gates and scope

- All six timed windows completed, with monotonic PC clocks, living players,
  no observer errors and no overlapping test games.
- Revision 8 installed rule16 revision8 with the expected source fingerprint;
  released stun rule18 revision3 coexisted.
- A fresh revision8 OFF startup matched all six examined released code sites
  (37 bytes), including the unpatched old picker site. Rule16 remained
  revision4 with values `{1,1,0,0}`.
- All seven per-test registry exports matched after restoration. Both original
  event-ring sequences were complete and their hooks restored. Both owned
  games closed, all four slots released immediately after the tests, and the
  released runtime DLL/INI restored. The retail executable was unchanged.
- No gameplay code changed for this rerun. The previously reported revision8
  native/source-differential gates (2196 checks), companion builder units
  (738 tests), and native agreement/combo/stun suites are retained evidence;
  they were not rerun or described as new tests here.

This is acceptance of the bounded officer shot-cadence window, not general
combat parity. Full-energy grenade/input interaction, damage parity, saved-game
and live co-op behavior, the unarmed cohort, and the other nineteen weapon-brain
NPCs remain outside the result. Normal builder output remains unchanged.

Private reproducibility labels are `r5-original-ep0-01`,
`r5-pc-released-ep0-01`, `r5-pc-rev8-ep0-01`, their three `melee01` controls,
and `r5-pc-rev8-off01`. The private trial package retains setup, raw events,
samples, load, reservation and registry evidence; none contains redistributed
game assets in this repository.

Candidate code commit: `a0520f6`; companion builder: `526e910`.
Candidate DLL SHA256:
`b0059612a0e3ae4c18ed9ff98c9c43ce06285efe4c14e9db1c21e25712b678b9`.
