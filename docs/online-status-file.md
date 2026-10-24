# online-status.json - what the launcher reads (0.3.0, launcher contract v1.5)

The launcher starts the game for "Host online" / "Join by code" by writing `[Online] Host=broker` or
`[Online] JoinCode=XXXXXXXX` into `xml2-fix.ini` (the builder writes `Broker` / `BrokerPin`). XML2 Fix answers in
`online-status.json` next to the game's exe. The shared contract is the launcher <-> XML2 Fix contract v1.5; this page
says what the DLL does.

## One-shot keys

At game start (DLL load) the fix reads `Host` and `JoinCode`, then removes them and the one-shot `Public` / `RoomName` keys from `xml2-fix.ini` with
`WritePrivateProfileStringW` (the launcher's writer; every other line stays as it was) before any room is created or
joined. A later start of the exe directly (Steam, a desktop shortcut) is a normal start and never re-hosts or re-joins.
`xml2-fix.log` says "online: [Online] Host read for this start and removed from xml2-fix.ini".

## The file

Written only when `Host` or `JoinCode` was set at this start (OFF writes nothing). Atomic: a temp file
`online-status.json.tmp` is written and flushed, then renamed over the old file with POSIX semantics (a launcher that
has the old file open with delete sharing does not block it; without delete sharing the fix retries the rename for
about 50 ms). UTF-8, one object, one line:

```
{"v":1,"role":"host","state":"room","code":"K7QN2X9M","players":1,"max_players":4,"public":true,"room_name":"Logan's game","error":null,"message":null,"updated":"2026-10-18T23:05:09Z"}
```

| state | when |
|---|---|
| `connecting` | the start was accepted; the fix signs in to the broker when the game's network layer starts (entering Play Online). A joiner's `code` is the code it joins; a host's is null. |
| `room` | host: the broker opened the room; `code` set, `players` 1 (the host). Also in `online-room.txt` (legacy/test file). |
| `joined` | joiner: the P2P connection to the room's host is up - the host's game answers Join Game's search. |
| `in_game` | the game's session runs over a room connection (the joiner was admitted to the host's game; the lobby counts). |
| `ended` | written on normal process exit; a killed/crashed process writes nothing. After an error the error id and message stay beside `ended`. |
| `error` | `error` names the id below; `message` is English text for logs. An error is final for the run. |

`players` counts everyone in the room including the host, from the broker's member list (joins and leaves update it);
`max_players` is the game's limit, 4. `public` (contract v1.3): the room is listed in the public game browser
(`[Online] Public=1`); `room_name` its name there (`RoomName`, else "<screen name>'s game"; null for a private room
or until the screen name is known). A joiner's come from the room it resolved.

| error id | cause in the fix |
|---|---|
| `broker_unreachable` | no `Broker` configured; the address does not resolve; no answer within 5 s; the TLS session ended before the broker answered; the broker connection was lost before the session started. |
| `broker_rejected` | the broker does not speak protocol 2, or refused the sign-in (`auth_failed`, `unsupported_version`). |
| `code_not_found` | the broker has no open room for the code (`no_such_room`), or `JoinCode` is not 8 characters of the alphabet. |
| `code_expired` | the room closed before this PC's session started (`room_closed`). |
| `room_full` | the broker refused the join: `room_full` (all four places taken). |
| `room_closed` | the broker refused the join: `room_not_joinable` - the room exists but takes no players right now (drop-in off, a level load, the host's game closing); contract v1.5. |
| `version_mismatch` | the room's version token (the host's `[Online] GameVersion`, e.g. `LC33`, carried in the room's metadata and compared by the launcher's browser too) differs from this PC's; refused before any connection. |
| `test_mode_on` | `[TestMode]` is on: online play is refused at start (no broker sign-in, no room). |
| `tls_pin_mismatch` | the broker's TLS certificate fails `BrokerPin` (or, without a pin, the system's trust), or the broker's CA is not the one this build pins. |
| `internal` | anything else (no retail executable, imports missing, an invalid broker answer, a build without GNS). |

## In the menus

Host: Play Online -> Host -> New Campaign / Load Saved Campaign. The room opens when the game's network layer starts
(entering Play Online), not at the main menu: the broker sign-in needs GNS, which starts on the game's thread at the
first receive pump.

Joiner: Play Online -> Join Game: the LAN search goes to the room's members through their P2P connections and the
host's game is listed; Enter joins it. With a join code, until the session runs, the master server's entries are not
listed (the two server-browsing callbacks' games-table allocations, 0x605d55 and 0x606171, return no entry; guarded
5-byte calls, one transaction), so the list shows the room's game only. Seen live before this: the list showed the
room's host twice (room + master list) and the joiner picked the master copy, which joins by the real address around
the room (plain UDP, no NAT traversal or relay). Connect by IP with any non-address text (e.g. `0`) still joins the room.
