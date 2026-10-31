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
   +0xc384[32], write/read/free at +0xc408/+0xc40c/+0xc410) is only ordered by the
   constructor (`0x522f70`); at a late restart's zone load, with every brain free,
   it goes back to identity through that retail init on the stock pool (a relocated
   e4 pool is left to the e4 core). Brain and world generations are untouched.
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
`room_not_joinable` agree with the host's DROP-IN choice; a code join to a
started room with DROP-IN off gets the broker's "Not taking players right now"
before the tunnel instead of the tagged native notice after it.

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
