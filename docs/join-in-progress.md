# Join in progress (`[Online] JoinInProgress=1`, gameplay rule 26)

The current release fallback keeps `JoinInProgress=1` and defaults **`LateJoin=0`**.
Before START the native lobby admits normally. After START the host refuses a
new request before allocating a participant, controller or hero seat. Legends
Classic shows: "This session has started; ask the host to return to the lobby".
Solo spectators, hero ownership restoration, slow-player protection and host
lifecycle remain enabled. `LateJoin=1` explicitly enables the experimental
next-area restore described below. Every peer needs implementation revision7;
older revisions are refused. LateJoin is the HOST's admission choice, not a
simulation parameter: guests do not need to mirror it. The Host setup DROP-IN
row changes this choice with keyboard/pad Accept or a mouse click before posting.
It updates the current process, leaves the INI startup default unchanged, and
never rewrites the frozen gameplay agreement. JoinInProgress=0 keeps the module
off; enabling that module still requires configuration and relaunch.

## AR5 diagnostic candidate (both switches default OFF)

For the lost sole newcomer investigation, `[Online] JoinDiagnostics=1` installs a
bounded observer. `[Online] JoinStallWrapper=1` independently enables the experimental
stalled-render wrapper. Both require the installed JoinInProgress module. Use the
same settings on every surviving peer for an A/B round; neither changes gameplay
rule 26's revision or the release identity. An absent key or `0` leaves the option off.
With both off there are no additional raw-poll, input, stalled-helper, menu-query,
PDA/pause, or RNG observation hooks or allocated capture buffers.

The observer retains the first 16,384 boundary samples and 32,768 draw records per
captured interval, with explicit omission counts. It writes `jip execution`,
`jip execution camera`, `jip execution input`, and `jip execution rng` lines to the
existing log at session end or restart replacement. Exit the session normally to
flush it; terminating the process cannot flush an in-memory capture. Records carry
the outer raw-poll iteration, local restart generation, native gate frame/flags/phase,
run permission, applied and retired input observations, world-call count, synchronized
seed/count, independently counted visual draws, raw camera eye/target/basis, game-time
bits, and clock ticks/steps. `readable & 1` means the full snapshot was read;
`readable & 2` means effective ticks could be reconstructed from the paused/fixed-step
clock fields. The observer never calls a lazy getter to sample time or camera state.
Event-head digests and pointers are local observations, not cross-PC identities.

The `video` field uses bit 0 for a validated video object/predicate layout and bit 1
for active video. Participant-ring indices, each record's controller field, logical
controllers and mapped controller objects are labeled separately in input lines.
With JoinDiagnostics and the legacy LateJoinProbe both enabled, draw output uses
the deferred `jip execution rng` ring; legacy touch formatting is bypassed on draws.

The protected generation begins at the authorized deferred late-restart entry,
observes the mode-2 load return, and becomes world-ready only after raw zone loading
bits and the native start menu clear. It survives cohort bookkeeping and the
late-restart marker clearing at frame 0, and has no frame-number or timeout cutoff.
An unrelated area load or validated session end clears it. The wrapper passes
through loading, all current menus or pauses, active video, unknown gate states, and contradictory
run permission. No early ESC result is filtered and no input or pause state is cleared.

This candidate implements the design's smaller, permitted **render-tail subset**.
At the original `0x4021b7` CALL it runs the native helper, preserving first-stall
feedback, seat service, filtered controller-continuation/save-load commands, manager
service and message selection. At `0x402a09`, after those services, it reads eligibility
again. Only world-ready input waits with flags exactly 1, phase 1 or 2, no modal/load,
and input permission 0 bypass the video/menu/HUD/draw/presentation tail through the
helper's existing non-render epilogue `0x402b50`. The native caller then reaches its
common cleanup unchanged. A protected wait yields `Sleep(1)` once after native
service; the simulation clock is never used for pacing or advanced to escape a wait.

The seat manager and native manager tick are **not certified inert**. This subset
does not establish the complete gameplay-freeze invariant or prove a deterministic
recovery fix. All modal states keep native behavior; PDA/pause callers and observed
menu replacements are logged, but complete Team/PDA provenance is not certified.
Live paired traces must identify the first differing writer and show whether the
subset removes it. A missing PDA alone is insufficient: compare consumed input,
clock steps, synchronized draws, camera vectors, and actual continued world updates.

The 060x saved X-Mansion late-join round failed twice at the first checksum
after restore, despite earlier three-instance coverage. No complete restoration
fix is certified. Recommend `LateJoin=0` for 0.6.0-beta.1. The builder writes that
default and refuses a companion DLL missing `LateJoinPolicyV1`. Rebuild the LC
Code/Browse notice resources together with the DLL.

Room discovery remains available for the explicit native refusal. It does not
grant admission; the host checks its current START state at request delivery,
including a request racing START or stale room metadata. The tagged native
`0x1c` refusal uses the existing variable body. Menus accept its exact marker
only from the current attempt's proven host; ordinary/foreign refusals retain
the native path. Cleanup runs on the next UI tick, outside packet delivery.

Earlier coverage (historical, with late joining enabled):

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
LateJoin = 0           ; safe default: admission only before lobby START
; LateJoin = 1         ; experimental shared restore; host setting / Host setup DROP-IN row
JoinHoldSeconds = 10   ; p060fix/AQ/AQ2: hold the host (from the zone load that admits a newcomer) and existing peers while it gets the image (0..15, 0 = off)
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
  The shared all-loaded query also runs in Team Management. It must preserve pending players unless the actual
  synchronous area boundary has latched; opening Team Management is not a bring-in boundary.
  Its loaded query excludes pending records from the barrier without writing their grid entries; every existing
  participant must still report loaded before Team Management can proceed.
  Closing Team uses a separate not-ready check (`0x60c200`): pause entry sets flag `0x10` on every record,
  including pending players who never enter Team. The host's pause scheduler (`0x60aaec`) and Team's two action
  checks (`0x5e4b4e`, `0x5e4c21`) exclude pending records. Loaded solo-story spectators with no human hero
  also cannot hold this Ready barrier. Watching participants are sampled during
  stable gameplay for each participant index, and that cohort is frozen while
  paused. A temporarily empty seat in Team's hero picker cannot create a new
  spectator exemption; active participants still must Ready. A watcher who has
  a hero again is no longer exempt. This changes no participant flags or
  loaded grid entries. Area loading, cohort restoration and pre-start checks retain their native barriers.
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
  Revision 6 introduced the 0.3.0d seat-owner behavior below; current revision 7 also provides the host admission gate and control, so this path cannot agree with earlier bring-in,
  clock, pending-seat or seat-owner behavior; a 0.3.0c peer (revision 5) is refused by name (`differs: 26`).
* **Seats.** A pending player who leaves frees the seat at once (the slot is the native one). The newcomer's hero:
  the native zone-start seat binding decides (standby when the party has no free hero). With Wolverine/Cyclops/Storm,
  participant 2 received controller 2 and Storm; with the saved Magma/Phoenix/Wolverine/Iceman roster it received
  Wolverine. Native leave handed Storm back to AI, and a fresh installation identity reused controller 2 at the next
  area. Tests read the native controller/AI fields and moved the assigned hero through actual input.

See `src/join_in_progress_rules.hpp` for every site and guard and `src/join_in_progress_core.hpp` for the state
machine (`xml2_join_in_progress_test`).

## Bounded checksum observations

`[Dev] LockstepChecksumAlways=1` (p060fix/AP, probe only, release default off) logs the native lockstep checksum's
producer sample (`jip checksum produced` / `jip checksum hero`) at EVERY native period of every session, lobby STARTs
included: the host of a lobby START never runs a cohort start, so before this nothing armed a window there. It arms
an observation window of no captured periods (epoch 0 until a cohort start; a cohort start arms its own window, a new
epoch, as before) at the first produced sample; the `jip checksum MISMATCH` line was already logged in every session.
Use it with `LateJoinProbe=1` on every peer to bracket a plain co-op combat desync.

Every full-campaign cohort start arms four native checksum periods, with a
180-second expiry. Initial saved-campaign START can also produce this control
sample. `jip checksum hero/produced/owner/head` logs report the inputs used by
retail `0x614a40`: each hero's six transform floats at `+0x20..0x34` as raw hex
bits and its native subtotal; both camera vectors and their subtotal; the RNG
draw count at `0x7ad8b4`; final hash and frame. The synchronized seed `0x6e0874`,
separate visual seed, controller/AI fields and input ring cursors are context.
Complete same-frame heads are observed before native arbitration clears them.

The seed is not directly hashed. Native network setup resets both seeds to
`0x845fed` and clears the synchronized count, so later divergent draws remain
distinct from a missing initial seed. The observer calls no world/camera
getter and consumes no random value. It preserves integer registers/flags,
x87 stack/control, XMM and MXCSR, and changes no native simulation state,
checksum, hold or arbitration decision. The self-drop caller probe covers
host and guest PCs, including target indices 1-3; target index alone does not
identify which PC is local.

Offline verification: `xml2_join_in_progress_boundary_test <retail-exe> checksum`
executes the native producer under invented services, compares its wire before
and after the observer bridges, and verifies floating-point/world/RNG
preservation and the four-period limit. Mode `admission` executes the actual
native handler with an int9 control and the production policy gate. Neither
mode starts the game or a socket. Live first-period cause remains unproven.

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

## Two-PC follow-up: outbound handoff

The supplied two-PC host retained and bound the joiner's native record but never
logged its GNS send switch. The old adapter let every new packet refill the native
reliable queue while requiring that queue to become empty before switching.
Continuous campaign/input production can prevent that observation indefinitely.
Bound links now stage NEW queued and direct game messages in the adapter, drain
only the finite old native reliable queue, then send the staged messages in order. Transport ACK/sequence receipt and ping/reply
service (0x0a/0x0f/0x10/0x11) bypasses the held gameplay lane so two peers
can drain each other; it does not advance simulation state.
Native receive, transmit and fragment backlogs all remain counted during the
transition. No reliable message is discarded and no premature switch is forced.

A failing-first continuous-production test starved the old GNS stream; the fixed
model delivers all90 staged input frames, beginning at0, only after old native
ACKs. A separate actual retail broadcast test sends60/60 input records to the
admitted peer after cohort synchronization/seat cleanup. It confirms the expected
native send set without inserting duplicate records or guessing active flags.

The late-drop line saying records were sent to0 participants is the LOST-INPUT
relay to other survivors after a peer leaves. Zero is expected with two players.
GNS counters also exclude tunneled native datagrams. They establish the missing
GNS handoff, not that every native game packet was absent. New reports include
bound/switched/native-send-pending; four-period owner snapshots include active,
pending and native-send backlog. The earlier same-frame checksum variant still
requires live component comparison; LateJoin=0 remains the release recommendation.

## Join hold: "A player is joining..." (`[Online] JoinHoldSeconds`, p060fix/AQ + AQ2)

Owen 2026-10-31: hold the host instead of letting it play on and then replaying those seconds after the restart.

**The problem.** A late admission's image is captured at the zone load (`start_image`). Before AQ the host kept
simulating alone until its own cohort restart while the image streamed to the newcomer. The restart reloads every
game byte from the image, so the host REPLAYED those seconds (deja vu), and any per-process state outside the image
that changed in that window differed on the host alone. Example from live-060zg ZG-C-3Pp2: 4.36 s of game time, 493
draws and seven hits ran on the host only. AP's NPC power slots were one such case (npc-power-rule.md item 23).

AQ held from the image capture. live-060zi (ZI-1b, ZI-1bp) showed a second, earlier host-only window: between the end
of the zone load that admitted the newcomer and the capture (the native add's wait while the mesh connects), the host
ran its world alone: ZI-1bp +55 synchronized draws and +1.0 s game time at the same native frame 1913 (host 3753 vs
lobby guest 3698 at the hold opening; both were 3698 at the zone load's end); ZI-1b +25 draws, +1.11 s. AQ2 closes it.

**The hold.** The game update's world section is skipped. That section is 0x46f0ca..0x46f60d inside 0x46eef0, behind
`test byte ptr [esi+0x728], 2` at 0x46f0a9. It contains:
- the timed-event pump, so every actor update and AI think;
- countdowns, physics and collision;
- the camera update;
- the dormant-entity update (0x46f2df).

A CALL-shape bridge replaces the 7-byte test (call rel32 + 2 nops). With the hold off it runs retail's own test. With
the hold on it returns ZF=1, the flags of the test with the bit clear, so the site's `je 0x46f60d` skips the section.
Each skip is counted. The game update has one caller, the main-loop iteration (0x40235a, game vt+0x16c; the two other
vt+0x16c call sites, 0x5a7f13 and 0x5abd3c, are three-argument methods of other classes).

**Clock and native frame (AQ3).** LoadZone/LoadMap pause the clock through vt+0x1c (0x55b3d0). The main loop then
unpauses it at 0x402345..0x402350 through vt+0x20 (0x55b410), before the update samples effective time into
game+0x3e8 at 0x46f079. AQ2 therefore froze draws but resumed time. A guarded replacement of that complete 12-byte
unpause block retains the native load pause while held; it executes the original getter/unpause otherwise. Raw
ticks through vt+0x14 (0x55b240) continue, while vt+0x34 (0x55b590) reads the saved effective time. Native unpause
compensates elapsed raw ticks on release, so there is no time jump. Wall-clock/network service stays live. The input scheduler copies
its next buffered input frame into network+0x1c at 0x606ada; that counter can move by one even with no world update.
Capture diagnostics distinguish that bookkeeping from a simulation step: draws and game time must stay fixed,
and `capture clock proof` reports deferred unpauses separately. The fixture executes all four retail clock
routines at 30/60 Hz: retail unpause fails the freeze assertion, the installed bridge passes, and release resumes
by exactly the next step.

What keeps running, so the window stays alive and never shows "not responding":
- the main-loop iteration 0x401d70 with its message pump;
- the scheduler 0x401f74: the network tasks, the native image sender, this module's tick;
- the input manager update 0x40219e;
- rendering, which draws the frozen scene, and Present, where the fix draws the message (below).

Who holds, and from when:
- **The host: armed at the admission, open at the zone load's end (AQ2).** The admission is the native add `run_add`
  at the synchronous area-load boundary (0x484def `call 0x611910` inside the zone loader 0x484ce0, after its
  `test byte ptr [ebp+0x220], 2` pending-load check and before the load): `area_barrier_bridge` -> `guarded_advance` ->
  `advance` -> `run_add` -> `hold_arm`. From that call the world section is skipped on the host. The load itself is
  synchronous (no pump runs during it: the 0x1f the add queued at 03.621 in ZI-1bp left from the pump task at
  04.987), its spawn draws happen in the loader on every peer alike, and every game update after it is held. The hold
  OPENS at the first pump after that zone load (`hold_tick`: the zone flag is clear): its clock, cap, opening line
  and message start there, so the zone load (disk time, 0.86..2.30 s measured) is not charged to the cap. While armed
  the cap does not apply; `join_hold_armed_limit_ms` = 30000 bounds a zone load that never ends, and every newcomer
  gone (its add given up, or it left) ends the armed hold at the next pump. A second newcomer admitted at the same zone
  load joins the armed hold at its own native add (`player N joins the hold`); its image rides the first one's
  transfer as before. The capture (`start_image`) then logs the proof line `join hold: image captured while held, N
  ms after the opening (K world update(s) skipped since the admission); frame F -> F, synchronized draws D -> D, game
  time T -> T - no simulation step between the zone load and the capture`.
- **Every existing participant (AQ3)** arms at its own synchronous area boundary, on the host's reliable admission
  decision for that same native frame. The host sends GNS control kind 10 directly before the native add/load:
  `[10, frame-low, frame-high, newcomer-seat-mask, 0xa3]`. Kind 10 avoids the room's party (7/8), removal (9), and
  restore (16) lanes, enforced by a compile-time collision check. Each guest waits for the proven host's reliable
  decision before entering the loader. GNS service receives the control and queues gameplay without native
  parser/drain dispatch, world updates, or simulation-clock advances. A zero mask is an ordinary load: it skips no
  world section. `hold_playing` is set at the shared input gate and cleared at restart/session end, so initial and
  cohort-restart loads never wait. Decision queues are bounded to eight frames, matched exactly (including 65535
  and 0), consumed once, and cleared at restart/session end. A missing decision releases within cap + 4000 ms.
  Members retain that absolute admission safety deadline across decision wait, load, and opening; an expired hold
  cannot reopen at load end. The ordinary cap still counts from opening, subject to that absolute safety bound.
  A host with the hold off still publishes zero-mask decisions, so enabled guests never wait out an ordinary load.
  The guest opens at its own load's end, with the newcomer count from the admission mask. The later 0x47 never
  renews an admission hold or its cap; the old 0x47 opener remains a fallback when no admission boundary was
  attempted. Every opening still logs draws/time since the local load's end.
  This exchange is necessary: ZJ-2a queued native 0x1f at 02:09:16.064 but only sent it after loading at 17.505;
  the guest received it at 17.567, after its own load ended at 17.525. Sending earlier alone cannot guarantee that
  a guest already inside its synchronous loader will receive the notice before its next world gate.
- **Never** the newcomer (it is loading the image), a lobby START (no marker), or `JoinHoldSeconds=0`. With 0 the
  gate is not even written and the admission arms nothing.

How it ends:
1. **This PC's restart begins.** `deferred_campaign_start` ends the hold first thing, before any native call. Log:
   `join hold: N ms, M newcomer(s) ready - this PC's cohort restart begins (K world update(s) skipped while held;
   frame F -> F, synchronized draws D -> D, game time T -> T)`. Equal draws mean no draw ran during the hold.
   On the host, the newcomers are ready when no admitted seat is still `adding`/`streaming`, i.e. every image is in.
   That moment is logged as `join hold: every newcomer is ready after N ms ...`. The native start then waits a fixed
   1 s inside the image sender task (0x60814b: `0x60a920(1.0)`, a blocking network wait) before 0x5f3c20. The hold
   covers that second too, bounded by `join_hold_restart_limit_ms` = 4000 (`... did not begin within 4000 ms`).
2. **The cap.** A newcomer not ready `JoinHoldSeconds` after the hold OPENED (the zone load's end on the host) ends
   it: `join hold: cap reached after N ms (R of M newcomer(s) ready) - this PC simulates again until the restart, as
   without the hold; the newcomer is not refused`. Nothing else changes: the seat, the transfer and the restart
   continue as before. An existing participant cannot see the seats, so only its restart or its cap ends its hold. A
   member whose host died is released by its cap, and the host-left path (5003) runs as before.
3. **Every newcomer of the hold left** (seat freed): `join hold: ended after N ms - every newcomer of this hold left`.
   A killed joiner's seat is only freed when the native removal runs, about 20 s later in ZD-JK/ZF-JK, so the cap
   ends that hold first.
4. **The session's end** (host Quit, back to the front end): `join hold: ended after N ms by the session's end`.
5. **A zone load that never ends** (armed only): the host retains its 30000 ms bound; a guest uses cap + 4000 ms,
   including the boundary decision wait. The host line is: `join hold: ended after 30000 ms - the zone load that admitted the
   newcomer did not end within 30000 ms; this PC simulates again`.

**The screen (AQ2/AQ3).** The fix draws the text itself, on the host and every existing participant: `A player is
joining...`, or `2 players are joining...` / `3 players are joining...`. AQ used the game's timed network message
(0x60a600); it never showed in a hold (live-060zi: 4 + 12 captures on host and lobby guest, frozen scene and HUD, no
text, no failure line). Its getter 0x60a5d0 is read by the main-loop stall screen 0x402920 (entered only when
0x606880(3) reports a stall, 0x4021b7) and by the menu layer 0x59f7d7, which only moves a panel by it; so it needs a
path the hold does not reach. Now:
- `display::set_overlay` (registered at install when the hold is on, before `display::install`, so Present is hooked
  in every display mode) calls `hold_overlay_draw` before every Present, ahead of the test pipe's screenshot hook.
- `hold_overlay::draw` (src/hold_overlay.cpp) renders the text once per text and back-buffer size with GDI (Arial
  bold, 1/24 of the back-buffer height, antialiased) into a panel: dark blue (0x101830, ~80% opaque), a thin blue
  border (0x5a8cd8), white text, centred horizontally at 30% of the height.
- **Blend path** (normal): CopyRects the panel's rectangle from the back buffer into a system-memory surface of the
  same format, blend on the CPU, CopyRects it back. Formats X8R8G8B8, A8R8G8B8, R5G6B5, X1R5G5B5, A1R5G5B5. No render
  state is touched.
- **Clear path** (a multisampled back buffer, another format, or a refused CopyRects): `Clear` over the panel, its
  border and the text's pixel runs (opaque), with the full-back-buffer viewport set and the engine's restored; only
  when the current render target is the back buffer.
- Logs: `join hold: message shown ("A player is joining...", blend path, panel WxH at X,Y)` at the first drawn frame
  of each hold; `join hold: message removed` when the hold ends (the next frame is drawn without it);
  `join hold: the on-screen message could not be drawn (<step>: <hresult>) - the hold itself goes on` once per hold.
- While armed (inside the zone load) nothing is drawn; the load screen shows. The newcomer draws nothing.

AQ3 replaces the stall loop's message-getter call at 0x4029f0. While the hold message is open it returns that nonnull
text, selecting retail's render branch; otherwise a null getter plus a clear forced-draw byte jumps to 0x402b50
and presents nothing (ZJ-2a/2b). The panel still draws through the same Present overlay, before screenshot capture.
The native getter is called unchanged when no hold message is open. The guest's count comes from the host's
admission mask; only the legacy fallback uses mesh completions.

**The cap's default: 10 s (Owen, 2026-11-01).** Owen: "should likely be 10 for a default just to be safe" - a join
over the internet can be slower than the LAN evidence. `[Online] JoinHoldSeconds` stays 0..15, 0 = off. With the AQ2
opening the cap covers the zone load's end -> every newcomer ready (the host's `cohort start announced`), measured over
every late admission in the live-060zd..zi host logs (p060fix-AQ2 scripts/measure_aq2.py; the zone load's end is the
host's `world back faces: zone` line after the admission, which the first pump follows within ~0.1 s, so these are
slight overestimates):
- 67 admissions where the joiner lived (2 duplicate logs and 2 runs without a zone-load line left out); the two
  joiner-kill runs (ZD-JK, ZF-JK) are the cap's case;
- the added window (zone-load end -> capture): 22..59 ms with 2 peers (no mesh to connect), 1.10..1.40 s with 3-4
  peers (median 1.14 s);
- longest open -> ready 6395 ms (live-060ze ZE-4PE2p: 1123 ms + 5272 ms), then 6353 (ZF-3P-E2c), 6151 (ZG-4P-E2):
  3.6 s under the 10 s default (AQ's 6 s would have been 0.40 s short);
- an existing participant's window (its 0x47 -> its own restart) is shorter: at most 5277 ms (ZE-4PE2p) over 64
  member windows in the peer logs (scripts/measure_members_aq2.py), 4.7 s under 10 s.

Expected host hold time (the opening -> the restart, i.e. what the opening and end lines will show). The cap applies to
open -> ready; the margin column is 10 s minus the longest open -> ready of the row:

| Peers after the join | Newcomers | PCs | Runs | Added window min/med/max ms | Open -> ready min/med/max ms | Hold (open -> restart) min/med/max ms | Margin vs 10 s |
|---|---|---|---|---|---|---|---|
| 2 | 1 | one-PC | 9 | 22 / 27 / 59 | 3458 / 4328 / 4457 | 4464 / 5349 / 5463 | 5.54 s |
| 2 | 1 | two-PC | 3 | 28 / 29 / 59 | 4202 / 4225 / 4312 | 5215 / 5238 / 5324 | 5.69 s |
| 3 | 1 | two-PC | 32 | 62 / 1135 / 1232 | 3646 / 5309 / 6353 | 4652 / 6336 / 7370 | 3.65 s |
| 3 | 2 | two-PC | 4 | 1100 / 1233 / 1395 | 5430 / 5483 / 5594 | 6441 / 6503 / 6623 | 4.41 s |
| 4 | 1 | two-PC | 12 | 1105 / 1184 / 1239 | 4915 / 5721 / 6395 | 5932 / 6725 / 7407 | 3.61 s |
| 4 | 2 | two-PC | 6 | 1110 / 1150 / 1356 | 5529 / 5813 / 5947 | 6530 / 6817 / 6953 | 4.05 s |

Logs: `join hold: [Online] JoinHoldSeconds=10 - ...` at install (or `join hold: off ...`); on the host `join hold:
armed on this PC (host) at the admission of player N ...`, then `join hold: the game simulation is held on this PC
(host) from the end of the zone load that admitted the newcomer (armed N ms earlier at the admission; K world
update(s) skipped since): ...`, `join hold: image captured while held ...`; on a member `join hold: the game
simulation is held on this PC (existing participant) from the end of the zone load ...`; then `message shown`, one
of the end lines above and `message removed`. The duration remains a per-PC choice. Use the current DLL and enable
the hold on every existing peer for the shared admission boundary; `JoinHoldSeconds=0` on all peers retains the
original no-hold path.

Tests: `xml2_join_in_progress_test` (the `join_hold` state machine: 1 and 2 newcomers, ready before the cap, the cap
across the tick wrap, every newcomer gone, the session's end, the restart limit, members with 1..3 newcomers, off,
the clamp, the texts, the guards; AQ2: armed -> open, no cap while armed, the cap from the opening, the armed limit,
gone while armed, two newcomers at one zone load, the default 10). `xml2_join_in_progress_boundary_test <retail>
restart` covers:
- the bridge entered by CALL exactly as installed;
- the host's hold armed through the PRODUCTION admission (`area_barrier_bridge` -> `run_add`, natives stubbed) and
  opened by the production pump poll at the zone load's end, with every main-loop iteration from the admission to the
  shared start checked to run no world step, draw nothing and advance no game time (including the frames between the
  load's end and the first pump - the window the host ran alone in live-060zi), and the capture's proof line;
- existing guests through the production area boundary, including a delayed decision, 1-2 newcomers, and native
  frames 65535 and 0; no world/clock step through restart; a late 0x47 preserves count/deadline;
- the actual retail stall conditional branch reaching the fake device/Present panel on every held frame;
- ordinary/initial/restart loads, missing decision/host, armed safety, and no 0x47 deadline renewal;
- the old production `campaign_notice` fallback, with its `+0` since its zone load;
- the production pump poll and the production `deferred_campaign_start`, whose stubbed native campaign start records
  that the hold was already released and that no world step or draw ran since the admission;
- the production overlay (`display::set_overlay`'s hook) on a CPU fake Direct3D 8 device (test/fake_d3d8.hpp): drawn
  on every held frame and logged once, gone after the restart, the cap, a joiner leaving and the session's end; a
  16-bit back buffer, a multisampled one (the clear path, viewport restored), a render target that is not the back
  buffer (logged once) and a refused CopyRects.
`xml2_test` draws the message with `hold_overlay::draw` on a real windowed Direct3D 8 device (blend and clear paths)
and reads it back with the test pipe's capture.

## A newcomer lost before the shared start (p060fix/AR)

Live 060zj ZJ-7j/7j2 (host + lobby guest + late joiner killed right after the image capture, 2/2): the host removed the
joiner natively only after its 20 s native fallback (its own cut-frame announcement never came: the never-started
joiner's input ring read 1..0, not empty), just before its own restart; the lobby guest's 8.5 s cut-frame timeout was
blocked by a non-empty ring until its restart reset the ring, so it removed the joiner INSIDE its restart (after
`resetgame`, before the mode-2 load: `vanished ... removed now at its frame 0 ... NOT AGREED (timeout)`), and ended on the
native pause/ready screen ("Not Ready") with no checksum while the host played on. The two peers began their restarts
with different participant lists.

One host decision, one rule for every peer:

- **The host decides.** A newcomer of the admission in progress (a seat at adding/streaming/starting, a pending seat
  named in the announced boundary mask, or one already decided) whose loss reaches the host before its restart is
  announced (0x26 not broadcast, no restart begun) is decided lost: the host sends every other bound participant the
  reliable GNS control kind 11 `[11, lost index, newcomers still joining (mask), 0xa4]`, then removes it at once (no cut
  frame: the restart replaces every peer's world from the one image). Every native removal path on the host passes the
  same choke points (the notice 0x611550 and the removal 0x612c40 filters: its watchdog, its leave, the seat's give-up
  drop, our own hand-over), so the decision always goes out before the native 0x2b. A refused send is queued on that
  link and leaves before any later game message (flush_link), so the decision precedes the restart's 0x26 on every
  host link (GNS reliable delivery is ordered).
- **Every other peer carries it out before its restart.** It records the admission's newcomers (the accepted boundary
  mask, its mesh newcomers, the decisions), never removes one by its own agreement or timeout before its restart,
  removes a decided one at once when its zone runs, and at the latest at its restart entry (`deferred_campaign_start`,
  before the hold ends and before the native campaign start). A decision it cannot carry out yet keeps the deferred
  start scheduled (nothing of the restart runs) for up to 3 s, then the restart goes on with a log line; a fault inside
  the native removal is logged and never retried. The host's record of sent decisions belongs to the admission: it is
  reset at every end of one (restart entry, no-restart, session end), and a bit for an index the admission no longer
  lists is dropped, so a reused index is classified afresh. Every PC clears its ledger at its session's end.
- **The restart always follows a positive admission.** If every newcomer was lost before its image (no transfer runs)
  and an old peer remains, the host still captures one image for the existing participants (the owed restart); the
  host stays held with the old peers until it begins. With nobody else left the host plays on as before.
- **Nobody removes anyone inside a restart.** From the restart's 0x26 (host: its broadcast) until its restored zone runs
  again (the mode-2 load returned and the next pump sees no load), the cut-frame removal and the native watchdog's 20 s
  fallback wait (the watchdog's own removal during a zone load still runs natively, as before, so no load barrier can
  wait on a lost participant). Then the restarted frame 0 waits for a participant lost after the 0x26 on every peer and
  the agreed cut frame removes it there (the existing in-epoch path; AJ2 makes its hero canonical). The window never
  waits for the cohort sync (a host seat waiting at `starting` for a dead newcomer would hold that for 120 s). A loss
  noticed before the restart and not decided by the host starts its agreement afresh at the restart entry.

So every peer begins its restart with the same participants, at the same epoch frame 0; the hold releases at the
restart on every peer (or at its cap, as before). A second newcomer of the same admission rides on (the sender
completes when its image is in). A host lost at the same time ends every guest's session as before (no guest waits on
a decision; none removes the newcomer by its own timeout). Residual: a second newcomer that sees the first one's link
end while the host still hears it may remove it by its own timeout before the host does (it has no admission ledger
before the host's decision); and a newcomer whose host link is not yet switched to GNS gets the 0x26 on the stock path,
outside the ordering.

Tests: `xml2_join_in_progress_test` (wire, ledger, classification, admission accounting, restart window, owed-restart
rule, and a message-level cohort model: 1-2 existing guests x 1-2 newcomers x five loss points x 8 delivery orders, all
peers equal at the restart entry, nobody removing inside a restart, every hold released, the second newcomer in; both
newcomers lost at once; the host lost too; a 060zj control and a sensitivity check showing the checks fail when a guest
ignores the decision or times out by itself). The model's frame 0 and host-left are modelled, not production code.
`xml2_join_in_progress_boundary_test restart` blocks AR-1..AR-6 run the production admission, advance, hold, owed
restart, restart entry (its bounded wait), the restart window and session end; the transport half (the decision on
the wire, carrying it out, the queued resend) has no fixture and is checked live.

### A mesh report about the removed seat (b2c-fix-crash, AS)

Live b2c X-4p (host + two lobby guests + a newcomer killed 1 ms after the host's image): the host carried out its
decision at 00:25:47.013 (native removal, reason 0x10cd); the slowest lobby guest finished its zone load 2.5 s later,
then ran the newcomer's queued mesh completion and sent the stock 0x23 report "seat 2 is meshed with seat 3"
(0x607210: one dword, `(1 << adding) << 16 | (1 << self)`). The host's 0x23 handler 0x604dc0 turns both halves into
indices and calls 0x607390(3, 2), which looks both up with record_by_index 0x6110c0 (null for a removed index), takes
record+0x40 (0x6141e0, a `lea`) and clears the pair's bits in `[record+0x60]` of both: `mov ebx,[esi+0x20]` at
0x6073e4 read 0x60 (esi = null+0x40) and the host died (`exception 0xc0000005 at 0x006073E4 ... eax 00a531f8 ebx 4
ecx 2 edx c esi 40`; edx 0xc = seats 3 and 2). The null read is stock code; the exposure is the decision above: stock
would have kept the record until its 10 s watchdog, long after every report. Three-player sessions never reach it (one
lobby guest, whose report arrives before the newcomer's loss is even noticed).

The entry of 0x607390 (5 bytes) jumps to a bridge. With both records present the stock function runs unchanged.
Otherwise the bridge does the stock function's two effects for the record(s) that exist (`join_in_progress_core::
mesh_pair_decide`): the pair's bits are cleared on the surviving record (the manager's own 15 s pending-pair fallback
0x607dc0 clears them the same way without dropping anyone when a record is missing), and when the manager waits for
exactly this pair (+0x2220) that wait ends as stock would (+0x2220/+0x2238 := 0, settle +0x2228 := game time + 1.0),
so a following admission is not held for the fallback. Logged `online: 0x23 mesh report names participant 3 and 2 but
only the second has a record here ...`. Guards: the entry, the body through the manager compare, the tail, the handler,
both callers, 0x6141e0 and 0x6110c0 (`mesh_pair_guards`, composed into `mismatch()`; the boundary fixture installs
the site against the retail image).

### A late input-epoch marker (b2c-fix-crash, after AR8)

Live b2c X-2c (under heavy load): the host queued its start marker and its 0x5b acks at 23:15:43.245 but its pump,
starved by its own load, flushed them at 44.390; the guest's parser had meanwhile consumed a stale pre-release host
batch (0x69 sent 42.528), passed its frame-0 gate at 43.524 and ran frame 0. The marker then reset a block the guest
had already read from (`read 1 write 1 next 1`), the next host record became label 0 again and native arbitration
dropped the guest 0.13 s later - worse than stock. Now a new nonce that reaches a block whose ring read index has moved
since the block's last reset is **late**: ignored (the block keeps its records and labels, the decoder's counter
increment is given back), the nonce remembered (its second copy is the usual repeat), logged `jip input epoch:
participant index 0's epoch 0801 marker ... arrived late ... IGNORED`. That is the stock labelling for the epoch:
if the consumed record was stale the one-label offset of fable-ar7 is back for that epoch (as without AR8), never a
re-labelling after consumption. The restart-release barrier (holding a peer's pre-marker records until its marker or a
cap, so the gate cannot pass on a stale record) is designed in the b2c-fix-crash report and not part of this change.

### The restart-release barrier (b2d-fix-jip, live-b2d N-LATE4 / N-2c-LATE)

Live b2d showed the late marker is not a load artefact and that IGNORE alone splits the cohort: X-4p-a (host IGNORED the
slow guest's start marker, both guests reset -> frame-30 checksums differ -> both guests arbitrated out), X-2cL, X-2cR and
X-2cR2 (the guest IGNORED the host's marker with no extra load -> the guest dropped at frame 30/60) - 3 of 3 lost-newcomer
restarts. The mechanism: the marker is one unreliable single record and a receiver labels single records by arrival, so a
record P sent BEFORE its restart and still in flight across the ~1 s reset skew is stored after the receiver's own ring
reset as P's label 0; the gate passes on it (a record from every participant), and P's marker is then late there, while
the peers whose parsers saw the marker first reset. The same real record is label 0 on them and label 1 on the late peer.

The barrier: from this PC's own all-blocks ring reset (the release 0x613f68 and the start 0x6140b3, the sites that post
the markers) every other participant's records - whatever their store kind - are copied aside with the decoder's label
(`input_epoch_held`, 96 per participant) and not stored, until that participant's marker, which drops the copies together
with its block reset (they were pre-restart records; its real records are counted from 0 on every peer alike), or until a
cap of 5 s after the reset, when the copies are stored in the order they came through the store itself (the stock
labelling, as before the barrier; a marker after that is late and ignored, as before). Observed start-reset-to-marker skew
in every live-b2d restart, passed or failed: 1.0-2.4 s. A real record can precede P's marker only through the native
reliable resend (`0x5c` -> `0x5d` batches, whose records carry their label on the wire - live-b2d P-LJ3, the newcomer's
marker 16.6 s behind its resent frame 0): dropped at a marker inside the cap it is requested again and resent by label;
past the cap it is stored as it came and the late marker is ignored, on every peer alike. The hold is armed only while
this PC itself sends markers (`[Online] RestartInputEpoch`, default on), never for its own index, and disarmed at the
session's end. Each reset snapshots the occupied native indices from the session's participant list, including sparse
rosters; empty seats and removed records outside that list are not expected. The start reset replaces the release
snapshot and clears its old held records. This prevents a seat empty at lobby START from retaining an expectation
until a later newcomer's first input logs a false cap (for example, 62 s later with 0 held records).
Logged: `... record(s) arrive before its epoch marker ... held, not stored`, the reset line's `(N held
pre-marker record(s) dropped)`, and `... sent no epoch marker within the cap ... held record(s) stored in the order they
came`. Residual: a marker LOST (not late) on one peer that holds a stale record gives that peer the stock labelling at the
cap - the b2d outcome, never worse. No wire change: rule 26 stays at revision 7.

Tests: `xml2_join_in_progress_test` (the barrier's rules; a label model of 2/3/4-player cohorts: on-time marker, the
X-4p-a split with the b2d build as a control, the X-2cR shape, late on every peer, marker lost on every peer / on one
peer, P-LJ3's resent frame 0 past the cap, a resent record inside the cap, per-participant holds, sparse/empty/own-only
rosters and replacement of a departed participant's hold);
`xml2_join_in_progress_boundary_test <retail> input-epoch` (the production store hook against the retail store 0x559560 of
the mapped image: hold, drop at the marker, label 0 afterwards, the cap's in-order replay through the retail store and the
late marker after it, a full hold, own records, disarmed = stock; both production marker-reset callbacks with sparse
and changed native lists, removed stale objects, an empty seat's first record 62 s later without a false cap log,
own-only/empty lists and the disabled control).

### A removal decision ahead of the native add (b2d-fix-jip, live-b2d N-STALL4)

In X-4p-b, X-4p-late and X-4p-late2 the guarded 0x23 no longer crashed the host, but the restart stalled ~1 minute and the
slow guest was lost. On that guest the host's decision ("participant 3 lost before the restart", a transport control
message handled in the receive pump) was carried out at the next transport tick while the host's native add of seat 3
(0x1f, sent 1.3 s BEFORE the decision on the same reliable link) still waited in the parser queue: the tick found no record
("already gone"), closed the ledger, and 4 ms later the parser ran the 0x1f - mesh completion, 0x25 reply, a record for
seat 3 (`GNS discovery ... 4 participants`) that nothing removed before the restart. The guest's lockstep then waited for
seat 3's inputs at frame 1 (`jip ring: 5 s after frame 0 frame=1`), host and other guest at frame 4 for the slow guest,
until the slow guest's own watchdog gave seat 3 up (its 0x09 -> the host's `native drop notice caller=0x006116A1
index=3`) ~60 s later and it was itself dropped 10 s after. X-4p-c / X-4p-c2 passed with the opposite order (the 0x1f
parsed first, the decision removed the record 29 ms after the mesh completion).

Now an absent record settles a decision only at the restart entry (by then every message queued before the decision,
the native add included, has been parsed): at a tick it stays open (`... the host's removal decision arrived before this
PC's native add of it (no record yet ...); the decision stays open and the record is removed as soon as it exists`) and
the next tick after the parser created the record removes it as before (`removed by the host's decision at its next
transport tick`). Tests: `xml2_join_in_progress_test` (the rule and the X-4p-b / X-4p-c / native-0x2b orders on the ledger).

## Canonical cohort restart (0.6.0, p060fix/AE)

The late admission already reloads EVERY peer from one campaign image: the host
serialises the live campaign at the zone-load boundary (`start_image`, the same
`save_format::serialize_wrapper` a save uses), hands it to the native campaign
manager exactly as Load Saved Campaign does, and the native sender / `J`
completion / `0x26` broadcast restart every participant through the retail
campaign start `0x5f3c20` (`resetgame`, mode-2 restore, zone load). What the
060y component logs showed is that this restart is not identical on a warm host
and a cold joiner: the retail checksum is `hero_sum + camera + synchronized RNG
draw count`, and at the first period every hero transform matched (E1) while the
camera term differed by -70 in 4/4 runs and the draw count by +70..+85
(`O:/Projects/agent-work/live-060y/evidence/latejoin`, run 4 frame 30 even
cancelled to equal hashes and broke at frame 60).

The camera is a process-lifetime singleton (`0x7171d0`, vtable `0x684cc4`),
constructed once inside its getter (`0x4457e0`, the call at `0x445819`) and
never rebuilt by `resetgame` or the network soft reset. The zone-start camera
init (`vt+0x74 = 0x4458a0`) keeps the CURRENT yaw `+0x74` unless the seat
manager asks for a snap (`0x445958..0x44596f`), and the full reset (`vt+0x68 =
0x440c70`) copies the yaw history `+0x2f0 -> +0x2f4`. The constructor zeroes
`+0x74`; that is the joiner's observed yaw 0.0, while the host carried the 22.6
degrees it had turned during play (decoded from the logged eye/target vectors:
same distance 374.8 and pitch on both, only the yaw differs).

The fix:

1. **Pristine camera image.** The construction call is bridged; right after the
   retail constructor returns, the singleton's `0x4dc` bytes (up to its
   construction guard byte `0x7176ac`) are copied. If the singleton was already
   constructed when the fix installed, no image is kept and the restart logs
   that it keeps the running camera.
2. **Every peer restores it before the native campaign start of a cohort
   restart** (`deferred_campaign_start`, the scheduler task outside packet
   delivery, after the existing member's soft reset). Warm host and cold joiner
   then run the restored zone's camera init from identical inputs. A foreign
   vtable refuses the restore. `[Dev] LateJoinRestartReset=0` keeps the 060y
   behaviour as an A/B control.
3. **Synchronized RNG epoch at the first shared frame.** The native setup
   reseeds both streams after the mode-2 load (`0x606fb0 -> 0x613df0(2)`), but
   the zone loads afterwards and the warm/cold paths draw differently while it
   does. `campaign_load_bridge` arms the epoch; the input-manager run after the
   frame gate (`0x606b98 -> 0x5587c0`, every participant's input for the frame
   present) reseeds the synchronized stream once with the native constant when
   the native frame `0xa3bbf4` is 0, on every peer at the same lockstep point.
   Ordinary play and lobby START are never armed.

4. **Only for a late-join restart, on every peer alike.** A lobby START of a
   saved campaign sends the same 0x47 prepare (mode 2) and only the guests
   enter the cohort epoch there (the host never receives its own 0x47), so an
   unconditional restore/reseed would run on the guests alone and break the
   proven lobby START. The host therefore stamps the 0x47 body's two unused
   tail bytes (`0x6095a5`, body+6; stack residue in retail) with `"LJ"` while
   `start_image` runs `native_campaign_start` for a late joiner and with 0 at a
   lobby START; receivers gate the restore and the epoch arming on the marker,
   the host on its own `start_image`. `campaign_notice` logs which kind it saw.

   The stamp bridge is entered by JUMP (no return address): the opcode write is at the
   site's `[esp+0x30]` and the tail bytes at `[esp+0x1e]` (`[esp+0x22]` under the pushed
   EAX). The first AE build used CALL offsets there and sent opcode 0xa4 for every
   saved-campaign prepare (live-060y-ae); the fixture now enters the bridge by jump on a
   patterned Start Game frame. The construction bridge (0x445819) and the frame-gate
   bridge (0x606b98) are installed as CALLs and handle their return address.
5. **A given-up seat is released.** When the add's wait, the image or the load outlives
   its limit after the native add, the host drops that participant natively (0x6133a0
   with the retail slow-player codes 0x10ce/0x10cf), so the hero returns to AI and the
   joiner gets the stock drop notice and can join again; a never-added pending seat is
   simply freed.

6. **Zone-load RNG epochs (latejoin-residue P1).** Retail draws the synchronized
   stream before frame 0 while the zone loads and starts (loading-screen chooser
   `0x487000`, spawner store `0x48adc0`, the touched nudge `0x498ef0`, drops
   `0x46e490`, AI), and a warm host and a cold joiner reach those draws from
   different states. A late restart therefore takes an epoch at CMap::LoadMap entry
   (`0x484108`, the `call 0x4457e0` after the loading bits) and at the zone-loaded
   finish entry (`0x4867fe`, its first call `0x596890`): call-shape bridges that
   keep the site's return address and tail-jump to the getter, gated on the late
   restart being armed and frame 0 not yet taken. Lobby STARTs and ordinary zone
   transitions never arm.
7. **AI brain free ring (P2).** The AI system's 32-entry ring (`0x78ba14`
   +0xc384[32], write/read/free at +0xc408/+0xc40c/+0xc410, held bits +0xc414) is
   only ordered by the constructor (`0x522f70`); at a late restart's zone load it
   is rebuilt over the FREE slots in ascending order (read 0, write = free count).
   p060fix/AI: the rebuild runs on the pool the published CAI descriptor names
   (`actor_ai_layout.hpp` ring offsets): the stock pool at stage 0 and at stage 1
   (the standalone `[Limits] ActorPools=124` install keeps the stock layout), and
   E4's 96-slot pool at stage 2 (`cai+4` +0x24a8c[96], +0x24c10/+0x24c14/+0x24c18,
   three held words at +0x24c1c: the E4 ledger's operands at 0x522a12/18/25/59 and
   0x522f74, checked by the unit test). Held bits, free/live counts (E4's
   reservation accounting reads them) and the brain/world generations are
   untouched; another stage/count pairing is left as it is and logged.
   p060fix/AP: the rebuild also resets every FREE slot's consumer state through `actor_ai::reset_controller_slot`
   (the npc power rule's combo deadline and reaction marks, npc-power-rule.md item 23), on every peer alike: the
   host runs frames alone after the last shared one while the image transfers (live-060zg ZG-C-3Pp2: 493 host-only
   draws, seven hits), and their marks must not reach the restarted brains. The log line ends `N free slot(s)'
   consumer state reset`.
8. **Probe (P3, `[Dev] LateJoinProbe=1`, off by default).** `jip snapshot:
   where=...` lines at every milestone (deferred start, mode-2 load, zone load
   entry/epoch, zone start/epoch, zone started, frame 0 (+epoch), frame N samples)
   with the native frame, session phases, clock, game time, both RNG streams, the
   world and brain rings, the camera and each hero's handle/brain/controller/flags/
   transform/health; and `jip rng: phase= frame= count= caller=` lines from a
   bounded ring (8192) fed by a jump at the synchronized draw entry `0x560090`
   (wrapper returns `0x5601bd`/`0x5601fb`/`0x56023f` normalised to the real
   caller), flushed at the milestones off the draw path.
   `scripts/jip_components.py HOST.log GUEST.log --mode snapshot|rng` pairs them.

Logs: `the game constructed its camera; pristine image kept`, `canonical
restart - synchronized RNG epoch armed`, `canonical restart (<role>) - the
camera is back at its constructed state ... (was yaw ... mode ...)`, `first
shared frame 0 - synchronized RNG epoch reset`. `scripts/jip_components.py
HOST.log GUEST.log` pairs the `jip checksum produced/hero` samples by epoch and
frame and reports every term; `xml2_join_in_progress_boundary_test <retail exe>
restart` runs the bridges and the real retail seed setter on the mapped image.
Room listings now advertise a started room as joinable exactly when the host
admits late joiners (`late_join_enabled()`), so Browse and the broker's
`room_not_joinable` agree with the host's DROP-IN choice for PUBLIC rooms; a code join to a
started public room with DROP-IN off is refused before the tunnel. Fix AN2: a private room (never listed) stays
resolvable after the start while this refusal is installed, so its code join reaches `LateJoinPolicyV1` and shows
"This session has started; ask the host to return to the lobby" ([online-rooms.md](online-rooms.md#started-private-rooms-with-drop-in-off-fix-an2)). Fix AN asks for a fresh public listing on that refusal:
a matching started, unjoinable row with a known free seat shows "This session has started. Ask the host to return to the
lobby." Private or inconclusive listings retain "Not taking players right now. Try again soon." The existing native
refusal notice remains available after the tunnel; see [the decision rule](online-menus.md#code-refusal-after-the-session-started-fix-an).

What only a live round can prove: that the restored camera init and the frame-0
epoch leave the first four checksum periods equal on both PCs with movement,
and that the E2 AI-hero displacement (run 2 slot 3) was downstream of the same
two terms.

Follow-up 5 (probe round 2): with the camera and the rings canonical, the joiner
still placed four more AI actors than the host at E1 (brains 16 vs 12 at "zone
started", four extra 7-draw actor groups before frame 0 and two more at frame 0)
and made the actor touch-nudge draws (`0x42974d`) at E2. A visited zone is
restored from its record: the live actors through the dynamic list (equal on
both peers) and the dormant monsters its spawners stored as CSmartEntMgr
records, which only the manager's update (vt+4 `0x4b7100`: the records inside
a box around the camera, 750 units ahead, 40 per call from a round-robin
cursor) respawns - from the game update every tick (`0x46f2df`), including the
ticks each peer spends waiting at the barrier before frame 0 with a camera that
drifts differently. During a late restart the call site hands the game update
a stand-in whose update does nothing, from the zone load to the frame-0 epoch,
where the real manager's cursor goes to 0 on every peer and the first respawn
runs in lockstep from the re-anchored camera. Logs: `the dormant-entity
respawn (CSmartEntMgr update) is held from the zone load until the first shared
frame` and `the dormant-entity respawn resumes at the first shared frame (N
update(s) held; M dormant record(s), cursor C -> 0)`. The probe adds
`smart=n: cursor: held:` to every snapshot and, from a jump at the spawn-by-
names entry `0x4612e0`, one `jip spawn: phase= frame= n= caller= def= inst=
at= draws=` line per entity the zone spawns (bounded, 2048 per epoch);
`scripts/jip_components.py HOST.log GUEST.log --mode spawn` names the extras.

Follow-up 7 (probe round 4): every actor update is a timed event of the
manager `0x75d340` (the actor's members +0x320/+0x328/+0x330 fire `0x434540`
when the queued time equals the stored one; scheduled through `0x41d380` ->
`0x41a730` -> manager vt+8; pumped by the game update through vt+0xc). The
pending entries are a binary heap of (time, record index) at body+0x7c40 that
compares TIMES ONLY, so the order of a frame's same-time updates - and so the
order in which due AI brains think and draw - is the heap's shape, which each
process's push/pop history decides (round 4: the same three brains re-armed
with the same three draws in a different order on the two peers at frame
16/17, in a different rotation per run). At the late restart's epochs (zone
load, zone start, first shared frame) every peer rebuilds the queue in a
canonical order: entries whose owner is a dead entity token are dropped (their
fire is a no-op that still shapes the heap), the rest are sorted by (time,
owner's dense index - never the generation, event offset, payload), the pool
is re-initialised by the retail pool init and every entry re-added through the
retail alloc / store / heap push. Logs: `the timed-event queue (every actor
update) is rebuilt in canonical order at <epoch> on every peer: N entries kept
(D whose owner is gone dropped, S cancelled kept), heap A -> B, records ...`.
The frame-0 epoch also logs the game thread's x87 control word and MXCSR and
sets them to the game's own values (0x027f: the CRT's `_PC_53`; 0x1f80) should
either have drifted (`FPU control state at the first shared frame is the
game's` / `CORRECTED`). Probe additions: `fpu=` on every snapshot and cam line,
`jip events: where= heap= records= ring= live= dead= stale= rebuilds= head=`
at every milestone, before and after the rebuilds, and at frames 1..3;
`jip camdump: frame= off= <32 dwords>` of the whole camera object at the
frame-0 epoch and frames 1 and 2. Tool modes `--mode events` and
`--mode camdump`; `--mode rng` now compares the actor's dense index, controller,
think time and distance as well as the caller; `--mode cam` accepts the `t=`
field.

Follow-up 8 (live round 5): `xml1_turrets` installs its scheduler dispatch hook
at the timed-event manager's pump slot (vt+0xc, `0x68de20`) before this module,
so the vtable guard covers only slots 0..2 and the install checks the pump slot
separately: retail `0x4b2d60` or `xml1_turrets::installed_process_hook()` are
accepted (logged `JoinInProgress composes with the xml1 turrets scheduler
dispatch hook ...`), anything else refuses the install. The rebuild and the
probe never call the pump (retail pool init / alloc / store / push and reads
only). `test/module_hook_sites.json` lists such slots for the guard scan's
cross-module table; the native fixture mode `turrets` installs a stand-in of
the hook first and drains a canonically rebuilt queue through it.

Follow-up 9 (live round 6): the frame-0 epoch never rebuilt the queue on
either peer (`should_reseed` consumed `armed` before the rebuild asked for
it) and `cohort synchronized` cleared `late_restart` BEFORE the joiner's frame
0 (245 ms earlier; after it on the host), so the joiner skipped the FPU step:
the epoch differed by role. Now the epoch's steps follow `should_reseed`
unconditionally in a fixed order on every peer - RNG epoch, camera re-anchor,
respawn release, FPU control state, the queue rebuild LAST - and the epoch
itself ends the late restart (`the frame-0 epoch is complete on this peer ...;
the late restart ends here`); `cohort synchronized` keeps the flag while an
epoch is still armed. Follow-up 10 (live round 7): AE10 also resumed an existing member's native
controls at the zone start and marked the pump's resume spent; the host then
never produced its frame-0 input and every late restart froze at the barrier.
Reverted: the pump resumes the controls after the world restore, as before
(the fixture drives the production zone-start notice as an existing member and
asserts the pump's resume is still due). The AE10 logs show every hero
transform bit-identical on both peers at `zone started`; the settle that parts
them runs in the barrier-wait ticks, whose count differs per peer.
The canonical order keys an event by its offset
inside its owner or, for a static kind object (the "moved" notification
0x6d7c5c and the table at 0x6d55a4), by its address, which every process
shares; the probe's `jip events:` lines now carry every entry (`part=k/n`,
16 per line, `time:index:handle:kind:payload`) and the snapshot's hero lines
walk the party list (game vt+0x11c). `--mode events` lists the entries present
on one peer only.

Follow-up 11 (the AE12 world hold) is gone (follow-up 14): the bridge over the
game update's world-section gate at 0x46f0a9, its watchdog, `[Dev]
LateJoinWorldHold` and the guard were removed after live rounds 8, 9 and 10
logged `0 world update(s) skipped` on every peer in every admission - the
world section does not run during the barrier wait (the pause byte holds it).
The AE8 FPU "correction" is gone too: the only thing it ever changed was MXCSR
bit 5, the sticky precision FLAG (0x1fa0 -> 0x1f80), which affects no result;
the probe keeps its `fpu=` field.

Follow-up 12 (round 8): the joiner alone held a queued "moved" event (the
static kind 0x6d7c5c) for its OWN hero at frame 0 and the host alone
registered them for the AI heroes at frames 1-3, with every hero transform
identical. The moved check is the entity's vt+0xf4 = 0x471740(time):
position +0x20 vs last-known +0x1e8, orientation +0x2c vs +0x1f4, or 0x470b30
(+0x23c / +0x240), and with bit 2 of +0x258 clear it sets the bit and
schedules the kind at game time + arg (0x41a730); the kind's payload getter
0x419c40 returns size 0, so the record's 16 payload bytes are stale residue
(never compared). The fire 0x4729b0 clears the bit, refreshes the last-known
vectors (vt+0x148 -> 0x471400; a non-AI hero also notifies its position
through vt+0x158) and re-runs the check with 0. The caller that registers
the joiner's hero during its wait is not traced (vt+0xf4 is a slot offset
~80 call sites share across classes). At the frame-0 epoch, on every peer,
before the queue rebuild, every party hero (game vt+0x11c) gets the state of
an entity that has not moved since it was placed - +0x1e8 = +0x20, +0x1f4 =
+0x2c, bit 2 of +0x258 clear - and the rebuild drops the party heroes'
queued moved events (by dense index; other owners' stay). Log: `the party
heroes' moved-check state is canonical at the first shared frame on every
peer: N hero(s), last-known = current (D differed), P pending flag(s)
cleared; their queued moved events go in the rebuild (run 1)`; the rebuild
line counts `M party-hero moved event(s) dropped`. `[Dev]
LateJoinMovedCanonical=0` is the A/B control. The probe's hero lines carry
`lk:` (the last-known vectors) and `mv:` (+0x258, +0x23c, +0x240).

Follow-up 13 (round 9: both E1 admissions agreed through frame 120 with the
queue 43/43, then the joiner self-dropped at frame 840 / 720 with no input;
E2 still parts at Storm's placement touch-nudge on the joiner alone, and the
host's heroes reach the zone start with warm last-known vectors): with
`[Dev] LateJoinProbe=1` the checksum diagnostics window's period budget is 30
(frames 30..900; the default four periods otherwise) - component samples,
draw-ring flushes and snapshots every 30 frames, an events dump every 60 -
and the three touch-nudge draws log `jip touch:` (the touched entity, its
moved-check fields and the return-address chain above the draw, 24 lines per
epoch; since follow-up 14 also `parent=`, the entity the child stands on). The
cold moved pass that follow-up 13 put at the zone-start notice ran on an
EMPTY party list (0x4867fe is before the heroes are placed by game vt+0x190 at
0x486884 and bound by 0x422520 at 0x4868a5: round 10 logged `0 hero(s)` on
both peers); follow-up 14 runs it from the party-binding tail instead.

Follow-up 14 (AE15; review O:/Projects/agent-work/ae-review/REVIEW.md and the
E2 trace O:/Projects/agent-work/e2-settle-re/RESULT.md):
- The camera canonical at frame 0. Every round's frame-0 camdump had 49-74
  dwords differing between the peers AFTER the re-anchor (eye/target copies by
  0.66 units, the yaw history by 8 ULPs) because the zone-start init vt+0x74
  (0x4458a0) takes the party target from the seat manager's vt+0x58
  (0x48e120, which reads the camera back through vt+0x88 / vt+0xd8) and the
  yaw through 0x43f150 from the CURRENT eye/target (fpatan at 0x43f1f8) and
  the history +0x2f0: per-peer state the loading-screen updates drifted. The
  checksum's camera term is six ROUNDED components, so a 1-ULP difference
  flips it only when a component lands on a half-unit while the camera moves:
  the late, delta-1..8 drops (frames 720..4200). Now, at the frame-0 epoch and
  before vt+0x74 runs from the party anchor, every peer puts the camera's
  derived spans (`join_in_progress_rules::camera_derived_spans`: the union of
  every dword that differed between the peers at frames 0/1/2 in the 11
  dumped admissions, plus the current yaw +0x74 and the matrix head +0x18c;
  0x1c8 bytes) back to the pristine image, so the init runs from identical
  inputs. The other 205 dwords were equal on both peers in every pair. Log:
  `the camera is re-initialised from party slot 0 at frame 0 (was ...; N
  derived dword(s) back to the pristine image first; now ...)`.
- E2's touch-nudge. CEntity vt+0xac is "OnAttached(child)"; CActor's override
  0x429710 draws three bools (0x42974d/63/73) and sets the CHILD's move target
  (+0x120/+0x138 through 0x473640) when a character lands on an actor. The
  only entity-to-entity caller is 0x419f20 ("set ground entity": when the
  child's +0xbc changes to a non-world entity it resolves the parent, calls
  parent->vt+0xac(child) and stores +0xbc), reached from FindGround 0x470bb0
  inside the physics steps. A fresh actor has +0xbc = 0, so its first
  non-world ground touches; the cold joiner's zone-loaded finish (110 ms) ran
  such a step for Storm before `zone started`, the warm host's (13 ms) did
  not. During an armed late restart a JMP-shape bridge at the override's
  entry (its 8-byte prologue displaced) answers `ret 4` for a party hero - no
  draws, no move target; 0x419f20 still stores +0xbc - and at the frame-0
  epoch (after the RNG epoch and the moved pass, before the queue rebuild)
  every peer puts each party hero's +0xbc back to 0 and runs FindGround in
  party order: the same ray from the same position, the same ground fields
  and, if an actor is below, the same nudge draws at the same count. Logs:
  `a party hero's ground attach callback (CActor OnAttached 0x429710: the
  settle nudge and its three draws) is suppressed before the first shared
  frame on this PC (hero H on entity E; suppression N)` (joiner expected,
  host not) and `the party heroes' ground is re-probed at the first shared
  frame on every peer: N hero(s), S had a ground entity before, H ray hit(s),
  C attach callback(s) (...)` with equal N/H/C on both. The probe's hero lines
  carry `gnd:<+0xbc>/<+0x200>`. Retail guards 0x429710, 0x419f88, 0x470bb0.
- The cold moved pass moved to a CALL-shape bridge over `call 0x495350` at
  0x4868ad (right after 0x422520 bound the party; guard 0x4868a5), logged as
  `... canonical at the zone start (after the party is bound, before the first
  physics step)`.
- Review fixes: a heap-allocated event kind (outside [0x401000, 0xa40000)) is
  keyed by its vtable (S2; logged once when a kept entry has one); the probe's
  30-period budget applies to a late restart's epoch only (S6); the draw ring
  is heap-allocated only with the probe (S5); `scripts/jip_components.py
  --mode events` never compares the payload (S7). The native checksum
  comparison bridge now logs `jip checksum MISMATCH: frame=N heads(...)` with
  this PC's LAST producer sample (hash, hero subtotal, camera subtotal, draw
  count, the six raw camera components, every hero transform) whenever two
  present heads disagree at one frame - outside the observation window too -
  so a late drop names the term that broke.
- The device-switch probe. Round 10 (ae14): with everything above, three E1
  admissions (one on two PCs) agreed on every term and every draw through
  frame 900 and one sat idle to frame 4200; every late drop of rounds 9-10
  came 1.2-1.7 s after the JOINER logged `prompts: player 1's prompts show
  the pad` (its first pad input device, before any stick deflection), while a
  lobby START survives the same switch. The retail input record (0x558ec0:
  four scaled axis bytes, the buttons, a frame time) carries no device flag,
  so the switch's effect is local to that PC; the fix's own `pad_hotplug`
  runs a local "arrival refresh" there (binding rebind, retail 0x628e20).
  With `[Dev] LateJoinProbe=1` the producer sample is now logged at every
  period on both peers, and a prompt device change (pad_prompts' counter,
  polled from the pump) re-arms the 30-period window in the same epoch,
  restarts the draw ring in phase 5 and dumps the snapshot, queue, camera
  and camera lines again on the switching PC (`jip switch:`), so the next
  drop is bracketed on both peers and names its term and first draw.
- The movement probe (round 10 A/C: the drop follows the first period of
  movement of either hero in a late-joined session - the pad-only joiner
  moving dropped at frame 420, the host moving with the joiner idle at 540 -
  on hero_sum only: the moving hero's last-bit x/y and a following AI hero's
  heading by 1-6 ULPs; the device switch itself is harmless, a lobby START
  survives it). With the probe on, one `jip hero:` line per party hero per
  gated frame carries the raw dwords of +0x20..+0x38, +0x120..+0x168,
  +0x1e8..+0x270 and +0x2b0..+0x320 (the ranges the physics steps 0x496f20 /
  0x4762c0 / 0x472cf0, FindGround and the move-target setter 0x473640 read)
  with the game's t/dt, for 60 frames after the frame-0 epoch, again from
  the first frame a party hero's position changes after that window
  (`jip move:`), and after a device switch; `scripts/jip_components.py
  --mode hero` pairs the lines by (frame, slot) and names the first
  differing dword offset.
- AE16 (O:/Projects/agent-work/padswitch-re/RESULT.md): the controlled hero's
  move direction is stick x the shared camera's basis vectors (0x42dbf0 ->
  camera vt+0xcc 0x43fe00 copies +0x2cc/+0x2d8/+0x2e4; 0x447730 rewrites all
  nine each frame) and the retail camera term is one _ftol of six summed
  components, blind to ULPs - so a ULP-different camera moves the heroes
  differently while the term agrees (the first-movement drops of round 10
  A/C). The derived spans now cover +0x2cc..+0x2f8 as one span. At the
  frame-0 epoch, after the ground re-probe, every peer also makes each party
  hero's motion fields canonical: gravity hold +0x2f0 := 0, last fall +0x28c
  := t, last update +0x150 := t, the velocity and angular pairs := 0 (log:
  `the party heroes' motion fields are canonical at the first shared frame on
  every peer (t ...)` with the before-values). The hero probe line adds
  +0x178, +0x190..+0x1a4, +0x1d0..+0x1e8, one contiguous +0x1e8..+0x320 and
  CHero +0x7e4..+0x81c; every `jip cam:` line carries the raw basis and the
  decoded stick floats (compared by `--mode cam`).
- Closing summary (live-060y-ae round 11, 2026-10-30: late join holds - 5/5
  admissions through 60 s of joiner movement, 60 s of host movement and
  walked exits, one PC and two PCs, E1/E2/E3, no checksum mismatch). What
  every peer does at a late restart, and the round that proved each step:
  the 0x47 late-restart marker (rounds 1-2); the camera singleton back to
  its constructed image before the native start (060y, rounds 1-2);
  synchronized RNG epochs at the zone load, the zone start and the first
  shared frame (rounds 2-3); the AI brain free ring in ascending order
  (round 3); the zone-start camera anchored on party slot 0 and
  re-initialised at frame 0 (rounds 3-4); the dormant-entity respawn held
  to frame 0 (round 3); the timed-event queue rebuilt in canonical order at
  the three epochs, composing with the turrets hook (rounds 4-6); the
  frame-0 epoch unconditional and ending the restart on every peer (round
  6); the party heroes' moved-check state cold after the party binding and
  at frame 0, their queued moved events dropped (rounds 8-9); the camera's
  derived spans - eye, target, history, basis, scratch - back to the
  pristine image before the frame-0 re-anchor (rounds 9-11: the retail
  camera term is one _ftol of six summed components, blind to the ULPs
  that moved the heroes apart); a party hero's ground attach callback
  suppressed while armed and FindGround re-run at frame 0 in party order
  (E2, rounds 9-11); the motion stamps canonical at frame 0 (AE16,
  hardening); given-up seats dropped natively (round 2). Every step is
  gated on the armed late restart; a lobby START, an ordinary transition,
  a host-only session and offline play never arm. Probe-only: the draw and
  spawn JMP bridges, every `jip ...` line, the device-switch re-arm (now
  unnecessary - round 10b disproved the switch as a cause; recommended for
  removal at integration). Removed along the way: the AE10 zone-start
  controls resume, the AE12 world hold, the AE8 FPU write, the AE14
  pre-placement pass. Residual risks: the camera span list is empirical
  over two zones (a magnet zone needs one probed late join with `--mode
  camdump`), +0x23c/+0x240 are logged not canonicalised, two-PC coverage is
  one admission, and a 3-peer admission / host quit during the reload were
  not re-run on this line.
- Toggles: `[Dev] LateJoinRestartReset` (the whole canonical restart, A/B
  returning the 060y pattern), `[Dev] LateJoinProbe` (bug reports only) and
  `[Dev] LateJoinMovedCanonical` (until E2 is settled).

Integration 11 composes the two stock AI-ring observation guards with an already
committed Actors=96 transaction. Their E4 ledger operands are accepted only when
E4 reports installation and publishes stage 2 / 96; the mandatory controller-pop
observer's reported bytes are composed afterwards. Every other prefix byte stays
guarded. The mapped-image `e4-guards` fixture refuses uninstalled, wrong-layout,
unreported-observer and individually corrupted prefixes. This restores JIP
installation for that optional configuration; it does not establish late-restart
determinism with 96 controllers, whose stock-ring canonicalisation remains off.

Integration 12 corrects three diagnostic regressions in the AE15b/AE16 handoff.
The per-hero frame window expires after 60 samples, allowing the first later
movement to arm one further 60-frame window, including that moving frame.
Explicit device-change rearming remains available; probes remain inactive when
disabled or outside a late epoch. The offline tool keeps checksum-hero and raw
hero-field regexes separate, and treats each PC's controller index as local
context instead of a simulation difference. Synthetic parser tests and the
mapped-image restart fixture cover these paths. Live movement agreement still
requires the acceptance rounds; these diagnostic corrections do not establish it.

p060fix/AI (after integration 11) closes the two gaps the int11 review named:

* `[Limits] ActorPools=124` without `Actors=96` (the standalone W11 actor-pools
  install, stage 1) relocates the brain alloc's ring read at 0x522a18 into its
  cave before this module installs, inside the 0x522a10 guard, so JIP was refused
  on every `--actor-pools 124` build. The guard now composes exactly the bytes
  `actor_pools::installed_controller_reset()` reports it committed there (the
  module's own verified relocation: 5-byte JMP plus 2 pad), only at stage 1 with
  E4 off; everything else in the 16-byte span stays retail, and a report that
  differs from the image, a report at stage 0 or an E4 installation are refused.
  The site is composed like the E4 observer rather than listed in
  `test/module_hook_sites.json` (the guard scan allows no literal guard over a
  listed site). The boundary mode `actorpools` installs the production plan's
  W11 relocation first (actor_pools_plan::make against a fixture cave), proves the
  raw guard refused it (failing-first), the composed guard accepts it with every
  prefix byte still guarded, and pops a canonically rebuilt stock ring in order
  through the relocated alloc. The install logs `JoinInProgress composes with the
  standalone ActorPools W11 controller-reset relocation ...`.
* With `Actors=96` the canonical restart's brain-ring rebuild (step 7) now runs on
  E4's 96-slot pool at its published offsets instead of being skipped, so the
  ring order no longer depends on each PC's allocation history there; the
  `restart` mode proves it on a synthetic 96-slot pool (held slots across the
  three words kept out, counts and held bits untouched, the stock pool untouched)
  and the unit test ties the offsets to the E4 ledger rows. Live admissions with
  Actors=96 remain unverified (the other restart steps never depended on the
  pool layout).

p060fix/AJ (live-060zb ZB-3P / ZB-3Pp, the first three-peer late join): host and
lobby guest agreed and the late joiner self-dropped at frame 30 with 8-12 more
synchronized draws, every hero transform and the camera equal. The probe showed
the same 115 dormant records respawned on every peer but at different frames:
the joiner swept all of them in the frame-0 tick, the two existing participants
40 records per frame over frames 2..11 (the AE14 two-peer rounds had swept all
114 at frame 0 on both peers). The game update decides the manager update's
`all` flag by `distance2(camera eye, game+0x1f0) > 10000` (0x46f2b7..0x46f2d8,
threshold [0x6872d0]) and stores the eye into game+0x1f0 after the update; the
zone-loaded finish puts the far marker igVec3f(1e30, 0, 0) there through game
vt+0x28 = 0x4685b0 (0x4869cd, right after the party is bound) so the first
update after a zone load examines every record. The one world tick the hold
spans lands before that finish on some peers (the marker survives: `all` = 1 at
frame 0) and after it on others (the stand-in update is a no-op but the site
still stores the eye over the marker: `all` = 0, a 40-record budget per frame) -
a per-PC timing, not a role, so two peers can agree by luck and a third part.
Now the stand-in getter logs the held tick once per hold (`the held
dormant-entity update ran at native frame F on this PC with game+0x1f0 = ...
(the zone-loaded finish's far marker: this tick came after the finish | a camera
eye: ...)`) and the frame-0 epoch, right after the respawn release and before
the first real update, puts the marker back on every peer (`the first
dormant-entity sweep after the first shared frame examines every record on every
peer (game+0x1f0 := the far marker 7149f2ca,0,0; was ...)`); from frame 1 on the
flag follows the canonical camera alike everywhere. Party-size independent: it
canonicalises one per-process variable at the epoch every peer runs, like the
cursor := 0 beside it. Also: a second newcomer admitted at the same zone load
used to be refused at `start_image` ("a campaign sender is already running")
and dropped; it now rides the transfer the first one started (the native sender
images every participant, the 0x26 broadcast restarts the cohort once every
image is in). `scripts/jip_components.py --mode spawn` reports `spawn frames
differ: host {...} / guest {...}` when the same entities spawn at different
frames. Guards: 0x4685b0 (the setter), 0x46f2b7 (the compare), 0x6872d0 (the
threshold), 0x4869c4 (the finish's call).

p060fix/AJ2 (live-060zb ZB-4Pb, the host and the lobby guest parting at frame 60
after a second newcomer was given up): a party hero's +0x3d8 bit 0x20 means "a
timed event of the static kind 0x6d5e30 is pending" - set once by actor
vt+0x1ec = 0x42a460 and scheduled through 0x41a730; the fire 0x42ef30 clears it
and, for a player hero (AI bit clear), re-arms it every 0.1 s, for an AI hero
clears +0x774. The zone-start binding gives a player hero the event (0x70) and an
AI hero none (0x51); the participant drop releases a hero through 0x41da60,
which ORs the AI bit only (0x71), so the hero keeps its queued event. The host
dropped the given-up seat before its own reload (hero 3 AI from the zone start,
no event), the lobby guest restored with the seat still listed and removed it
35 ms before its frame 0 (one more queued event at t=3.0 on hero 3: rebuild 3
kept 45 entries there, 44 on the host); the pop at frame 30 reshaped the heap
and the same-time think order swapped at frame 31 (rng 160 vs 150 by frame 60).
A cold newcomer never activates the seat and restores like the host (ZB-3Pb
held). Now the frame-0 epoch makes the periodic event follow the AI bit on every
peer: an AI party hero loses the pending bit and the rebuild drops its queued
kind-0x6d5e30 events (log `the party heroes' periodic event (kind 0x6d5e30)
follows the AI bit at the first shared frame on every peer: N hero(s), A AI ...,
C pending bit(s) cleared on released seats, M player hero(s) without one; flags
(slot:before->after/+0x774) ...`, and the rebuild line counts `AI-hero periodic
event(s) dropped`). Party-size independent: per party slot, decided by that
hero's own AI bit, on every peer alike. Guards 0x42a460, 0x42ef3d, 0x41da63.

p060fix/AK (live-060zd ZD-3P-E2 / E2p, three peers with the newcomer admitted
at E2): the three peers reached frame 0 identical (draws, brains, heroes, the
128-entry timed-event queue in order), yet at frame 1 the fourteen AI actors of
mansion2_2 (the sp_civiliankid_* kids, dense 0x114..0x121) re-armed their actor
update at two cadences: seven every frame, seven every 0.1 s - and the two
sets differed between the newcomer and the existing peers ({0x116,0x11a,0x11f}
fast on the host and the lobby guest, {0x115,0x11e,0x121} fast on the newcomer;
the one-PC 2-peer control agreed on one set). The cadence is the actor update's
tail 0x434fc3..0x43500c: `+6 bit 0` or a positive move speed (+0x180 / +0x184,
set with the move goal +0x1d0 / start +0x1dc by 0x422840, cleared together by
0x421b90) re-arms through vt+0xf8 with 0.001 s, else 0.1 s. A kid updated every
frame thinks on the first frame its brain is due, a kid updated every 0.1 s up
to three frames later (actor 0x115: frame 14 on the newcomer, 17 on the host,
the same next-think 2.602), so the same brains drew in a different order from
frame 13 and the counts parted (82 vs 78 at frame 30). The speeds were a
one-step goal (fast at frame 1 only, every kid slow from frame 2 on); which kids
carried one into frame 1 depended on the party and on the PC - the exact
writer before frame 0 is not traced (no draw, no think and no actor update runs
between the zone start and frame 0; the settle passes vt+0x98 = 0x41e280 ->
0x474080 only place actors on the floor). Now the frame-0 epoch makes every
non-party AI actor's move intent canonical on every peer (after the motion
pass, before the seat-events pass and the rebuild): the installed world is
walked by dense index (CWorld vt+0x10), every entity with the CActor vtable
0x682ccc, no controller (+0x3d4 = -1) and not in the party gets 0x421b90's
values (goal and start FLT_MIN, speeds 0); the +6 flag, a follow handle and the
goal vector are counted, not written. Log `the AI actors' move intent is
canonical at the first shared frame on every peer: N AI actor(s) (CActor without
a controller; H party hero(s) left to their own passes, C entity slot(s)
walked), S had a move speed (...), G a move goal (+0x1d0), F the fast-update
flag (+6 bit 0, counted only), L a follow handle (+0x3ec, counted only); goal,
start and speeds now 0x421b90's values on all; with a speed: <dense indices>
(run k)`; the completion line gains `AI move intent`. Party-size and exit
independent: every live NPC on every peer, nothing keyed on a seat, a role or
a PC; the heroes keep their own passes. Guards 0x434fc3 (the tail, 0x49 bytes)
and 0x421bb0 (the clear's body, 0x45 bytes).

p060fix/AL (live-060ze ZE-3P-E2p, 4/4 failures on the AK build): the AK line
read `S 0, F 7` on every peer - no kid had a move speed at the epoch, seven
carried the OTHER every-frame input, the entity's active bit (+4 bit 16 = +6
bit 0), and `--mode cadence` named the same six kids. The bit has one writer:
CEntity vt+0x5c = 0x4601f0(bool) through the generic flag setter 0x41a190(16,
v) (a true-to-false change can invoke gameplay through entity vt+8 after a
read-only grid occupancy check),
called from the grid/query activation code (0x466b10 deactivates a query's
results, 0x466dde activates a list, setInvisible 0x4a8910 pairs it with bit 15)
- camera/query driven before frame 0 with each PC's own camera (the zone-start
camera history differs per PC until the frame-0 re-anchor), so 7 of the 14
kids were active on the existing peers and 7 others on the newcomer. From
frame 2 on every kid re-arms at 0.1 s on every peer (nobody re-activates them),
so the epoch clears the bit directly on every non-party CActor (never through
the setter), logs the dense indices that had it (`F the active bit (... cleared
on <indices>)`), and the probe adds one `jip npc: frame=N <index>:<bit>/<speed
a>/<speed b> ...` line per gated frame. The first update after frame 0 then
re-arms every NPC at 0.1 s alike on 2..4 peers at any exit: per entity, on
every peer, nothing keyed on a seat, a role, a PC or a zone.


p060fix/AM closes the audit's R1 and R4 gaps:

* After AL directly clears NPC active bits, every registered selected NPC (+0x6c bit 0)
  notifies the installed world's `vt+0x3c(token, flags+4)`, matching the flag
  notification at retail `0x45fe40`. Retail `0x4c6990..0x4c6b5c` changes only
  the transposed flag bitmaps and 16-byte cached flag record; its only call is
  the `rep movsd/movsb` copy at `0x4015d0`. The raised world's corresponding
  method has the same contract. No SetActive, grid operation or entity callback
  is invoked. Already-inactive NPCs are reconciled too, including stale cached
  flag-15/17 predicates; their authored object bits are preserved. The AL line gains `N world-index entries corrected` (cached records
  that differed when notified). Unregistered NPCs have no entry to notify; party
  heroes are excluded before the clear. This repairs indexed flags, not the
  other activation side effects listed in audit R2.
* At every canonical frame-0 cohort epoch, the XML1 turret adapter explicitly
  resets its three deadlines from that frame's game time, target/burst/aim state,
  binding retries, captured rest orientation, resource caches and flight list.
  Existing rotation audio loops are stopped. A repeated load into the same zone
  at the same time takes this reset too. Uninstalled turrets are a no-op, and
  `[Dev] LateJoinRestartReset=0` retains the A/B behavior. The native queue rebuild
  still runs last and composes with the existing turret dispatch hook.
* With `[Dev] LateJoinProbe=1`, one `jip audit:` line is emitted after the epoch
  passes, then at gated frames 1 and 30 (once each, even on repeated gate calls).
  `entities=nonparty/registered` and `flags=hash` cover every non-party entity's
  dense index, registration, four object/cache flag words and flag-16
  bitmap membership through installed `vt+0x44` / `vt+0x40(16)` interfaces.
  `diff=N[index:object_word0/cached_word0/bitmap,...]` names the first eight local
  inconsistencies. Unregistered objects contribute object flags only.
  `vis=activate/deactivate/remove/actonvisible/shy_inhibit/shy_permit` counts
  actual dispatches/tests since zone load, with an ordered normalized hash and
  `ids=N[index:event_mask,...]` for the first eight affected indices (mask bits
  0..5 in that order). The activation counts are dispatches, including a repeated
  SetActive with the same value; removal is recorded immediately before dispatch.
  The guarded observation sites are `0x466dde`, `0x466ec3`, `0x466f72`,
  `0x48a533` and `0x48b05b`. The shy observer records the actual projection result.
  `turrets=placements/live/flights:hash` covers scheduler/deadline/target/binding/
  aim/burst/rest state, normalized flight identities and definition-cache names;
  process-local resource handles are represented by state/presence, not addresses.
  The observer bridges preserve integer registers, flags, x87 and SSE state.
  These five observation patches and guards are enabled only with the probe;
  collection ends at frame 30 or a new zone load. Frames 1/30 require the epoch
  sample first; the next late zone load starts a fresh trace.

The mapped restart fixture executes the real retail world cache/bitmap updater,
checks registered and unregistered NPCs and an untouched party hero, and drives
all five probe bridges with the retail stack shapes. The production turret
adapter has its own `xml2_xml1_turrets_restart_test` for same-zone/repeated-time
resets and a generation-normalized read-only digest. These are offline checks;
a live round is still needed for cross-peer behavior. No R2 blanket activation,
R5 roster reconciliation, or R6-R10 state repair is included.


AM follow-up to ZF-3P-E2p: the frame-1 activation set is
`query_class & ~CWorld.flag16 & CWorld.flag15 & grid.window0(shared_camera_box)`.
Deactivation uses `query_class & ~grid.window1(expanded_box) & flag16 & flag15`;
smartent storage/removal uses `query_class & ~window1 & flag17`. Flag 15 is the
visible predicate paired with SetActive by setInvisible; flag 17 is smartent
lifecycle state. `query_class` is CWorld vt+0x40(global[0x718448]+0x24). The epoch preserves those authored object bits and refreshes
their complete indexed record even on an NPC whose active object bit was
already zero.

Both persistent grid query windows (cell extents plus entity bitmaps) are now
invalidated through installed grid vt+0x38 at the epoch, after the NPC pass.
Retail `0x464330` only writes invalid extents and zeros that cache's bitmap.
It changes no entity, cell membership, physical registration or gameplay state.
The next `0x4646d0` stores the shared camera's bounds first and rebuilds each
invalid cache from the grid. Old bounds cannot affect the decision.

CEntityVisibility's routines do not retain a previous results list: they acquire
and release temporary query/order records. `0x4ab670` calls the query reset before
use; `0x4a9ac0` sets order count to zero before filling it. The policy vtable
`0x68202c` calls `0x4dbb30`, which returns constant zero, so this path enumerates
dense indices without a direction sort. The script scratch list `0x756138` is
not read by visibility; setInvisible's surviving inputs here are indexed flags.

The restart fixture runs actual retail `0x466bf0` against production 1024-slot
World/Grid/Query owners. The old AL control reproduces peer A no activation and
peer B `{116,11a,11f}`; AM activates `{116,117,119,11a,11d,11f,120}` on both, in
the same order, with equal spatial caches/indexes. It also tests already-inactive
objects with stale indexed flags 15/17, different prior spatial windows, stale
query/order records and script scratch lists, and party/hidden/smartent preservation.
No blanket SetActive(false) or smartent/invisibility rewrite is added.
