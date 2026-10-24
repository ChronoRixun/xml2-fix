# Online tick rate (`[Online] TickRate`)

XML2 Fix 1.8.0-beta.3 (TickWait: beta.4). Online sessions of the stock game run at 30 frames per second; single-player runs at 60.

```ini
[Online]
TickRate = 60   ; 60 or 30; absent = the game's own 30
```

## What the game does

`CClient::frame` (0x401d70) writes its minimum frame time at the top of every frame: `1/30 s` (0x401dab, immediate
0x3d088889) while the session's online flag is set (session getter 0x612be0, online test 0x610d20 = session byte
+0x420), `1/60 s` (0x401db4) otherwise; at the end of the frame (0x401fcb..0x40200c) it spins until that much wall time
has passed. Online play is lockstep: input capture (0x558ec0) stores each player's measured frame time in his input
record, the consumer (0x559190) runs one simulation step per frame with the slowest player's frame time as the step,
and the gate 0x606a50 waits for every player's input for that frame. The simulation therefore keeps wall-clock speed
at either rate; a higher rate means more, shorter steps, more input records and more checksums.

`TickRate = 60` replaces the online `1/30` with `1/60` (`frame_rate_rules::online_tick_patch`) from DllMain, after
checking the 16 bytes, with a read-back (put back on a mismatch). `[Display] FrameRate` patches the offline `1/60`
in the same bytes; each patch accepts the other's value there. With `FrameRate` set the fix paces offline frames
and, online, the game's spin still caps the session at the tick rate (a `FrameRate` below it slows the session).

## The 15 fps cliff (`TickWait`)

```ini
[Online]
TickWait = timer   ; timer (default with TickRate) or spin (the game's own busy-spin, as in 1.8.0-beta.3)
```

Beta 3 at 60 on a PC running two other games (agent test games) hosted alone at ~15 fps; at 30 the same PC played
normally. The minimum-frame wait (0x401fe9..0x40200c) spins on the game clock (igTimer, QueryPerformanceCounter), so
the Windows timer period plays no part (no Sleep anywhere in it), and a windowed/borderless D3D8 device never waits
for the vertical blank (no vsync quantisation). What the spin does is make every game a 100 % CPU thread. On a
saturated PC (every logical CPU busy - measured 95-99 %, run queue 5-24 during the reports) the scheduler takes such
a thread off the CPU for whole quanta: the slow frames' game thread ran 150-430 kcycles per ms against ~2900 when it
runs (QueryThreadCycleTime), i.e. it was descheduled 85-95 % of those frames. A 60 Hz frame has ~11 ms of spin slack
and absorbs none of that; a 30 Hz frame's ~28 ms absorbed one quantum, which is why beta 2 hid it. The lockstep's
own step (0x559190: the slowest participant's recorded frame time) has no rounding.

`TickWait = timer` writes 0.0 into the online branch's minimum (`frame_rate_rules::online_wait_patch`, the same 16
checked bytes; the TickRate, TickWait and FrameRate patches accept each other's values) and the fix paces online frames
(session 0xa53058 +0x420 set, what 0x610d20 reads) at the tick rate from its Present hook - or at `FrameRate` when that
is lower - with the high-resolution waitable timer, a final 0.6 ms spin, and the wait at above-normal priority so the
woken thread preempts busy normal-priority threads (without it the timer wake queued a quantum: 28-32 ms frames followed
by 2-4 ms catch-ups). Rule 29 is unchanged: the wait is local, the rate is the session's.

| host on 4 CPUs + 12 busy processes, 60 Hz, solo room | fps (native counter) | period p50 / p90 ms | game CPU |
|---|---|---|---|
| spin (beta 3) | 22.3 | 16.8 / 101.7 | 0.32 cores |
| timer, normal priority | 17.8 | 80.1 / 102.6 | 0.05 |
| timer, the wait above normal (1.8.0-beta.4 default) | 57.9 | 16.7 / 17.1 (p99 89.9) | 0.19 |
| timer, the whole thread above normal online (`TickPriority = thread`) | 59.8 | 16.7 / 17.1 (p99 17.5) | 0.21 |

`[Online] TickPriority = thread` keeps the game's thread above normal for as long as frames are online (the default,
`wait`, raises it only for the pacer's wait). It measured best, but a whole-thread raise can starve the game's own
normal-priority workers (streaming, audio) during an online zone load; it stays opt-in until beta testers have run it.

The lockstep's not-ready iterations (waiting for a remote player's input) still loop without waiting, as stock; they
never reach Present.

## Co-op

Gameplay-agreement rule 29 (TickRate), revision 1, values `{rate in effect, 0, 0, 0}`, registered whenever the key
is there. All PCs of a session must have the same rate: a mixed 60/30 session runs near the slower rate (measured
~27 fps) and the frame cadence is part of the lockstep. A 60 and a 30 PC refuse each other at admission naming
`29 TickRate`; a PC without the key says `29 TickRate (disabled on this PC)`. A 60 request that could not patch the
game advertises 30, so it still plays with 30 PCs.

## Timers at 60

Everything in the fix that times an online session is wall-clock based (GetTickCount): the cut-frame agreement's
1.5 s settle, 3 s record wait and 8.5 s timeout, the 15 s lost-records memory, the join-in-progress limits, the
keepalive and tunnel timers. The native watchdogs (0x613790: 5 s not-responding exchange, 3 s, 10 s removal) run on
the game clock in seconds. Frame-count limits:

| limit | at 30 | at 60 | change |
|---|---|---|---|
| lost player's records a survivor re-sends (`lost_inputs`) | 64 = 2.1 s | 128 = 2.1 s | scaled (`online_tick::frames_for`); the parser accepts 128 |
| native input ring (300 records, 0x79fbb0) | 10 s | 5 s | native; survivors are a few frames apart, far inside it |
| native slow-player guard (0x559190) | frames over 50 ms | same | absolute 50 ms test, counts of slow frames: a PC under 20 fps is slow at either rate; rule 26 keeps the player instead of the native drop |
| native checksum every 30 frames | 1 s | 0.5 s | native cadence |
| 16-bit frame counter wrap | 36.4 min | 18.2 min | the fix's frame arithmetic is wrap-safe (uint16 differences) |

## Cost

Outbound UDP payload (GNS overhead included, IP headers excluded) is about 190 bytes per frame per remote player,
so it scales with the rate the lockstep actually reaches:

| measurement | players | lockstep fps | upload per PC | CPU (one logical CPU) |
|---|---|---|---|---|
| research, quiet PC, direct IP, TickRate 60 prototype | 2 | 58.5 | 11.2 KB/s | ~30 % |
| research, same, stock 30 (timer-paced) | 2 | 30.0 | 5.7 KB/s | ~15 % |
| beta 3, loaded PC (other games, ~100 % CPU), rooms, 25 min | 3 | 18.5 | 7.4 KB/s | 25-35 % |

A slow PC holds the whole session to its rate (lockstep); `TickRate = 30` halves the per-frame work and traffic.
