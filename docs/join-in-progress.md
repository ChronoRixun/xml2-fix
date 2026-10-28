# Join in progress (`[Online] JoinInProgress=1`, gameplay rule 26)

**ON by default in Legends Classic 0.3.0** (the builder writes `JoinInProgress=1`). The generic DLL remains
requested-only: absent or `0` writes nothing and retains the capability record of a build without it.
Local three-instance tests have exercised New Game and a copied saved campaign: the late player waits, the next
area restores one live campaign on every peer, and 60 seconds of real newcomer input retained matching native
checksums and hero transforms. Native menu leave returned the hero to AI and a fresh identity took the reopened
seat at the following area. Validation used a three-hero New Game fixture and a four-hero saved campaign.

Complete cancellation/recovery after a failed shared restore is unfinished; rehosting may be required. A joiner whose process dies is removed at one frame all remaining players
agree on (docs/gns-transport.md) and can drop in again; a host that dies ends the session. Owner acceptance of 030e on three real PCs, including a hotspot PC, remains pending; four-player WAN coverage and multiple simultaneous late admissions are not certified.
Host migration and automatic reconnection are not provided. Native LAG warnings may still appear, but the slow-player drop is disabled with rule 26 revision 6.

```ini
[Online]
Transport = gns        ; required (rule 25); rooms (Host=broker / JoinCode) imply it
JoinInProgress = 1     ; every PC of the session must set it (rule 26 JoinInProgress)
```

Without `Transport=gns` the key is refused by name in the log ("needs [Online] Transport=gns (rule 25 GnsTransport)")
and rule 26 is not offered. A peer without rule 26 is refused by the shared agreement with "26 JoinInProgress (not on
the other PC)" / "(disabled on this PC)".

## What it does

* **Start Game keeps admission and listing.** The host's Start Game no longer unregisters the join handlers (0x14,
  0x18, 0x19, 0x1a, 0x1e, 0x2c) and keeps the lobby tick; on the host only it keeps the GameSpy session, so the game
  stays on the master list with the real player count. Started joiners keep opcode 0x1f ("a participant joins").
  The engine's cap of four participants (the slot table) refuses a fifth as stock.
* **A late joiner is admitted as pending.** After Start Game the host's admission takes a slot and answers as stock,
  but the participant add (0x608350: the "Adding player" wait, 0x1f to everyone, the record marked active) and the
  newcomer's controller slot and "remote player added" notice are deferred. The newcomer waits in the native
  "Joined Game" screen. Participant creation also defers its earlier hero-seat activation/reconciliation
  (`0x612b25`), which would otherwise turn an AI hero human on the host alone before admission returns.
  Those native seat calls run at the area boundary after the controller slot is assigned.
  Everything in the lockstep that walks the participant records treats a pending record as
  absent: the per-frame gate (0x606ae5) and its resync state (0x606960), the broadcast's three modes, the stall
  watchdog and the input manager's two walks (live: host and first joiner keep playing in sync while the late player
  waits).
* **Bring-in at the next zone load.** The synchronous area boundary at `0x484def` latches the transition and runs
  the native add before loading. Pump polling alone misses this interval. Controller allocation and the native
  player-added notice run at this boundary. Mesh completion preserves both native topology and `0x25` replies.
  Once the host has loaded, its serializer (`save_format::serialize_wrapper`) supplies one canonical live image to
  the native campaign sender, which streams `G/H` to every remote participant. The consumed serializer cursor is
  rewound before the host replays its own wrapper; receivers initialize that cursor separately. New Game mode 1 is normalized to full
  campaign mode 2; the native saved-stats-only flag is cleared. Native `J` completion broadcasts one `0x26` start.
  Existing participants and the newcomer replay the same image, with the existing participants resetting their
  reused network/loading state first. After their restored world completes, the native controller-resume function
  enables local input again. Each cohort restart clears the native clock's pending and fixed frame steps before
  campaign setup installs its 30 Hz step. Native time reset alone preserves a previous input frame's pending step.
  The start is deferred out of packet delivery; its sender is retired before
  world reset to prevent a nested tick from broadcasting another start. Checksums are held during this cohort
  restart and resume only after observed native loading and the first fresh shared input frame (the native frame
  gate requires every participant). Stage limits are add
  45 s, image 90 s, load 120 s. A stage timeout is logged; recovery of a failed cohort is not live-verified.
  Rule 26 uses revision 6 (0.3.0d: seat owners only at solo segments) so this path cannot agree with earlier bring-in,
  clock, pending-seat or seat-owner behavior; a 0.3.0c peer (revision 5) is refused by name (`differs: 26`).
* **Seats.** A pending player who leaves frees the seat at once (the slot is the native one). The newcomer's hero:
  the native zone-start seat binding decides (standby when the party has no free hero). With Wolverine/Cyclops/Storm,
  participant 2 received controller 2 and Storm; with the saved Magma/Phoenix/Wolverine/Iceman roster it received
  Wolverine. Native leave handed Storm back to AI, and a fresh installation identity reused controller 2 at the next
  area. Tests read the native controller/AI fields and moved the assigned hero through actual input.

See `src/join_in_progress_rules.hpp` for every site and guard and `src/join_in_progress_core.hpp` for the state
machine (`xml2_join_in_progress_test`).

## One peer per participant (four players, 0.3.0c)

A joiner completes the mesh with every existing participant at once, one `0x1f` after the other, through the session
transport (`mesh_connect`: the native completion `0x607210` with `manager+0x221c` = that participant's index). The
stock code assumed one natneg at a time, and two other callers of `0x607210` bind "the participant being added"
(`+0x221c`, i.e. only the last one completed) to an address that need not be that participant's:

* the `0x25` mesh reply (`0x604e93`, call `0x604eb0`) binds it to the reply's **sender**, and frees the record's previous
  peer. Live (run Q1a, 4th player): join1's reply arrived between the two completions and rebound join0's record to
  join1's peer; join0's own peer was freed, join1's record was then bound to the same peer, and join0's datagrams
  kept creating peers no record held (the reaper `0x60f0a0` frees those every tick). join0's first `0x5b` barrier ack
  then reached `0x6116d0`, whose record lookup `0x611110` returned null: access violation at `0x614270`.
* the GameSpy natneg completions (`0x6051a6` result 2, `0x60524b` result 3) bind it to the natneg-reported **real**
  address, bypassing the room tunnel / GNS link (live negative control N2: join0's messages reached join2 from join0's
  real socket through a peer no record held).

The fix: a `0x25` from a peer some record already holds changes nothing; a `0x25` from another address does not rebind
a record that has its own peer (`decide_mesh_reply`); a natneg completion leaves a record that has a peer as it is (the
caller's `0x25` goes to that peer). Records without a peer keep the stock completion. The handlers of `0x5b`
(`0x6116eb`), `0x4b` (`0x60c4f2`) and `0x4d` (`0x60c598`) ignore a message from a peer without a record instead of
dereferencing null, and log the peers no record holds. The receive pump logs `MESH ERROR` if two remote records ever
share a peer. Nothing on the wire changes (this alone kept rule 26 at revision 5).

`[Dev] MeshDeferMs = <ms>` (development builds) runs a burst's later mesh completions that much later, so other
joiners' `0x25` replies arrive while the first participant is still "being added" (the race on demand);
`[Dev] MeshReplyLegacy = 1` restores the three stock paths and the stock `0x5b` null access (negative control).

## Solo story segments (rule 26 revision 6)

The first game seats one hero alone in story segments: Wolverine's New Game opening (`[Game] NewGameTeam`), Magma's
mansion tours and hub visits (`seatParty("magma", ...)`, 45 calls), Phoenix in the Muir brig, Professor X in the Astral
plane, Cyclops alone for the Havok boss and the old Weapon X flashback, Wolverine alone for the Weapon X flashback start
and the Mystique boss. Online, a player whose controller has no hero in the zone **stays in the session and watches**:

* Nothing in the lockstep needs that player's hero. Their input still travels (the frame gate is unchanged); the shared
  party camera follows the solo hero on every PC. What dropped such a player before was not the missing hero: the
  auto-advancing XML1 conversations (see "Conversations online" in the CHANGELOG) changed lines on different frames.
* The watching PC shows the game's own timed network message "Solo story segment - you rejoin when it ends" (6 s,
  repeated every 30 s) and logs `solo segment: no hero for this PC's player ...` once per segment.
* When the story widens the party again (the next `seatParty`/`popParty` with room, `joinHero`, or a level with more
  heroes), the native zone-start seat binding hands heroes out in slot order. Every PC tracks the segments from the
  bindings themselves (the zone load's `0x486d3b` and the zone start's refresh `0x422618`, both `game vt+0x188`): a
  segment starts at the first binding that leaves an active player without a hero and ends at the next binding where
  every player holds one. The memory is the heroes the players held when the segment started: `seatParty` (the
  first game's way into a segment) notes the live owners before it reseats the party - each party hero's controller
  (+0x3d4) unless its AI bit (+0x3d8 bit 0) is set; script time, the same frame on every PC - so a hero switch made
  just before the segment counts. Without such a note the last wide binding's owners are used. (The heroes' stored
  controllers, hero registry `0x44b8f0` vt+0x40 / record +0xc0 vt+0x38, are no use there: `seatParty` has already
  reseated them by the time the zone loads - live G01.) Inside the segment the solo hero goes to
  the player who had it; in the zone where it ends (its load binding and the zone start's refresh, which the native
  `vt+0x184` then applies to the heroes) each remembered hero is swapped back to its player.
* A normal level change (no segment) changes nothing: the native binding already keeps the heroes the players hold,
  hero switches included. (0.3.0c handed every level change back to the owners of the last wide zone's start - the
  owner's three-PC test, mess hall: the players' d-pad switches were undone at the level change.) The tracker is
  cleared when the participant count changes, so a newcomer agrees.
* Live (two local instances, rooms, copied save in nyc1_1_4, the story's own calls staged on the same native frame):
  Wolverine/Cyclops -> Magma alone (mansion1a_1, subbasement1a) -> Cyclops/Iceman/Storm/Wolverine: 60 matching native
  checksums, nobody dropped, the host has Wolverine and the joiner Cyclops again, Iceman and Storm AI.

`[Online] JoinInProgress=1` is required (the watcher message and the owner swap live in this rule).

## Host alone, keep hosting (rule 26 revision 6)

* Start Game alone: the host menu (0x5bacd0) refused a room list of one (dialog 0xffc, `cmp [list+0xc], 2` at
  0x5bae34); with rule 26 the host starts alone, plays, stays listed, and players drop in as above. Live: host alone
  ~2 min with a level change, joiner 1 admitted mid-level and brought in at the next level, then joiner 2 the same way:
  every position identical on all PCs, heroes Magma (host) / Phoenix (joiner 1) / Wolverine (joiner 2).
* Keep hosting: 0x612c40 ended the whole session when a participant was lost from a session of two (count test at
  0x612d92); with rule 26 the host removes only that player and keeps the session, so a replacement can drop in.
* The native slow-player guard (0x559190, counters at 0x79fb80, a drop at 900 slow frames) only resets its counters
  after a drop; they are cleared when a restored cohort is synchronized.

## Slow players stay (rule 26 revision 6)

The host runs the game's slow-player guard (`0x559190`, host only, counters at `0x79fb80`). Every PC puts its own last
frame time into each input record it sends (`0x558f90`: the main timer `0x401b00` vt+0x24 = timer +8, the time since
the previous frame, x 10000, capped at 1 s; bit 15 set while the PC is in a menu, read as 25 ms). Each simulation
frame the host walks the participants in index order and counts a slow frame for the first one over 50 ms and any
later one that is slower still (300 good frames in a row reset that count,
1,800 the stage); at 200 the game shows `PLAYER_LAG1`, after 600 more `PLAYER_LAG2`, and after 900 more it dropped
the player (`0x6133a0` at `0x55933c`). The count never decays during a steady slowdown, and when the lockstep stalls
everyone alike the host's own index is counted first. Over a few minutes a PC near 20 fps (or a one-PC test rig, B04:
the host dropped itself at frame 2755, every count in stage 2, checksums equal to the end) is dropped and the session
ends for it ("Player has been dropped from the game session" / "You have lost connection to the host"). The owner's
three-PC test (0.3.0c) lost a joiner this way after a hero switch. With rule 26 the drop call is replaced: the
player stays, one log line names it, the native warnings and the native reset after the drop stay. A frozen or
vanished player is still removed by the game's own timeout and the GNS link checks.

The online ready roster (including Xtraction) labels a participant without a held
hero as `Player (Spectating)` instead of leaving empty parentheses. `Player` is
the participant nickname; the parentheses contain the native hero display name.
The current hero name resumes as soon as the story gives that participant a hero.
The formatting call at `0x5d95bd` is guarded and installed with JoinInProgress;
this display change does not alter hero ownership or the shared gameplay rule.

The synthetic `xml2_union_host` also accepts `UNION_COOP_PROBE=on` with
`Transport=gns` and `JoinInProgress=1` in its fixture INI. It executes the installed
DLL's roster wrapper through the real native formatter and both GNS removal
entry bridges on synthetic records, checks stack balance, and exits without a
game session. Use `UNION_COOP_PROBE=off` for an absent/zero JoinInProgress key or
for the guard-refusal case (`UNION_MUTATE_VA=5604f0`). Outputs should show zero
fixture failures, and the ON log should include the synthetic notice reasons
`0x10ce/0x10cf`, flag 1 and the removal reason `0x10ce`. As with other union runs,
use a private desktop and compare registry snapshots.
