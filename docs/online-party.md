# Online Party: member reports, host table, lobby cards and the status mirror (0.6 round 7)

Presentation data for the four pre-game lobby cards (`host` / `join` native skins, 05-lobby), the Browse PING cell and
`online-status.json`. Nothing here admits, readies, kicks, starts, seats or simulates anything: native readiness
(`0x614240`), admission (GCAP agreement + native join), Start, Kick and the lockstep stay the native/rule owners'
facts. A report is a claim a peer makes about itself, shown as such; a PC's own observations always win.

Pure part: `src/online_party_core.hpp` (format, validation, rate limits, the model). Wiring: `online_room.cpp` (send,
receive, local measurements), `gns_transport.cpp` (`party_snapshot`: native session facts), `online_menus.cpp`
(card/Browse bindings), `online_status*` (additive status block). Host fixtures: `test/online_party_test.cpp`,
`test/online_status_test.cpp`, `scripts/test_status_readers.mjs` (the released launcher readers).

## 1. Framing table (checked before allocation)

The GNS payload's first byte is the link kind (`gns_transport_core::kind`). At f90bc91 kinds 0-6 are used: 0 game,
1 hello, 2 hello_ack, 3 keepalive, 4 datagram (rooms' pre-admission tunnel, framing 2), 5 lost_inputs and 6 lost_ready
(framing 3). Nothing else in `gns_transport.cpp`, `online_room.cpp` or the tests writes a raw kind byte; `parse`
returns invalid for every other value. This round takes:

| kind | name | direction | lane |
|---|---|---|---|
| 7 | `party_report` | every room member -> every other connected member (mesh) | unreliable, no Nagle |
| 8 | `party_table` | the room host -> every connected member | unreliable, no Nagle |

Both travel **only on room tunnels** (the GNS P2P connections `online_room` owns, whose remote identity GNS
authenticated against the broker's member list). Direct-IP GNS links (no room, no broker identity) never send or
accept them. Unreliable on purpose: reliable bytes would enter the GNS transport's reliable-backlog accounting
(`outbox::outstanding`, the native queue query `0x610790`) and change native message timing. Loss is covered by the
periodic refresh; reordering by the sequence number.

How each build treats them:

- This build: `online_room::poll` hands kinds 7/8 to the party receiver **before** the tunnel policy; they never reach
  the game's receive queue or `gns_transport::received`. Accepted pre-admission (a Connecting/Checking member can say
  so) and after the transport is bound.
- A rule-25 revision-4 build (f90bc91 and every released one): `parse` says invalid; on a room tunnel `poll` counts it
  as a refused tunnel message (first four logged, then counted) and drops it; on a GNS link `received` logs
  "malformed - dropped" once. The link stays up and nothing reaches the game. So the new kinds are harmless to old
  peers; they just show no reported data.

## 2. Rule impact

**needs coordinator revision (rule 25 GnsTransport).** The framing gains two kinds, so the owning contract changes.
Proposal (not applied - `gns_transport_rules.hpp` still registers revision 4 / framing 3 / `{1, 1000, 3, 0}`):

- revision **5**, framing **4**, effective values `{1, 1000, 4, 0}` while `[Online] Transport=gns` is installed;
  unchanged OFF (rule not registered, stock transport byte-identical).
- Old-peer refusal: revision 4 and 5 refuse each other through the existing agreement (record digest differs) and the
  refusal names `GnsTransport`, as the revision 3/4 split did. Without the bump a mixed party still works: the old
  member's card shows only native/local facts (no reported name/phase/build, ping measured locally), so the bump is
  for reciprocity, not safety. Coordinator chooses whether to bump at integration; switching is the two constants in
  `gns_transport_rules.hpp` (`agreement_revision`, `framing_revision`) - nothing else reads them.
- GCAP stays 108 bytes: the party contract adds nothing to the agreement record.
- The party wire version (`party_wire = 1`, byte 1 of both kinds) is internal to rule 25 framing 4. A **distinct**
  party contract (e.g. reports that ever influence gameplay, a hero chooser, consent) **needs a rule id** (63+ free;
  not picked here).
- Rules 26 JoinInProgress, 14, 10, 27: unchanged. Reports cannot open, close or bypass a seat.

## 3. Identities and the native-index mapping

- **Member key**: the 8 bytes behind the broker identity `legends:<16 hex>` (first 8 bytes of the install's Ed25519
  public key). On a tunnel the sender's key is the connection's authenticated identity; it is **never** read from the
  message. Rows naming other members carry their key.
- **Native index** (session index, participant record +0x18, `0x614210`; the host is 0; the same on every PC): each PC
  maps keys to indices **itself**, every tick, from its own native session: tunnel connection -> bound GNS link ->
  its peer -> participant record -> index (`gns_transport::party_snapshot`). Its own index is the record flagged local
  (flags bit 0). Nothing is cached across a native session change, so an index reused by a new participant maps to the
  new tunnel's key only.
- A report's or table's claimed index is a cross-check only: a claim that disagrees with the local mapping is ignored
  for that row (counted), and a host-table index is used only for a member this PC has no local mapping for
  (presentation, marked "host-reported"). Two members can never share an index in the model; the later claim loses.
- Display names: an admitted member's card uses the **native** participant name (the native list row). A reported name
  is shown only on a pending (broker-only, Connecting/Checking) card and is display-sanitized (`display_byte`).
- No address (real or virtual 10.77.x.y), port, broker id text or code fragment is ever shown on a card or written to
  the status file. Keys are hashed into slot order only.

## 4. Generation and sequence

- **Room**: both kinds carry the 8-character room code. A message for any other code (an earlier room's, delayed) is
  dropped. Tunnels are per room and cleared on leave, so a connection never outlives its room.
- **Sequence**: u32, one counter per kind per sending process, starting at 1 and strictly increasing (so it is also
  monotonic on every connection). A receiver keeps the last accepted sequence per tunnel and kind; equal or older
  (replay, duplicate, out-of-order) is dropped. A new tunnel object (rejoin) or a new connection to the
  same member (its process restarted) starts with no memory. 0 and a wrap past
  0xffffffff are refused (a sender never gets near it at <= 3 messages/s).
- **Table generation**: u16 the host increments whenever its table's member set or any native index changes. Receivers
  show it in diagnostics and the status block; it never authorizes anything.
- **Model generation** (status only): a local u32 incremented whenever the local roster (members, indices, phases,
  host, route) changes; written with the roster in one atomic file write.

## 5. `party_report` (kind 7) - a member about itself

Little-endian; all lengths exact (no trailing bytes); total 23..119 bytes (cap `report_max` = 128).

| off | size | field | rule |
|---|---|---|---|
| 0 | 1 | kind = 7 | |
| 1 | 1 | wire = 1 | other values dropped (future versions need a revision) |
| 2 | 8 | room code | 8 chars of `ABCDEFGHJKMNPQRSTUVWXYZ23456789`, equal to the receiver's room |
| 10 | 4 | sequence | >= 1, > last accepted from this tunnel |
| 14 | 1 | phase | enum below, 0..9; 10+ dropped |
| 15 | 1 | percent | 0..100 only with phase `receiving_campaign`, else 0xff |
| 16 | 1 | claimed native index | 0..3 or 0xff (unknown); cross-check only |
| 17 | 1 | flags | bit0 host-agreement observed (sender admitted by the host's native/GCAP checks); other bits must be 0 |
| 18 | 1 | token length T | 0..32 |
| 19 | T | version token | printable ASCII 0x21..0x7e (the build's `[Online] GameVersion`) |
| 19+T | 1 | name length N | 0..31 |
| 20+T | N | screen name | bytes 0x20..0x7e and 0x80..0xff, no NUL; native <= 31 game bytes |
| 20+T+N | 1 | RTT count R | 0..3 |
| .. | 11*R | RTT rows | key (8), rtt ms u16 (0..60000, 0xffff = not measured), route u8 (0 unknown, 1 direct, 2 relay) |
| .. | 2 | hero | must be 0xffff (reserved: no proven seat/hero bridge in 0.6; anything else dropped) |

RTT rows: distinct keys, never the sender's own key or the receiver-unknown member's (a key not in the active broker
roster drops the whole message). They are the sender's own measurements, shown to others only as "<sender> to X".

Phases (u8): 0 connecting, 1 checking_build, 2 in_lobby, 3 ready, 4 waiting_next_area, 5 receiving_campaign,
6 loading, 7 playing, 8 watching_story, 9 left. A sender derives its own phase from facts only
(`online_party_core::local_phase`): no tunnel to the host -> connecting; tunnel up, transport not bound ->
checking_build; native participant in a lobby -> in_lobby, or ready when its native ready flag is set; JIP-pending
participant -> waiting_next_area; zone loading -> loading; **playing only when the local and every bound participant's
input ring advanced within the last 1000 ms** (real shared input, not `in_game`); left on leave. `receiving_campaign`
(no byte-progress source is wired in this round) and `watching_story` (no proven solo-segment signal) are never
produced by this build; receivers accept and display them for later builds.

Readiness shown on cards is always the native column; a reported `ready` never lights READY and never reaches
`0x614220`/`0x6070b0`.

## 6. `party_table` (kind 8) - the host's view

| off | size | field | rule |
|---|---|---|---|
| 0 | 1 | kind = 8 | |
| 1 | 1 | wire = 1 | |
| 2 | 8 | room code | as above |
| 10 | 4 | sequence | as above, own counter |
| 14 | 2 | table generation | u16 |
| 16 | 1 | row count C | 1..4 |
| 17 | 15*C | rows | key (8), native index u8 (0..3 / 0xff), phase u8, percent u8, rtt-to-host ms u16 (0xffff none), route u8, build u8 |

Total 32..77 bytes (cap `table_max` = 80). Build (u8): 0 unknown/checking, 1 token differs, 2 token same, 3 agreement
observed. Rows: distinct keys, all in the receiver's active roster; distinct non-0xff indices; exactly one row is the
host's own (its key = the sender) and its index is 0 or 0xff; every other index is 1..3 or 0xff.

Sender permission: accepted only from the tunnel whose authenticated key is the broker's `host_id` for this room
**and**, once a native session maps that tunnel, whose native index is 0. Anything else (a guest sending a table, a
spoofed "host" row, a host claiming index 2) is dropped whole and counted.

## 7. Caps

- Members: 4 (the broker roster's cap; the model refuses a 5th key, a table with 5 rows does not parse).
- Bytes: report <= 128, table <= 80 (parse refuses larger before reading fields).
- Send: a report when its content changes, at most 1/s, and a refresh every 5 s; a table on change at most 2/s and a
  refresh every 5 s. Worst case per tunnel ~ 1.2 x 128 + 2.2 x 77 B/s < 0.4 KiB/s.
- Receive (per tunnel, token buckets): reports 2/s burst 4, tables 4/s burst 6, party bytes 1 KiB/s burst 2 KiB. Over
  the limit: dropped and counted, the link stays. After 16 refused party messages on one tunnel the receiver ignores
  that tunnel's party messages for the rest of the connection (logged once); its card falls back to local/native facts.
- No saves, lockstep input, scripts or campaign bytes in either message.

## 8. Lobby cards (05) and Browse

Per native lobby row (existing D-bindings `lc_pN_*`, round 4/5b):

- `ping` / `ms` / `measuring`: the **local** RTT of this PC's own tunnel to that member (GNS
  `GetConnectionRealTimeStatus().m_nPing`, sampled 1/s). No local tunnel but the host table has a value -> the value
  with `lc_pN_to_host` shown ("to host"). Own card: You (as before). Nothing known -> Measuring (neutral), never 0.
- `lc_pN_route` (**new D text**): `direct` / `relay` from the tunnel's GNS flags (relayed = relay); empty unknown.
- `same_build` / `checking_build` / `lc_pN_other_build` (**new**): SAME when the member's reported token equals this
  PC's token or an admitted-agreement was observed; OTHER when the reported token differs; CHECKING otherwise. Never
  "compatible" from the broker alone.
- `lc_pN_phase` (**new D text**): Connecting, Checking build, In lobby, Ready, Waiting for next area, Receiving
  campaign, Loading, Playing, Watching story - from the member's report, overridden by local facts (own card: local
  phase; a native row is at least In lobby). Unknown: empty.
- `hero_fallback`: always "Hero assigned at start" (no proven seat/hero bridge).
- Pending members (broker-roster members not yet native rows) fill the next free cards: `lc_pN_card` shown, native
  ready/host/Kick parts hidden, name = sanitized reported name or "Player", phase Connecting / Checking build. They are
  never native rows, never kick targets, and are never counted in "n of m ready".
- Browse `room_<i>_5` (**new D text**): the measured RTT to that room's host when this PC currently has a tunnel to it
  (the room it is in), else empty (neutral `--` scene stays). No connection is opened per row.

## 9. online-status.json: additive block (contract v1.6 proposal)

`"v"` stays **1**. Every released launcher (0.4.0-rc.1, 0.4.0, 0.5.0-beta.1, 0.5.0-beta.2, main 2aa92c8) parses the
file with rapidjson into an object (any keys) and `readStatus` accepts numeric `v >= 1`, a known `state` and a `role`,
copying only the v1 keys; tested by running their actual `readStatus` from git (`scripts/test_status_readers.mjs`).
`v:2` would also pass them, but nothing needs it: the change is additive. Launchers before 0.4.0-rc.1 have no status
reader.

The v1 keys keep their bytes and meaning (process-final `error`, `ended` on exit, `players` from the broker). After
`"updated"` the DLL appends, in this order:

```
,"party":{"generation":7,"state":"in_game","error":null,"phase":"in_lobby","table":3,"roster":[
  {"slot":1,"name":"Logan","host":true,"you":true,"phase":"in_lobby","ready":null,"build":"same","route":null},
  {"slot":2,"name":"Kitty","host":false,"you":false,"phase":"ready","ready":true,"build":"agreed","route":"direct"}]},
"process":{"pid":4242,"started":"2026-10-27T23:05:09Z"}
```

- `party.generation`: the local model generation (section 4). It also increments on every new in-game room operation
  (Host / Join / Retry after an error), so a launcher can tell a new attempt from the old one inside one process.
- `party.state` / `party.error`: the **current attempt's** state and error id (same names as v1). v1's top-level
  `state:"error"` stays final for the run (legacy meaning); `party.state` returns to `connecting` on a retry.
- `party.phase`: this PC's phase name; `table`: the last accepted host-table generation (null none).
- `party.roster`: <= 4 rows in slot order, `name` <= 31 bytes escaped, `ready` the native flag (null unknown /
  pending), `build` one of `checking|other|same|agreed`, `route` `direct|relay|null`. **No ping** (it changes every
  second and every write changes `updated`; the released launchers' dismissed-session check compares the whole object,
  so volatile fields would re-open a dismissed session) and no address.
- `process`: this process's id and creation time (UTC). Stale rule for readers: a file whose `process` does not name a
  live process with that creation time is a previous run's (crashed/killed: `ended` was never written).
- Writes: atomic as before; the block is written together with the state it belongs to; roster-only changes coalesce
  to at most one write per second (game thread; the write flushes). OFF / no room operation: no file, as before.
- Direct in-game entry (Hub Host/Join without launcher keys) writes the same file, role from the first room operation
  (v1 rule; `party.state` and `roster` follow later attempts of either role).

### What the launcher must change (round 9)

1. `readStatus`: keep the v1 checks; additionally read `party` when it is an object: `generation` (non-negative
   integer), `state` (v1 state names), `error`, `phase` (section 5 names), `roster` (array <= 4 of objects; clip names
   to 31, strip control characters, accept only the listed enum strings; ignore unknown keys), `process` (`pid` integer,
   `started` ISO). Malformed block -> ignore the block, keep v1 fields (never reject the file for it).
2. Session follow: when `party.generation` increases, treat it as a new attempt (clear the previous outcome only when
   `party.state` is not `error`/`ended`); keep using v1 `state` when there is no block.
3. Dismissed-session comparison: compare `{role, state, code, party.generation}` instead of the whole object, so
   `updated`/roster writes do not re-open a dismissed session.
4. Stale file: before adopting a running-game status, check `process.pid` + `started` against the running game process;
   mismatch -> stale (show nothing, do not adopt). No `process` key -> v1 behaviour.
5. Room card: show roster names/phases/build badges; never show ping or addresses from the file.

## 10. Live procedure (G/V, coordinator scheduled; nothing here was launched)

Four PCs (or VMs) on the candidate with this DLL and `[Test] NoActivate=1`; one PC behind a hotspot or
`[Online] Route=relay` for a relay route. Before each launch follow `_rules-2026-10-24.md` (HOLD-GAMES, slot.py --wait,
registry export/compare).

1. **Four-member lobby.** A hosts (private code); B, C, D join by code one after another. On every PC within 2 s of
   each change: four cards, the same names in the same P1..P4 order (native index order), HOST on A only, You on the
   own card only. Pending phase visible on the joiner's card on A while it is admitted (Connecting -> Checking build
   -> In lobby). Log lines `online: party:` show accepted reports/tables per member and zero refusals.
2. **RTT provenance.** Each guest card shows the local RTT to every other member (mesh), `direct`/`relay` matching
   the `online: connection to ... route` log line of that tunnel; the relay PC shows `relay` on both ends. Kill one
   guest-guest path (firewall that pair's UDP): its cards fall back to the host table value with "to host", or
   Measuring - never a stale number. Own card: You.
3. **Build badges.** Same-token members: SAME BUILD after the token report, before admission; a member with another
   `[Online] GameVersion` (fixture build): OTHER BUILD and refused natively as before (badge never admits).
4. **Ready / Start.** B readies: READY appears from the native column on all PCs; status roster `ready:true` within
   2 s. Spoof check (dev build or fixture peer): a report claiming `ready` for C does not light C's READY anywhere.
5. **Next area / load / watcher.** Start with three; D joins late: D's phase Waiting for next area on all PCs, then
   Loading at the zone load, then Playing **only after** its pad moves its hero (inputs flowing); A/B/C go
   Loading -> Playing around each transition. Status `party.phase` matches. A solo segment shows no fabricated hero.
6. **Old peer refused.** With the rule-25 bump applied by the coordinator: a revision-4 build joining is refused and
   the refusal names `GnsTransport`. Without the bump: it joins; its card shows native name, local ping, no reported
   phase/build; its log counts refused kind-7/8 tunnel messages and nothing else changes.
7. **Leave / index reuse.** C leaves, E joins and gets index 2: E's card never shows C's name, phase or ping; status
   generation increments; table generation increments on A.
8. **Launcher reader.** With launcher 0.4.0 and 0.5.0-beta.2 open during 1-7: the Online panel keeps showing the room
   and player count; a dismissed finished session is not re-opened by roster writes beyond what v1 writes already did.

Captures stay outside the repositories; V compares 05-lobby with the approved PNG (badges, long names, missing values,
relay variant).
