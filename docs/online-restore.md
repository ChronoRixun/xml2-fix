# Online restore: the shared Load transaction and its failure contract (0.6 round 10, rule 63 OnlineRestore)

One host-authorized, unanimously confirmed operation replaces the campaign of every active PC in a co-op room with the
same verified image, or changes nothing. Round 10 is the **contract and a pure decision core only**:
`src/online_restore_core.hpp` (wire format, validation, host and guest state machines, the re-pick decision) and
`test/online_restore_test.cpp`. Nothing here touches native state, sends a packet, reads a save or enables a Load
button; rounds 11-14 wire it (section 12). No runtime load, rollback or resume is claimed to work.

Sources (binding): INGAME-DESIGN.md rev 2 section 4.4 and 9 / 9.1 (owner answers 2026-10-27), DESIGN.md section 4,
RESTORE-SPIKES.md (native restore/JIP/host-loss paths), IMPL-PLAN.md sections 10-14.

## 1. Words used here

| term | meaning |
|---|---|
| host | the room/native session host (native index 0, `online_room` broker order). The only proposer and the only sender of decisions. |
| member | one PC in the operation's **membership**: its 8-byte room identity key (`online_party_core::key`, authenticated by the room tunnel) and its native session index. |
| active set | the members currently admitted to the native session (playing or watching a story segment). Every one is a voter. |
| pending | PCs in the room that are not active (broker member waiting for admission / next area). Never voters. |
| room roster | active set + pending set, as the host sees them. |
| safe point | a moment the host's integration reports as a supported common boundary: no combat, cutscene, conversation, movie, scripted travel, area load, native admission or JIP bring-in/transfer in progress. Round 11 defines the exact supported set; the core only consumes `safe` + an unsafe reason. |
| image | the canonical native campaign payload every PC will load (W2: at most `0x200000` bytes; only the payload, never the wrapper's process-local cursor/scratch). |
| source | the selected LC save the image was translated from (its file length and SHA-256). |
| rollback capture | a PC's verified capture of the current campaign at the freeze frame (round 11 defines it, including how profile bytes are normalised so equal worlds have equal digests). |
| check | an opaque nonzero 32-bit value each PC reports after its native load (round 12 defines it: loaded-state/seat observation); all must be equal. |

Digests are SHA-256 (32 bytes, never all-zero). Lengths are exact byte counts. All integers little-endian.

## 2. Phases

```
            propose                all Yes                safe                 own image + rollback ready
  idle ----------------> voting ------------> [queued] ---------> preparing ------------------------------> distributing
                           |                    |  ^ unsafe            |                                        |
                           | No/timeout/leave   | roster change,       | failure / cancel                       | every member STAGED
                           v                    v host Cancel,         v                                        v
                        cancelled <-------------+ withdraw ------> cancelled <---------------------------- committed (COMMIT sent)
                     (world unchanged)                           (world unchanged)                              |
                                                                                                                | every member LOADED, checks equal
                                                                                                                v
              ended <---- frozen <------------- any failure between COMMIT and all-LOADED ------------------- repick
              ^   ^          |  verified rollback                                                              | ASSIGN final, all SEATED
              |   |          v                                                                                 v
              |   +----- rolling_back ---- all LOADED ----> resuming (RESUME sent) <---------------------- seating
              |                                                    | first shared frame
              +----- resume timeout                                v
                                                                playing (operation complete)
```

| phase | Propose / Prepare / Distribute / Commit / Resume | world |
|---|---|---|
| `voting` | Propose | original, simulation running |
| `queued` | Propose (consented, waiting for a safe point) | original, running; "Load pending - waiting for a safe point" shown to everyone, host has Cancel |
| `preparing` | Prepare: every PC pauses at the freeze frame, the host captures rollback and stages its own image | original, frozen at frame F |
| `distributing` | Distribute: receivers get the image (FastTransfer, round 11), validate and capture rollback | original, frozen |
| `committed` | Commit: point of no return. Old callbacks/input retired, every PC loads the same image | replaced (in progress) |
| `repick` / `seating` | Resume, part 1: everyone loaded the same image; players re-pick heroes; seats applied | new, frozen |
| `resuming` | Resume, part 2: RESUME sent; Playing only after the first fresh shared input frame | new |
| `playing` | done; the operation is complete, a new one may be proposed | new |
| `cancelled` | terminal, precommit: everyone continues the **unchanged original world** | original |
| `frozen` | postcommit failure: nobody plays; the host chooses rollback or end | mixed / unknown |
| `rolling_back` | a new operation restores every remaining PC's identical verified rollback capture | original (restored) |
| `ended` | terminal: native play ends for everyone (0.6: manual rehost from a local save) | none |

**The commit line.** Before COMMIT nothing destructive has happened anywhere and any party (host Cancel, a guest's No /
abort / failure, a timeout, a departure) cancels for everyone. A guest's STAGED is its binding consent to commit: after
sending it a guest cannot abort on its own and only CANCEL, COMMIT, FREEZE, END or native host loss resolves it. After
COMMIT nobody ever resumes alone: no per-peer local resume, no subset continuing in a different campaign.

**The consistency line.** Once **every** member has reported LOADED with equal checks, all PCs hold the same new world,
so a departure from then on is an ordinary departure (native agreed loss path, rule 25 revision 6): the re-pick
continues with the remaining members and the departed player's hero becomes free (section 7). Between COMMIT and
all-LOADED any departure, failure, mismatch or timeout freezes everyone.

## 3. Membership, generations, authority

- **Exact membership.** PROPOSE freezes the active set exactly (keys + native indices, host at slot 0 / native index
  0, at most four, distinct keys, distinct indices). Every guest compares it with its own active view and refuses
  (FAIL `membership`) if it differs or does not name itself. Members are addressed by **slot** (their row in PROPOSE)
  in every later message. Pending PCs never vote and never receive the image.
- **Session.** A nonzero u32 from the integration naming the native session instance (changes on every new native
  session/rehost). Every message carries the room code and session; another room's or session's message is `foreign`.
- **Operation.** A nonzero u32 the host increments for every new proposal and every rollback, never reused in a
  session. A guest remembers the highest op it saw; a lower op is `stale`. **One operation at a time**: the host core
  refuses `propose` while an operation is open (`busy`), a guest refuses a new PROPOSE while its operation is open.
- **Restore generation.** A nonzero u32 that counts native replacements in the session (COMMIT and ROLLBACK each take
  the next). LOADED / REPICK / PICK / ASSIGN / SEATED / RESUME / FREEZE carry it; another generation is `stale`.
- **Authority.** Decisions (`propose`, `tally`, `queued`, `cancel`, `prepare`, `commit`, `repick`, `assign`, `resume`,
  `freeze`, `rollback`, `end`) are accepted only from the host key. Replies (`vote`, `progress`, `staged`, `fail`, `loaded`,
  `pick`, `seated`) are accepted by the host only from a member of the open operation and only on its own link: no
  payload field names the sender, so a guest can never vote, stage or pick for someone else. A host-type message from
  a guest, a reply-type message from the host or a reply from a non-member is `unauthorized` and changes nothing.
  Host authority is enforced here, below every menu; a hidden guest button is not the guard.

## 4. Consent, timeout, cancel

- The host's proposal is the host's Yes. Every other member must answer **Yes** explicitly; silence is not consent.
- Any **No** cancels (`declined`). No answer from everyone within **60 s** cancels (`vote_timeout`).
- A Yes can be withdrawn (a later No) while voting or queued; a No is final (the operation is already cancelled).
  Changing a No to Yes is impossible. A withdrawal sent while queued may arrive after the first safe tick: a No that
  reaches the host in `preparing` or `distributing` still cancels at once (`declined`; review fix 4).
- The host may Cancel in `voting`, `queued`, `preparing` and `distributing` (`host_cancel`).
- A guest may abort in `preparing` / `distributing` before its STAGED (`guest_abort`), e.g. its player backs out or
  its staging fails (`stage_failed`, `digest_mismatch`, `membership`). The host turns it into a CANCEL for everyone.
- Timeouts (host clock): own preparation 30 s; distribution **progress-aware**: cancel when no un-staged member made
  progress for 30 s (`transfer_stalled`) or after 180 s in total (`transfer_timeout`); load 120 s from COMMIT
  (`load_timeout`, a freeze); re-pick 90 s, then deterministic auto-assignment (section 7); seating 30 s (members not
  seated by then **depart**; the host must be seated or the session ends, `seat_timeout`); first shared frame 30 s
  after RESUME (`resume_timeout`, end: a missing shared frame cannot be pinned on one member). The queue has no timeout: it waits for a safe point, a roster change, a
  withdrawal or the host's Cancel. JIP's 45/90/120 s stage budgets are the reference; round 11 measures real loads.
- A guest that hears nothing from the host for 10 s while `voting`, `queued` or `preparing` (before its STAGED) backs
  out locally (FAIL `host_silent`); the host's once-a-second resend keeps a live host from ever looking silent. After
  STAGED a guest never leaves on its own, with one exception: after **150 s** of total host silence (longer than the
  120 s native load budget) a bound guest ends itself (`host_silent`); it never resumes. The host's integration must
  keep pumping restore messages during its own load.
- Every guest **resends its last answer once a second** while the operation is open (and for up to 60 s after a local
  cancel the host has not confirmed). CANCEL, END and FREEZE are sent once; the host answers a resend about a finished
  operation with its outcome, so a guest that lost one learns it within about a second (review fix 2).
- A cancelled operation always says why (reason + responsible slot) so every PC can show the same text.

## 5. Safe point and the queue (owner answer 9.1, binding)

- A Load is never prepared, committed or resumed mid-fight or mid-cutscene: `voting/queued -> preparing` happens only
  on a host tick whose safety observation is `safe`. Once `preparing` starts, every PC is frozen at the freeze frame, so
  safety cannot change underneath the transaction.
- If the vote completes while unsafe, the operation becomes `queued`: QUEUED (with the unsafe reason: combat,
  cutscene, conversation, joining, loading, travel, menu) goes to everyone, who shows **"Load pending - waiting for a
  safe point"**; the host has Cancel; any member may withdraw. It runs at the first safe host tick, without a re-vote.
- The queued request is **dropped** when the **active** members change while it waits: an active member leaves
  (`member_left`) or a PC becomes active (`roster_changed`). The host must propose again and everyone confirms again.
  A PC arriving to wait (pending) or leaving the wait does **not** drop it (coordinator decision 3, 10-27): it stays
  held outside, exactly as while voting.
- While `voting`, a pending arrival does not cancel either (pending PCs wait outside the vote, as DESIGN.md section 4);
  an active-set change does (`member_left` / `roster_changed`).
- From `preparing` on, pending PCs are held outside (`admission_hold()` is true from PROPOSE until the operation ends):
  no native admission or JIP bring-in starts; they join through normal next-area JIP after `playing`. An active-set
  change is a cancel (precommit) or a freeze (committed) or an ordinary departure (after all-LOADED).

## 6. Distribute, commit, resume

1. PROPOSE carries the **image length and digest and the source length and digest**: the host validated and
   translated the selected save before proposing (round 11/14), so consent binds to exact bytes. The display text
   (save description <= 63 bytes, area <= 31 bytes, saved play time) is for the 08 dialog only.
2. PREPARE carries the freeze frame and whether rollback captures are requested. The host stages its own image and
   captures rollback (`host_ready`); then it is `distributing`.
3. Each receiver acknowledges with STAGED: the exact image length and digest it holds (must equal PROPOSE) and its
   rollback digest (all-zero = none). STAGED means bytes complete **and** digest and structural validation passed;
   PROGRESS (validated byte count) only feeds the stall timer and the loading display. A mismatching STAGED cancels
   (`digest_mismatch`). Rollback is available only when the host and **every** receiver hold a capture with the
   **same** digest.
4. COMMIT (new restore generation) only after every member is staged, the host included.
5. Every PC loads the image and reports LOADED with its check. Local load completion permits nothing: every member's
   LOADED is required, and all checks must equal the host's (`check_mismatch` freezes).
6. Re-pick (section 7), then SEATED from everyone, then RESUME. A PC may produce input after RESUME (the shared frame
   needs it) and shows Playing only when the first fresh shared input frame is observed.

## 7. Re-pick after a load (owner answer 9.1, binding)

After the save loads, **every player chooses a hero again from the save's roster**; heroes keep their saved levels and
gear (the pick is who plays a hero, not its progress). REPICK lists the save's roster (1-32 distinct hero ids) and the
story hero (when the save is in a solo story segment) with its saved owner's slot (unknown = none).

- Normal: each member picks a distinct roster hero; the host grants the first valid pick of a hero, refuses later picks
  of a taken hero (`taken`), and lets a member change to a free hero until the final ASSIGN. If the roster is smaller
  than the membership, the members without a hero watch.
- Story segment: only the story hero is playable. Its saved owner, if still a member, takes it; others watch. If the
  owner **departed** (or is unknown), the host or any remaining player may take control of it so the story can
  continue: the first claim wins.
- After 90 s, unpicked members get the lowest free roster heroes in roster order (story: the host takes an unclaimed
  story hero); no player can hold the room hostage. Final ASSIGN: one hero (or watch) per slot. Somebody always plays:
  if every member chose to watch outside a story segment, the host takes the first roster hero (review fix 7).
- PICK carries the member's own sequence number (1, 2, ... per re-pick); the host accepts only a higher one, so a
  delayed older PICK on the unreliable lane never overrides a newer choice (review fix 8). The final ASSIGN is resent
  together with REPICK, so a member that lost REPICK still gets both (review fix 3).
- A member departing during re-pick frees its hero (and, in a story segment, its story hero for the host / a remaining
  player); the re-pick completes with the rest.

## 8. Failure contract

| when | event | result |
|---|---|---|
| voting | No, 60 s silence, host Cancel, active-set change, guest FAIL | `cancelled`, world unchanged |
| queued | host Cancel, withdrawal, **any room-roster change** | `cancelled`, world unchanged; re-propose + re-confirm |
| preparing / distributing | host Cancel, guest abort/FAIL, digest mismatch, stall/timeout, departure, own staging failure | `cancelled`: every PC unfreezes the unchanged original world at the freeze frame |
| committed (before all LOADED) | departure, FAIL, load timeout, check mismatch, conflicting generation | `frozen`; then `rolling_back` if rollback is verified on the host and every remaining member, otherwise `ended` |
| rolling_back | any failure | `ended` (never a second rollback) |
| repick / seating / resuming | departure, or a member's FAIL (e.g. its loaded roster differs from REPICK: that PC ends itself, `check_mismatch`) | that member's departure (review fix 6); the rest continue the same world |
| seating | a member not seated in 30 s | that member departs; the host not seated -> `ended` |
| resuming | no first shared frame within 30 s | `ended` |
| any | host loss (native `0x612c40 -> 0x6123e0`) | the session ends; before COMMIT every PC still holds its unchanged original world; manual rehost (0.6). An uncommitted image never becomes a recovery source. |

Rollback is a new operation (op + 1, restore generation + 1) on the remaining members with no vote: it restores the
world they all agreed to leave only moments before, from each PC's own identical verified capture. It skips re-pick:
the original seats are restored. A member that staged but missed COMMIT and FREEZE (unreliable lane) takes the ROLLBACK
straight from `staged`: its world never changed, and it reloads its own capture with the others. A member whose
capture digest differs from the ROLLBACK's refuses it with FAIL `rollback_failed` **under the rollback's op** (so the
host hears it at once, review fix 5) and the host ends. `ended` means the integration ends native play for everyone and offers the 0.6 manual
rehost from a local save; personal save slots are never written by this transaction.

## 9. Wire format (link kind 16, wire 1)

Every message: `kind u8 = 16`, `wire u8 = 1`, `type u8`, `code[8]` (the room code, `online_party_core::valid_code`),
`session u32 != 0`, `op u32 != 0`: **19 bytes**, then the body. The length must be exact (one size per type, or exactly
what the counts say); at most 256 bytes. Parsers check structure only; context (room, session, op, phase, sender) is
the state machines'.

| type | name | from | body (after the 19-byte header) | bounds |
|---|---|---|---|---|
| 1 | propose | host | `purpose u8 = 1 (load)`, `count u8`, count x (`key[8]`, `index u8`), `image_len u32`, `image_digest[32]`, `source_len u32`, `source_digest[32]`, `saved_seconds u32`, `desc_len u8`, desc, `area_len u8`, area | count 1-4; keys nonzero, distinct; indices <= 3, distinct; slot 0 index 0; image 1..0x200000; source 1..16 MiB; digests nonzero; desc <= 63, area <= 31, valid UTF-8 names (no controls); 229 B max |
| 2 | vote | member | `answer u8` (1 yes, 2 no) | 20 B |
| 3 | queued | host | `unsafe u8` | 1-7; 20 B |
| 4 | cancel | host | `reason u8`, `slot u8` | known reason; slot <= 3 or 0xff; 21 B |
| 5 | prepare | host | `freeze_frame u32`, `rollback u8` | rollback 0/1; 24 B |
| 6 | progress | member | `bytes u32` | <= 0x200000; 23 B |
| 7 | staged | member | `image_len u32`, `image_digest[32]`, `rollback_digest[32]` | length 1..0x200000, image digest nonzero; 87 B |
| 8 | fail | member | `phase u8`, `reason u8` | known phase/reason; 21 B |
| 9 | commit | host | `restore_gen u32` | nonzero; 23 B |
| 10 | loaded | member | `restore_gen u32`, `check u32` | both nonzero; 27 B |
| 11 | repick | host | `restore_gen u32`, `count u8`, count x `hero u16`, `story u16`, `story_owner u8` | count 1-32, heroes < 0xfffe and distinct; story 0xffff or listed; owner <= 3 or 0xff, 0xff when no story; 91 B max |
| 12 | pick | member | `restore_gen u32`, `seq u16`, `hero u16` | seq nonzero (newest wins); hero < 0xfffe or 0xfffe (watch); 27 B |
| 13 | assign | host | `restore_gen u32`, `final u8`, `count u8`, count x `hero u16` | final 0/1; count 1-4; 0xffff (open) only when not final; 33 B max |
| 14 | seated | member | `restore_gen u32` | 23 B |
| 15 | resume | host | `restore_gen u32` | 23 B |
| 16 | freeze | host | `restore_gen u32`, `reason u8`, `slot u8` | 25 B |
| 17 | rollback | host | `previous_op u32`, `restore_gen u32`, `rollback_digest[32]` | previous op < op, nonzero digest; 59 B |
| 18 | end | host | `reason u8`, `slot u8` | 21 B |
| 19 | tally | host | `yes_mask u8` (bit per slot: who has said Yes, for the 08 ticks) | bit 0 set, bits 4-7 clear; a guest also refuses bits beyond the member count; 20 B |

**Lane and replay.** Restore messages travel only on authenticated room tunnels between the host and each member
(never guest-to-guest, never on direct-IP links), on the unreliable lane like the party kinds (no reliable backlog in
native accounting). Loss is covered by **idempotent resend**: the host repeats its current decision for the open
operation every second; a member that already answered repeats its last answer when it sees a repeated decision; the
host answers any message about a finished operation with that operation's outcome (CANCEL / END / the current
decision; an END also answers a member still on the operation a rollback replaced). Exact duplicates are ignored
(`repeat`, no state change). A different message of the same type for the same op from the same sender is a
`conflict` (refused), except a vote Yes -> No (the allowed withdrawal) and PICK, which is ordered by its sequence
number (a lower or equal one is `stale`). A message for an older op or generation is `stale`; for a phase the receiver
is not in, `out_of_phase`; anything from a member that already departed is `stale`. None of these change state. Sixteen refusals
from one sender within ten seconds make the host cancel the open precommit operation (`protocol`).

## 10. Rule impact

- **Rule 63 OnlineRestore** (coordinator-assigned 10-27): revision 1, effective `{1 on, 1 wire, 16 link kind, 0}`.
  Not registered by round 10 (nothing is installed); the round that first sends restore messages (11/12) registers it
  from `online_restore_core::rule_*` and adds its name to `gameplay_capabilities.hpp` so refusals are named. Every
  peer of a session must agree it before any restore message is sent; a peer without it never sees one.
- **Rule 25 GnsTransport** stays revision 6 / framing 4: kind 16 is a new message kind inside framing 4, carried by the
  room tunnel like kinds 7/8. A framing-4 build that lacks rule 63 parses kind 16 as invalid and drops it (counted,
  link kept); rule 63's agreement keeps that from happening in a session. Kinds 9-15 are left free (rounds 8+).
- **Rule 26 JoinInProgress**: values unchanged. `admission_hold()` holds native admission / bring-in during an
  operation; a JIP bring-in or transfer in progress is an unsafe point. Round 12 must keep normal next-area JIP and
  this path isolated (the native G/H/J/0x26 completions must not auto-start a restore image, RESTORE-SPIKES section 2).
- **Rule 10 Xtraction services**: values unchanged; round 14 routes Xtraction Load to the same host operation.
- **Rule 24 FastTransfer**: the image travels through it unchanged (round 11); no new save-message parser.

## 11. Host fixtures (round 10)

`xml2_online_restore_test` (H only): parser bounds/round trip for every type plus seeded fuzzing (also built with
AddressSanitizer outside the repository build), exhaustive vote permutations for 1-4 members, silence / No / stale /
duplicate / conflict / unauthorized / foreign messages, membership and generation changes at every phase, digest
mismatch, delayed COMMIT and LOADED, timeout and host loss at every phase, queue-until-safe (active change while
queued, pending arrival kept, host Cancel, withdrawal, never prepares while unsafe), resend/replay, the re-pick rules
(conflicts, departures, story owner present/departed/unknown, auto-assignment, nobody-plays, reordered picks), the
review a2f6b3c probe scenarios P1-P7, and 4 x 200 lossy runs (a third of all messages lost, CANCEL / END / FREEZE
included, story rosters) that must reach their outcome - complete, cancel, end, rollback - on every PC, every time. No G or V evidence exists for this round: there is
no runtime path yet.

## 12. What rounds 11-14 build on

Integration contract (review fix 9), binding for every round below:

- `safety(safe, why, freeze_frame)` is fed on **every** host tick before `tick()`; the queue fires only on a tick
  whose observation is safe, and `freeze_frame` must be the frame every PC would pause at from that tick.
- `host_loaded(started_gen, ...)` / `loaded(started_gen, ...)` pass `generation()` as it was when that native load was
  **started** (at COMMIT, or at ROLLBACK). A late completion of an older load (the commit load finishing while a
  rollback is under way) is ignored; it is never counted as the rollback load.
- `first_frame()` means a simulated frame that holds **every** remaining member's fresh input (the native lockstep
  barrier), never a local frame; RESUME stops being resent there.
- A departure the core reports (`departed(slot)`) must be carried out natively by the agreed departure path; a
  departed member's messages are ignored from then on.

- **11 (prepare/transfer):** produce `image_len/image_digest` (canonical payload only) and `source_len/source_digest`
  before PROPOSE; define the safe-point set and the rollback capture (profile normalisation so equal worlds digest
  equal); deliver the image by FastTransfer with commit disabled; feed `host_ready`, `progress`, `staged`,
  `stage_failed`; route kind 16 on room tunnels before the tunnel policy; register rule 63.
- **12 (commit/resume):** on `committed`, retire old callbacks/input and load the staged image on every PC through one
  guarded cohort path (both `0x607fb0` and `0x608170` completions and host-alone `0x609525` guarded); define `check`;
  report `loaded` / `load_failed`; apply ASSIGN seats then `seated`; resume input after RESUME; `first_frame`.
- **13 (failure/unwind):** act on `cancelled` (unfreeze unchanged world), `frozen`, `rolling_back` (load own verified
  capture), `ended` (end native play, manual rehost); host-loss mapping from the native teardown.
- **14 (UI/routes):** 08-load-proposal from `view()` (names, ticks, countdown, reason), "Load pending - waiting for a
  safe point" + host Cancel from `queued`, the re-pick screen from `repick`, pause and Xtraction both calling the one
  host `propose`; guests see host-only guidance.
