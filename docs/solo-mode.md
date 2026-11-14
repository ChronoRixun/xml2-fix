# Solo mode (`[Game] SoloMode`)

X-Men Legends I's solo mode for a mod's own campaign (the Legends Classic port, issue #5). XMen2.exe keeps the
input action and the voice lines (`snd_solo_begin` / `snd_solo_end`) but dropped the mode itself.

```
[Game]
SoloMode = 1   ; the port's builder writes it; absent or 0: nothing patched, setSoloMode does nothing
```

## What the first game does (default.xbe)

- A solo box at a gap (`trigger_solo`, touched by the leader) calls `enterSoloMode('_ACTIVATOR_')` (0x9eef0); four
  moving-bridge scripts call `exitSoloMode('_ACTIVE_HERO_')` (0x9ef70). Each checks the character's solo bit
  (character+0x334 bit 1, getter 0x2e030), runs the party toggle 0x33a70 and plays the character's voice line
  (0x31860 / 0x318b0: voice ids 0x13 / 0x14).
- The toggle walks the whole party list (game vt+0xe0, 0x72470: every seated character): solo is party-wide. Entering
  is refused in the Danger Room's free-play mode 0xfd (byte 0x4e7ab0, 0xbf750; XMen2.exe keeps the same state byte at
  0x782728), with more than one player (the session's player count) or a party of one. A character built later starts
  without the bit (constructor 0x33cd0); nothing is saved.
- While solo, the AI heroes skip their follow work: the hero idle handler (0xf3410) neither takes up formation behind
  the leader (it stops the follower, vt+0x14c) nor uses a bridge power for the leader (Iceman / Storm); the follow
  movement states (0x104d90, 0x105340) stop the follower and end; the idle handler's assist step (0xf2610) and the
  follow-me command (0xfb010) do nothing. Combat, damage and party membership are untouched.
- The world update (0x73420) regroups automatically: 1.5 s after solo began and then once a second it asks 0x34bb0
  whether the followed hero and every living party peer stand on the level rather than a live entity such as a moving
  platform, and whether every peer can reach the hero - the hero's navigator (CPath, character+0x340) finds a
  path to each peer within 200 steps (status 2) - and leaves solo when it can.

## What the fix does (XMen2.exe)

The same tests, at the PC counterparts of those functions, in one transaction (guards, writes, read-back, rollback;
any mismatch: one log line, the native AI):

| PC site | Function | default.xbe |
|---|---|---|
| 0x51ee14 | hero idle handler 0x51ea70, formation follow: solo takes the existing stop branch (vt+0x154) | 0xf373e / 0xf374d |
| 0x51ebd3 | same function, the AI's bridge power for the leader: skipped while solo | 0xf3523 / 0xf3536 |
| 0x50b8c9 | follow movement 0x50b7c0: stop and end | 0x104ea0 / 0x104eb0 |
| 0x50c29e | follow movement 0x50c210: stop and end (the PC has no test here at all) | 0x1053d1 / 0x1053e1 |
| 0x51dc10 | hero idle handler vt+8 (assist step): returns 0 | 0xf2610 |
| 0x526960 | follow-me command (AI manager vt+0x1c0): ignored | 0xfb010 |
| 0x50f598 | the hurt hero's call for help 0x50f520: skipped for a solo character | 0x1079ac in 0x107920 |
| 0x4e514c | a teleport combat event's success (0x4e4c20, CCEAtkTeleport): a player's teleport whose event sets CheckSolo (character+0x730 bit 0x200, parsed at 0x4e3b50) starts solo when the first party peer can't reach the hero (regroup test with manual 0, all 0) | 0xd5be5 in 0xd56b0 |
| 0x425982 | controller update 0x425500: the hero's own selection button (NextHero / PreviousHero / hero aggression rows, i.e. the D-pad) held past 9999 s toggles solo (the shipped first game compares the hold with 9999.0, 0x3c97cc: its manual toggle is effectively unreachable, a tap still switches heroes and the SOLO action is the follow-me command); leaving needs the manual regroup test (every peer reachable and on screen: CCamera vt+0x178, margin 150) | 0x36011 in 0x359f0 |

XMen2.exe has no free solo bit (its +0x3d8 byte reuses bit 1), so the fix keeps the generation handles of the members
the toggle marked. Scripts reach it through `setSoloMode(name, on)`, registered with the forced parties' functions
(`forced_teams.hpp`; the builder rewrites the first game's two calls to it). `xml2fixFeature("solomode")` is 1 when the
sites installed. The automatic regroup runs the original schedule on the game clock from the AI sites (once per game
clock value) and uses the hero's own CPath (vtable 0x689b78; query 0x496280 with the PC's extra argument 1, which
skips the far-path lookup XMen2.exe added, leaving the first game's near search).

The floor check precedes navigation for the followed hero and each eligible living peer. The floor handle is at
actor+0xbc (default.xbe +0xc0). A null or stale handle proceeds to navigation; a valid handle must equal the current
world's level handle. World vt+0x24 writes that level handle to its output argument; it does not remap the supplied
floor handle. The PC accessors are 0x4654d0 (validity) and 0x45fe70 (level handle), so both retail and raised-capacity
worlds use their own virtual methods. Their retail wrappers and the actor floor field are guarded at installation.

This prevents the Forbidden Halls moving-platform puzzle from ending solo mode on the first 1.5-second check and
sending AI followers into the gap. The original trigger starts solo when the leader crosses the gap; the switch's
bridge script removes the moving platform and ends solo after the bridge forms. It does not teleport the teammates.
The same entity-floor rule applies to every solo area, including CheckSolo teleports and manual regroup.

`[Test] SoloTrace=1` writes one line per automatic regroup check to xml2-fix.log: what refused it, or each peer's
navigator status (0 failed, 1 still searching, 2 reached, 3 already there, 4 blocked). Refusal 5 means the followed hero
is on a live entity other than the level; refusal 6 means an eligible peer is.

Online: shared gameplay rule 8, `{installed, 2, 0, 0}` ([gameplay-agreement.md](gameplay-agreement.md)). Revision 2 restores
the floor checks; it refuses revision 1 in both directions. Verified off stays `{0,0,0,0}`. Entering is refused with
more than one human player, as in the original. This is a DLL change and requires no content rebuild.

`xml2_solo_mode_regroup_test <retail XMen2.exe>` executes the production predicate with synthetic actors and native
service stubs: leader/peer platform refusals, world/null/stale floors, peer eligibility, path failures, CheckSolo's
first-peer behavior, install guards and old/new online-agreement refusal.

## Not ported (unproven PC counterparts)

- The HUD's "solo" text (string ids 0x9d / 0x9e in the first game) - the voice lines play.
- The HUD's per-hero solo marker (0x1588c0) and the automap's solo mark (0x72150).
