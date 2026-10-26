# Officer weapon brain, revision 9 (round 6)

Port-only measurement on 2026-10-24 against the S2 original table (owner's
default.xbe in xemu, S2 rings and samples re-read; no new xemu run). The port is
a private copy of the 0.4.0-beta.5 package (040f) with the candidate DLL swapped
in; candidate DLLs are this branch and revision 8, each merged with the local
beta-5 head. Staging is S2's: haarp2_1, hero (771,367,-119) HP 10000, officer
HP 115, EP 0 or 103 of 103, on the hero-to-officer line at 120/300/520, fixed
camera, natural AI, 30 s windows, two windows per cell, FrameRate 60, no other
game running in any window.

## What the original does (S2 ring, six windows)

- The weapon brain does not run while the officer repositions: it starts about
  1.5 s after activation from 120 and 520 (after running out to 306-312 or in to
  338-339) and at once from 300.
- Its burst timers stay at the constructor values (0 / 0.6 / 100) and the burst
  branch at `0xefc4a` is never hit; power and fire (`0x90`) are held on every
  brain update. Shots are the input gate's `power_attack` requests at the 0.9 s
  recoil: 32/33/32 per 30 s at EP 0, one looping `power_attack` move.
- At full energy the first power decision presses the grenade (input `0xa0`
  once, `power_smash`); the brain then holds fire, the gate keeps requesting
  `idle` against the priority-5 move and never requests `power_attack`: the
  grenade move is held for the whole window, 0 shots.

## Port results (shots per window; gaps are sound-event gaps)

| Cell | Original | Released | Revision 8 | Revision 9 |
|---|---|---|---|---|
| 120 / EP 0 | 32, median 0.905 s, holds ~337 | 0/0, 16/17 melee, ~75 | 32/29, 0/4 melee | 31/32, mean 0.908 s (0.899-0.917), 0 melee, 339/315 |
| 300 / EP 0 | 33, median 0.918 s | 0/0, 16/14 melee | 33/29, 0/4 melee | 33/33, mean 0.908 s, 0 melee, 300 |
| 520 / EP 0 | 32, median 0.910 s, closes to ~339 | 0/1, 18/13 melee | 32/29, 0/4 melee | 32/32, mean 0.908 s, 0 melee, closes to 340/337 |
| 120 / full | 0, grenade held, ~312 | 0/0, 16/15 melee | 29/29, 3/4 melee | 0/0, grenade held, 323 |
| 300 / full | 0, grenade held | 0/7 (7-shot burst), 15/14 melee | 30/29, 3/4 melee | 0/0, grenade held, 300 |
| 520 / full | 0, grenade held, ~339 | 0/7 (burst), 14/12 melee | 29/28, 3/4 melee | 0/0, grenade held, 337/334 |

Revision 9 gaps take the 60 Hz frame values 0.899-0.900 and 0.916-0.917 only
(mean 0.908 s in every EP-0 cell; no gap under 0.899 s, no burst). The
original's 0.905-0.918 s medians are the same 0.9 s gate at its 49 Hz cap.

Revision 8's good EP-0 windows are not its own branch: in that session the
native near-target query found an entity on every update, so the native
held-fire branch ran and revision 8's burst/pause code was never reached. In
sessions where the query finds nothing (probes on the same package), revision 8
fires one frame per 1.2-2.7 s pause (5 shots per 12 s). At full energy it never
holds the grenade.

## OFF

The revision-9 DLL without the opt-in rows leaves all seven candidate sites
(plus the picker site) byte-identical to the executable and advertises the
released rule 16 revision 4 `{1,1,0,0}`.

## 49 fps sessions

Two revision-9 sessions at FrameRate 49 (the original's cap). Where the officer
repositioned normally the cells match the original value for value: EP 0 at
120/300/520 gave 32/33/30 shots with every gap 0.918 s (45 frames), full energy
held the grenade at 312 and 298 units with 0 shots. But in 7 of 12 windows of
each session (from the 6th window on, and once earlier), the PC's own repositioning did not
behave like the original: the approach from 520 was ended after 0.33 s
(state 7 ended through `0x50cf70`, then state-0 re-queues and walk/idle cycling
at 511), or the back-off from 120 ran 3.9 s instead of about 1.5 s; after that
the officer kept about 500 units in every later window of the session, where the
grenade's PC distance band [120, 500] (`0x78b908` table, type 15) no longer
applies, so it fired instead of holding the grenade. Revision 9 does not touch
this navigation code; revision 8 shows the same stall (S2, and one probe at
60 fps). The original repositions once for about 1.5 s in every window.

## Cause of the 49 fps repositioning failure (diagnosis only)

The early end of the approach is XML2's generic actor stuck test, not the
weapon brain: the movement update (`0x433a28..0x433a54`) counts an update as
stuck when the AI-manager getter (vfunc `+0x128`) times the length of this
update's displacement is below 0.008, and after three such updates
(`0x424d4e..0x424d8e`, actor+0x3d9 bit 0x08 and actor+0x3f0 >= 3) the AI's
blocked callback `0x50cf70` ends the move. That callback also keeps a strike
counter (ai+0x1c0) across re-activation, matching the later windows held at
about 500. The test is per update with a fixed threshold, so it depends on the
frame rate; it runs for every AI-driven actor, so it needs its own rule after
reading the original's equivalent, not an officer-scoped patch.

## Proposed gap acceptance per frame rate

The 0.9 s gate lands on whole frames: 0.900/0.917 s at 60 fps (mean about
0.908), 0.918 s at 49 fps. Gaps should be these frame-quantised values at the
measured rate, mean within 0.905-0.918, none under 0.899 s (frame-time jitter
under CPU load produced single gaps down to 0.853 s at 60 fps).

## Not covered

The PC repositioning issue above, damage parity, the officer's behaviour after
the grenade hold is broken (hero attacks), two officers, live co-op, other
weapon families (MP5 soldiers are spawner-only and were not measured) and other
frame rates.
