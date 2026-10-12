# Death hand-over

`[Game] DeathSwitchClear=1` makes the hero who takes over from a dead controlled hero stand still until the player
moves it, as in X-Men Legends I. The default is 0, the PC game's own hand-over. The companion builder writes the key
for every first-game build.

## What the PC game does

When the controlled hero dies, CActor onDeath (0x429de0) hands its controller on at 0x42a042: cdecl
0x420860(out, team 0x1d, controller) picks the first living AI party member, writes the controller into its +0x3d4
and makes it the controller's hero (0x4205d0, which sets the old hero back to AI with 0x41da60 and the new one to
player control with 0x4204f0). Nothing on that path touches the member's move intent: its move goal (+0x180/+0x184
speeds, +0x1d0 goal, +0x1dc start), its follow/move-to handle (+0x3ec) and its navigator path (+0x3e8).

The manual hero switch (0x425500 -> 0x424030) clears that intent first (0x42bd60). On PC the control update's
player branch (0x434540) runs the click-to-move code: with no movement input and a move goal still set
(0x423ba0), it drives the hero through the AI's own move driver 0x424b70 (call at 0x434c83). So after a death the
successor finishes the walk its AI had started - when its AI was following a leader across a gap, into the gap.

## What the first game does

The first game's control update (0x439a0) runs the move driver (0x35370) only in its AI branch; the player branch
(0x3bad0) reads the pad and nothing else. A newly controlled hero never continues an inherited goal.

## The fix

One call. The CALL 0x420860 at 0x42a042 calls a wrapper with the same cdecl ABI, which runs the native hand-over
first and then, when it produced a living, player-controlled hero of that very controller, clears that hero's move
intent the way 0x42bd60 does: +0x3ec = 0, the move goal (0x421b90), +0x820, +0x828, +0x82c and +0x808. It keeps the
target (+0x3e0) that 0x4204f0 may just have handed over from the dead hero, as the manual switch keeps it; the target
moves no one (the player branch steers toward it only in the +0x808 mode, which is cleared). A party wiped out
(null handle), another controller's hero, a hero still AI-controlled or a dead one is left as the game made it.
Each clear writes one log line.

The installer checks the call site (0x42a034..0x42a04a), the prologues of 0x420860, 0x421b90 and the resolver
0x4654b0, then rewrites only the call's rel32 through the shared install transaction (write, read back, roll back).
A mismatch or a refused write leaves the native call and logs one line; only a failed rollback refuses startup.
The other callers of 0x420860 (controller join and re-seat, 0x48ea20 and 0x48ecf0) are untouched. Nothing is saved.

## Multiplayer agreement

Online play is lockstep, so every peer must hand over the same way: shared [gameplay rule](gameplay-agreement.md)
15, revision 1, `{installed ? 1 : 0,0,0,0}`. Absent/off registers verified `{0,0,0,0}`; requested but not installed
registers unverified, which closes hosting and joining. A requested key enlists the agreement in any installation.

## Verification

Build for Win32 and run `xml2_death_switch_test` (optionally with a retail XMen2.exe path, read only, to check every
guard against it). It links the production installer and hook against isolated settings/logger/startup providers, on
the retail addresses in a fixture section of its own, and checks the rules, the absent key, a wrong image, each
guard's refusal (one log line, nothing written), the one rel32 write, the hook's clear and everything it leaves alone.
In-game acceptance (a successor's movement read through the test pipe) belongs to the integrator.
