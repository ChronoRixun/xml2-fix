# Online tick rate (`[Online] TickRate`)

XML2 Fix 1.8.0-beta.3. Online sessions of the stock game run at 30 frames per second; single-player runs at 60.

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
