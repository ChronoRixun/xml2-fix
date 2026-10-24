# [Online] Host=broker / JoinCode - rooms through the project broker (Legends Classic 0.3.0 / XML2 Fix 1.8.0)

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
   are byte-bounded. The certificate (project-CA-signed, 24 h) is installed with `SetCertificate`; TURN/STUN servers
   go into GNS's `P2P_STUN_ServerList` / `P2P_TURN_*`. ICE uses every candidate kind, or relay only with `Route=relay`.
3. **Room.** The host writes the full eight-character code to `online-room.txt` next to the game for sharing; logs
   show only its first two characters. Codes and broker `host_ok` / `resolve_ok` responses are checked against the
   31-symbol alphabet. The room's protocol-2 metadata comes from the game (contract v1.3/v1.4, the public game
   browser; `max_players` is 4, read-only live `players` stays server-derived): `public` from `[Online] Public`,
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
