# [Online] Host=broker / JoinCode - rooms through the project broker (Legends Classic 0.3.0 / XML2 Fix 1.8.0)

The in-game broker API (`lc-online-broker/2`) extends this same client and TLS
stack. `[Online] Menus=1`, the compatible LC menu assets, an installed
`Transport=gns`, ReceiveValidation, the checked campaign loader, and a configured
Broker are required for that route. TestMode remains refused. Hub entry performs
no room operation. Launcher Host/Public/RoomName and JoinCode keys are consumed
once as Host Setup / Code presets after real controller acceptance. Host Setup
requires Open, native Game Options acceptance, then native Post; a complete Code
submits one resolve per edit generation.
The legacy non-menu Host/JoinCode route retains its one-shot start.

The broker contract remains protocol 2: `auth`, `cert`, `list_rooms`, `host`,
`resolve`, `turn`, `presence`, `signal`, and `ping`. Intentional cancel/leave
shuts down the worker connection; the existing broker disconnect semantics
remove membership and close a host's room. No new server operation, gameplay
rule, agreement revision, transport framing, or release identity is introduced.
Any future wire/gameplay contract **needs a rule id** from the coordinator.

| Current state | Input / result | Next state and effect |
|---|---|---|
| Idle Hub | Host / Code / Browse navigation | Open the corresponding UI; no host side effect |
| Idle | Browse or Refresh | Authenticate, then list; retain prior rows while pending |
| Idle | Open room | Native Game Options/save/difficulty/name/max choices; no broker room yet |
| Native setup | Native Post / cancel | Accepted settings start host / cancel releases the selected image |
| Idle | Complete Code / selected Browse Join | Authenticate, then resolve, one outstanding operation |
| Authenticating | Verified protocol-2 challenge/auth identity and pinned CA | Listing / Hosting / Resolving; post that operation once |
| Listing | Valid list, including zero rooms | Idle with fresh, atomically replaced snapshot |
| Hosting | Valid code/roster | Room stays unjoinable until actual native Host activation |
| Resolving | Valid code/roster; compatible token | Room -> authenticated host tunnel -> room-only discovery -> native admission -> Join |
| Pending | Back, edit of submitted Code, close or route departure | Canceling; invalidate request/room generation, shut down worker socket |
| Room | Intentional Back/Leave from custom screen, or a change to committed Code | Leaving; release tunnels/listener/queues and room-scoped state |
| Canceling / Leaving | Worker has finished | Idle; no game-thread join/wait |
| Pending | Timeout, invalid reply, broker error or TLS refusal | Retry wait; retain last list as stale; discard late completions |
| Retry wait | Retry / explicit Refresh after limiter and backoff | Authenticate and retry the chosen operation |
| Room | Broker loss, inbox overflow, bad/refused certificate or identity mismatch | Remain Room; retain virtual address/roster/tunnels and service flush/poll, stop new admission; only explicit Leave releases ownership |
| Any start | Missing dependency / TestMode / invalid room name | Refused; no network success advertised |

One shared lookup budget reserves at least 2 seconds between starts, including
authentication. It also paces host/retry attempts conservatively. Back, Leave,
re-entry and manual Refresh do not reset it. Failures back off 2/4/8/.../60
seconds; `rate_limited` imposes at least 10 seconds. A successful operation resets
backoff. Browse automatically refreshes after 10 seconds idle following a
successful reply; failures need an explicit retry. Each attempt has a 10-second
deadline. The UI never waits for network, DNS, TLS or worker completion. If native
networking initialized GNS first, an idle instance may adopt the install identity
through the existing GNS ResetIdentity API. Live native links prevent that
switch and refuse the broker attempt; they are never disconnected by Hub work.
A launcher code preset cannot filter native discovery before user selection.
Cancel/Leave release room mode so Other ways can restore the native IP listener;
the sendto/recvfrom adapters delegate directly while room mode is inactive. The
native GNS identity is captured before room adoption and restored, discarding
the room certificate, only after all native links/strays are idle. This idle
check occurs immediately before listener/identity changes, not solely at the
earlier menu request. A committed room never resets its GNS context because of
a broker error. An idle fresh Hub does not initialize GNS.

Changing the committed value on the custom Code screen (Delete, Clear, Back
deleting a symbol, or Paste replacing it) intentionally performs Leave and
`clear_room()` before a new resolve. An unchanged value/ignored key does not.
The screen remains Code; a later complete value is subject to the same lookup
limiter. This is the custom broker-Code policy, not native in-session departure
or admission. LC must show "Editing the code leaves this room" in its committed
Code status copy.

An invalid launcher RoomName is discarded as a value; it does not disable
Browse or Code. Host falls back to the derived default name. The one-shot name
is removed even without Host/JoinCode. Launcher presets do not start the legacy
status file until an accepted menu action actually starts an operation.

There is one worker slot and one pending user operation. Socket shutdown
interrupts connect/TLS/receive work. A synchronous Winsock DNS lookup may finish
later; its result is canceled by generation and the slot is not reused until
the old worker exits. No unbounded canceled-thread pool is created. Every inbox
entry carries connection, request and room generations. Protocol 2 has no request
IDs, so canceled/timed-out work uses a fresh connection; stale identities cannot
be attached to a newer room.

The existing 16 KiB **including LF** line bound is sufficient: broker SPEC 3.4
and its `_op_list_rooms` cap the list at 50 summaries and remove whole trailing
summaries until the serialized reply fits that bound. The client refuses an
oversized line, 51 rows, duplicate/noncanonical codes, malformed JSON, invalid
UTF-8, duplicate object keys and invalid known field types. It never truncates
a room identity or publishes a partial malformed list. Optional metadata stays
unknown. Inbox/outbox each retain at most 256 entries and 256 KiB; worker pending
plaintext is capped at 64 KiB, TLS buffered input/handshake input at 64 KiB.
Inbox overflow fails a pending attempt instead of evicting an identity/
completion. In a committed room it closes only the broker worker and blocks
admission, retaining the room generation, virtual address, roster and live
tunnels. Certificate or identity errors follow the same room-survival policy;
Retry/Host/Resolve cannot replace that room before intentional Leave.

In-game room names are validated as trimmed printable ASCII, at most 32 bytes,
excluding semicolon and quote. Invalid input is refused, rather than silently
edited into another room name. Native screen names remain at most 31 game
bytes. All remote displayed names use the round-3 renderer allowlist. Browse
RTT stays `--`/Measuring; SAME means only an equal known release token. Unknown
tokens never claim SAME. Full, nonjoinable, stale and password-protected rows
cannot be selected for Join through this password-free UI.

`online_status` v1 retains its process-final error semantics. Per-attempt
retry/cancel/service errors are local `client_view` fields; retry does not reset
the legacy file's final error. Successful Code or Browse resolve stores the
canonical code and allowlisted room title in the existing bounded local Rejoin
record. Broker membership is not native admission or Playing.

Host evidence: `xml2_online_broker_test` executes the production worker/lifecycle
against invented socket and mock-TLS transcripts (the mock does not verify a
cryptographic pin); `xml2_broker_tls_test`
separately checks real certificate trust and protocol selection. Its optional
`--live` handshake/record path is a separately scheduled controlled-broker gate. Browser and menu fixtures
cover limits, backoff, stale data, navigation and renderer bindings. These
fixtures do not establish game rendering, real broker connectivity, native
admission or healthy gameplay during broker loss. Room-survival fixtures drive
the real `ui_tick()`/`tick()` using a real in-memory GNS socket pair and verify
queued datagram flush, inbound polling, address rewriting and retry refusal
after broker close, overflow, certificate failures and identity mismatch.
The broker fixture answers `cert` like the real broker (a `cert_ok` after `auth_ok` on every connection) and installs
through the room's certificate seam, and checks that Retry, Refresh and Done each succeed on a new connection after a
rate limit, `no_such_room`, `room_full`, version mismatch, timeout or idle disconnect.

Requested-only. Absent `Host`/`JoinCode`: nothing is hooked or written (the step-4 `Transport=gns` LAN mode and the
stock transport are unchanged). Either key implies `[Online] Transport=gns` (gameplay rule 25, revision 4).

```ini
[Online]
Broker = broker.example.org:10020   ; required; Legends Classic writes the project broker and pin
; BrokerPin = <64 lowercase hex>    ; self-signed broker's SPKI SHA-256; omit for public-CA + hostname checks
Host = broker                       ; this PC opens a room when its online session comes up
; JoinCode = K7QN2X9M               ; eight characters; this PC joins that room instead (wins over Host)
; Route = relay                     ; TURN relay only (restrictive networks / tests)
; Public = 1                        ; list the room in the public game browser (default 0: code only)
; RoomName = Weekend co-op          ; the browser's name for it (public rooms; default "<screen name>'s game")
```

## What happens

1. **Key.** One Ed25519 key per install, created on first use in `%LOCALAPPDATA%\XML2 Fix\online\install-<hash of
   the game folder>.key` (DPAPI current-user scope), its public half beside it (`.pub`). Both files are replaced
   atomically; an existing empty, unreadable or undecryptable key refuses room startup instead of silently changing
   identity. The certificate blob is erased after `SetCertificate`. DPAPI is not a boundary against malware already
   running as the same Windows user. The broker derives the identity `legends:<first 8 key bytes, hex>` from the key;
   GNS starts with `str:legends:...` as its identity.
2. **Broker** (net030-broker SPEC.md v2, a background thread): TLS 1.2+ is mandatory. With no `BrokerPin`, Schannel
   checks the system-CA chain and DNS hostname; with a pin, only the leaf's exact SPKI SHA-256 authenticates the server.
   Trust is established before the `v:2` challenge is read. The client signs `"LCB1-auth-v2\0" || nonce || pubkey`,
   sends `cert` and `host` / `resolve(code)` after `auth_ok`, and requests `turn` after the room response. A `ping`
   every 30 seconds stays inside the broker's 120-second idle timeout. JSON lines, decrypted input, and both queues
   are byte-bounded. The certificate (project-CA-signed, 24 h) is installed with `SetCertificate`. Every new broker
   connection delivers a `cert_ok` after `auth_ok` (fresh or cached), so a retry, refresh or later lookup meets the
   certificate its earlier attempt installed: outside a room a repeat for the same identity is ignored (GNS keeps the
   certificate while its identity is not reset), another identity is validated like a first certificate, and inside a
   room any repeat is ignored without dropping the broker. TURN/STUN servers
   go into GNS's `P2P_STUN_ServerList` / `P2P_TURN_*`. ICE uses every candidate kind, or relay only with `Route=relay`.
3. **Room.** The host writes the full eight-character code to `online-room.txt` next to the game for sharing; logs
   show only its first two characters. Codes and broker `host_ok` / `resolve_ok` responses are checked against the
   31-symbol alphabet. The room's protocol-2 metadata comes from the game (contract v1.3/v1.4, the public game
   browser; `max_players` is the accepted native 2/3/4 for the menu route (legacy default 4); read-only live `players` stays server-derived): `public` from `[Online] Public`,
   `name` from `RoomName` (printable ASCII, at most 32, no `;` or `"`; public rooms only; default "<screen name>'s
   game"), `host_display_name` = the online screen name (the session's local player name, +0x3e8 of 0xa53058, set by
   the Play Online menus), `game_mode` `campaign` / `danger_room`, `zone` = the current zone's path (printable ASCII,
   at most 48; empty in the menus, the last level kept in the menus between levels), `zone_name` = that zone's
   zoneinfo savename (the name the game's save screen shows), `in_progress` = latched once a level of the online game
   loads, `joinable` = a free seat and, after the start, `[Online] JoinInProgress`; `version_token` = exactly `[Online] GameVersion` (for 0.3.0, `LC33`), without a fingerprint suffix.
   The browser compares that same token; admission still checks the full content fingerprint and rules.
   `Public`, `RoomName` are one-shot like `Host` (removed from the ini at start). The room opens before the name
   entry, so the first listing may carry the game's default name; changes go out as `presence` updates with only the
   changed fields, at most one per 5 seconds (the broker allows 30 a minute per address). The host sees the code in
   the game: Play Online's Host/Join menu ("Code: XXXX-XXXX", left of Back) and the host lobby ("Room code:
   XXXX-XXXX", beside Back) show it in the bottom help bar - the menu's own desctext slots (0x5bc6b0) laid out by the
   game (0x5ae6b0), put back whenever the menu refills its bar. A joiner learns the host's identity and virtual address from the ordered roster.
   A project-CA mismatch refuses room start.
4. **P2P connections** (`ConnectP2PCustomSignaling`, signals relayed as the broker's `signal` op): a joiner connects
   to the host; two joiners: the smaller identity connects. Accepted only from room members; at Connected the peer's
   proven identity must be the member's (`str:` + the broker's identity), else the connection is closed. The patched,
   project-CA-pinned GNS build (net030-gnslib) refuses unsigned and foreign-CA certificates itself. Signals are capped
   at 8192 decoded bytes and 20 per second per member; duplicate requests on a live tunnel are refused.
5. **Virtual addresses.** The broker's member order, preserved in a vector rather than a `std::set`, assigns the host
   `10.77.0.1` and later members `.2`, `.3`, `.4`. Addresses are not key-derived and are not renumbered on departure;
   tombstones keep rejoins and later joins from colliding with live assignments. Datagrams use link framing kind 4
   (`[4][from port][to port][datagram]`) and reach the receive pump as the sender's virtual address. The embedded
   game-info route at datagram offset +13 is confined to that same sender address and source port.
6. **After admission** the GNS transport (gns_transport, rule 25) carries the game's messages on the same P2P
   connection: a native peer at a virtual address gets a link that borrows the member's connection (hello /
   acknowledgement as on the LAN, the virtual addresses decide who says hello).
7. **Joining in the menus** (0.3.0 launcher flow): with `JoinCode` set, Play Online -> Join Game lists the room's host
   (the LAN search goes to the room's members; master server entries are not listed until the session runs, so the
   list holds the room's game only) -> Enter joins. The launcher-facing status is `online-status.json`
   ([online-status-file.md](online-status-file.md)); `Host` / `JoinCode` are one-shot (removed from the ini at start).
   The older path still works: Join Game -> Connect by IP (`[Del]`) -> type `0` -> Enter.
   Enter IP takes digits and dots only, so the code itself cannot be typed; anything that is not a whole address
   becomes the room host's virtual address (the entry's copy at 0x5cf078 into the address buffer 0xa3bc1c, guarded,
   written only with `JoinCode`); a whole address stays the game's (LAN / direct IP). The host's game info (0x18)
   comes back through the P2P connection, the games list shows the room's game, Enter joins it. Join Game -> Search
   also asks the room's members instead of the local network (the LAN search broadcast goes through the P2P
   connections), next to the master server's list.
8. **The game info's version field.** The 0x18 answer carries `1.30` at +67 - the field the joiner's 0x6090d0 compares
   with its own version (session +0x410) - although `[Online] GameVersion` made the version `LC29`, which the answer
   carries at +113. The joiner files such an answer as another version's game and never lists it; on the test rig the
   stock Connect by IP between two LC29 instances listed nothing for the same reason. Through a room connection the
   joiner gives the field the answering PC's own version (+113) before the game reads it. (The host-side source of
   `1.30` was not found - the only "1.30" in XMen2.exe is 0x6a49b0, which the fix rewrites; worth a game_version
   follow-up, it also breaks LAN search / Connect by IP between LC builds without rooms.)
9. **Room lifetime.** `room_closed` or broker loss stops new admission and is logged; established P2P tunnels are not
   torn down and still end through the normal game/GNS timeout paths. A normal `peer_left` retains its existing teardown.
   Broker errors, including `rate_limited` and quota failures, are logged and never kill a running game.

## Defaults, privacy and limits

Legends Classic 0.3.0 writes `JoinInProgress=1`, the project `Broker` and `BrokerPin`; no player ini editing is
needed. In the generic DLL the drop-in key remains opt-in. Drop-in uses rule 26 revision 6: start alone, admit late
players at the next level change, and keep unseated players watching during solo story segments until the party widens.

The broker caps each internet address at two public rooms and two seats in other people's rooms. Public games
connect players directly, so participants can see each other's internet address; relay fallback is available when
the networks need it. Share a private code to play only with friends.

Contract v1.5 distinguishes `room_closed` (broker `room_not_joinable`, temporarily cannot take new players) from
`room_full` (all four places taken). A broker `room_closed` notification about a vanished room remains distinct
from that launcher-facing join error; see [online-status-file.md](online-status-file.md).

## Status and limits

The 030b local three-player checks passed abrupt guest-loss recovery, agreed-frame removal and rejoin by code.
Owner 030e acceptance on three real PCs including a hotspot PC remains pending; four-player WAN coverage is not certified. A lost or frozen host ends the session for joiners with "lost connection to the host":
there is no host migration. Failed shared-campaign restore may require rehosting. Historical protocol-1/2 smoke
checks are development evidence, not real-network acceptance of this release.

## Wire / agreement

Rule 25 GnsTransport revision 4, values `{1, 1000, framing revision 3, 0}`. Framing 2 introduced the room datagram
kind 4; framing 3 adds agreed lost-player input/cut messages (kinds 5/6). Survivors agree the lost guest's cut frame
before removal; earlier revisions are refused by name. See [gns-transport.md](gns-transport.md).

0.6 round 7 adds presentation-only member reports (kind 7) and host tables (kind 8) on room tunnels, plus per-tunnel
RTT/route sampling for the lobby cards: [online-party.md](online-party.md). They never admit, ready, kick or start;
the proposed rule-25 revision (5 / framing 4) needs the coordinator.


## Native menu bridge

See [online-menus.md](online-menus.md#native-host-setup-and-automatic-admission-round-6)
for the state/flow table, LC nodes, ABI addresses and the pending G/V procedure.
`host_native` accepts the confirmed native name (1..31 bytes), maximum and
campaign/Danger Room choice. These set only existing v2 metadata fields.
Difficulty/load-image selection stays native; no unrecognized broker field is
added. `joinable=false` is sent with host, kept through posting, then an existing
presence update enables it after actual native Host activation. Repeated
activation does not post again. The accepted game mode is retained between
native setup and the first loaded world, where the ordinary live world reader
would otherwise still report the old menu mode.

`admission_target(ticket,address)` requires the current room generation,
authenticated broker room, installed certificate, ICE readiness and a connected
host tunnel whose identity passed the existing GNS callback check. It returns
no address on failure. Native menu discovery suppresses master records even
after native hello binds the transport, and raw UDP game-info replies cannot
seed the room table. The established native admission/Ready/JIP handlers still
own membership and play. Intentional Leave drops queued datagrams, callbacks,
room listener/roster and old-generation worker results before later attempts.
Rules 25/26, their revisions/values, framing and release identities are unchanged.
Any distinct future peer-visible contract still **needs a rule id**.
