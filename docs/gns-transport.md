# [Online] Transport=gns - the game's messages over GameNetworkingSockets

Requested-only (`[Online] Transport = gns`; absent, empty or any other value = the stock transport, byte for byte, and
the released capability digest). Every PC of a session must use it: it is gameplay-agreement **rule 25 GnsTransport**,
revision 6, values `{1, port offset 1000, framing revision 4, 0}` (revision 2: the datagram kind 4 of rooms, docs/online-rooms.md; revision 3: kinds 5/6; revision 4: every participant loss uses the agreed cut frame below; revision 6 / framing 4: the 0.6 party kinds 7/8, docs/online-party.md; revision 5 was skipped - its rule summary collided with revision 4's); a PC with it refuses a PC without it (and the other
way round) and names `25 GnsTransport` in the refusal.

The LAN / direct-IP mode is described below. [Online rooms](online-rooms.md) imply this transport and add
certificates, ICE and relay fallback; room connections carry both pre-admission datagrams and participant traffic.
Legends Classic 0.3.0 enables drop-in by default (rule 26 revision 6). Local three-player abrupt-drop tests passed;
owner 030e three-PC/hotspot acceptance remains pending; four-player WAN coverage is not certified. A lost host ends the session; no host migration.

## What stays stock

The game's UDP socket, the OpenSpy/GameSpy listing, NAT negotiation, the join request, admission (0x18 -> 0x1a ->
0x1b/0x1f with the agreement record) and everything before a peer is a session participant. Nothing is sent over GNS
for a peer that is not a participant.

## What changes

When a native peer (manager list +0x124b8) is bound to a remote participant record (session +0x26c, the broadcast's
walk), the PC with the lower stock address/port connects with GNS to the other one's stock address on the stock
port + 1000 (stock port - 1000 above 64535); the other one listens there. A hello names the connector's stock address
and port so the listener maps the connection onto the right native peer; an acknowledgement completes the binding.
Once the native reliable queue to that peer is empty (everything sent before is acknowledged), its game messages go
over GNS:

| site | stock | with Transport=gns |
|---|---|---|
| 0x60f620 enqueue entry | queue, fragments above 300 bytes | a switched peer's message goes whole to the adapter's buffer (header +0xe reliable -> GNS reliable, else unreliable; +0xf coalesce: a buffered reliable message of the opcode is replaced, the newest last) |
| 0x60d9c0 direct send entry | one datagram | a switched peer: one unreliable GNS message, at once |
| 0x610790 queued count entry | native receive + send queues | a switched peer: buffered + GNS-unacknowledged reliable (from GNS's pending + sent-unacked bytes, never under-reported) + received-undelivered reliable |
| 0x6a4aa4 manager vtable +0x1c | receive pump 0x616340 | the pump, then RunCallbacks, ReceiveMessagesOnConnection, the flush and a 100 ms keepalive, on the game's thread |
| 0x60a913 parser task jump | 0x60dca0 | 0x60dca0, then the received GNS messages enter the game as the parser enters a datagram's |

Received messages enter the game in the datagram parser task, exactly as the parser 0x60dca0 enters a datagram's
messages: a reliable one as a slot-0 event `[opcode][sequence][body]` whose sequence continues the native one (0x6100b0
queues it in the peer's ordered receive queue; the native drain delivers it with its own timing, deferral and the
fix's receive-room bridge), an unreliable one (and a reliable body above the pools' 0x145 bytes, as a reassembled
message) dispatched as itself with the parser's flags (2 when the peer is not connected). A return of 2 keeps the
message for the next tick. Delivering from the pump task instead - it also runs while a zone loads - crashed the
lockstep handler (0x6117b0 -> 0x559eb0 read null at 0x559faf). Each received message and an adapter keepalive every
100 ms update the peer's native last-receive time and frame as the pump does for a datagram: the native peer timeout
(0x6137e0: 3 s notice, 5 s 0x5c, 10 s drop; 50 s while loading) was kept fresh by the stock ACK datagrams. Bodies keep
the native fixed-length rule (length 0 with a body = the opcode's entry at 0xa3e490, as 0x615840 resolves it).

The legacy fragment opcodes 0x12/0x13 are never sent over GNS and are dropped when received from it.

Link kinds (first payload byte): 0 game, 1 hello, 2 hello_ack, 3 keepalive, 4 datagram (room tunnel, framing 2),
5 lost_inputs, 6 lost_ready (framing 3); 7 party_report and 8 party_table (0.6 round 7, room tunnels only, unreliable;
[online-party.md](online-party.md)). Kinds 7/8 are handled by the room module before the tunnel policy and never reach
this adapter's game lane; a build without them drops them as malformed/refused tunnel messages and keeps the link.
Rule 25 is revision 6 / framing 4 for them (`{1, 1000, 4, 0}`); revision-4 builds are refused by name.
Kind 9 removal_notice (0.6 preview fix F, rule 64 OnlineRemovalNotice, [online-party-menu.md](online-party-menu.md)
section 4) is the same kind of room-module message: host to the removed member only, before the tunnel policy. Kind 16 is
rule 63's restore transaction (not sent yet); kinds 10-15 are free.

## A participant lost abruptly (three or more players)

The lockstep keeps every participant's inputs in the synchronizer's input ring (0x79fbb0, getter 0x55a390; one block
of 0x3184 bytes per session index = participant record +0x18): 300 records of 0x28 bytes at +0x268 (valid byte,
delta kind, then 0x24 record bytes whose first u16 is the frame), read index +0x3170, write index +0x3174, next
expected frame +0x317c. The store 0x559560 files a record by its frame (the first one wins; an earlier frame is
ignored) and resolves the delta kinds of the contiguous run; the consumer 0x559190 clears only a run record's valid
byte. A frame's input arrives twice: in an unreliable message carrying the frame (0x07 and kin, with the next four)
and in a reliable one (0x02 and kin) that the receiver numbers by its own count (+0x317e).

When a participant's room connection or GNS link ends, each survivor (`vanished_links`) sends every other survivor,
reliably, the resolved records it holds for that player - the run ending just before its next expected frame, at most
64 (`[5][index][first frame][count][count x 0x24]`) - and again whenever it holds more; records it receives go
through the native store (kind 0, resolved), so every survivor ends with the union. Once its lockstep has run every
frame it has that player's input for (that player's ring empty, the lockstep frame unmoved for 1.5 s - the frame gate does not always enter its waiting mode 2) and the other
survivors' records arrived (or 3 s passed), it announces that player's next frame (`[6][index][frame]`); it removes
the player (the host as its watchdog does, a joiner as the host's notice would) when every other survivor announced
the same frame, or after 8.5 s (logged NOT AGREED; the native host watchdog would remove the player 10 s after its
last message anyway). The host's notice 0x2b cannot overtake this: the host removes only after every joiner announced,
and a joiner announces only while it waits at that frame. Two players: no other survivor, so the player is removed as
soon as this PC waits at its frame. A vanished host is left to the native watchdog and its dialog (no host migration).

Every loss takes this path (rule 25 revision 4): a link ends here when GNS closes it, when another survivor's lost
message names that participant while this PC still hears it (never the host, index 0), and - on the host - when
the native session times it out. The native removals of a remote participant with a bound link in a running zone
(lost codes 0x10cc/0x10cd: the watchdog 0x613790 at 0x613adc and frame/checksum arbitration 0x614810 via 0x6133a0) are
filtered at the entries of the notice 0x611550 and the removal 0x612c40: the link is ended and `vanished_links`
removes the player at the agreed frame (its own `hand_over` calls pass). Loads, other reasons (leave, session
teardown), the host's record and a loss not handed over 20 s after it began keep the native path. Before (030c/030d,
swap-030 F01/F03): a joiner frozen 30 s was removed by the host's watchdog ~1.3 s before GNS closed any link; the
host's link ended without the agreement, the other joiner waited for the host's records until its own watchdog
grace (10 s after its lag report 0x09) ran out and it left - the host played on alone.

Before (1.8.0 candidate 030a, live rel-030 R2/R3/R7): the survivors removed the player at their own stop frames, one
apart, and the frame gate/checksum compare ended the session ~15 s later.

## Receive validation

Before a GNS message enters the inbound queue, `network_validation.hpp` applies the receive table it shares with the
stock-path hook (`receive_validation_rules.hpp`; there is one table, so a message cannot pass one lane and fail the other):
every accepted opcode has its researched minimum/maximum body length, unknown opcodes reject, and no body exceeds the
stock 0x505-byte maximum. The table carries the 0.2.3h captured-byte corrections and the dynamically registered opcodes
(0x20..0x25 NAT mesh, 0x53..0x58 shared state, 0x59 movie completion), which join and drop-in flows send. The reliable
0x00 wrapper recursively validates its inner opcode (dispatch length = payload + 2). Body-aware checks cover the
0x18 and 0x72 strings, the 0x44 counted roster records, and the peer/player indexes in 0x29, 0x46 and 0x5d.

Admission opcodes 0x1b/0x1f remain accepted, but their player indexes must be in the four game slots. The same index
check runs for direct or reliable-wrapped copies on the room's pre-admission datagram tunnel; unrelated stock datagrams
are left to the native parser unchanged. Without rule 24, opcode 0x48 requires the researched 200-byte stock chunk.
With FastTransfer installed, it instead requires the production record format: `D` plus 1..299 data bytes or a
five-byte `Z` zero-run. The receiver's existing chunk/copy bounds still run after dispatch.

## Lifetime

GNS is initialised on the first receive pump (the game's thread, never DllMain). A link ends when its native peer
leaves the manager list, changes address or stops being a participant (closed with linger), or when GNS reports the
connection closed or broken; then nothing more arrives from that peer and the game takes its normal peer-loss path
(the keepalive and messages stop, so its last-receive time stops advancing, as for a stock socket gone quiet). A receive queue past 8192 messages ends the
link the same way.

## Development keys and logs

`[Dev] GnsFakePacketLoss = <percent>` and `[Dev] GnsFakePacketLag = <ms>` set GNS's send-side fake network
conditions (both peers), for tests only. `[Dev] GnsDelayTo = <session index>`, `GnsDelayMs = <ms>` and `GnsDelayTrigger = <file>`
hold this PC's game messages to one participant back (while the file in the game folder exists): killing such a PC
leaves the survivors with its inputs up to different frames, on demand. `[Dev] LostCutLegacy = 1` restores the
1.8.0 candidate's own-frame removal as the negative control. These keys, `GnsDelay*`, `LostCutLegacy`, `OnlineCert` / `OnlineCertFile` / `OnlineIdentity`, and the
`NetFake*` hooks are compiled out of Release (`NDEBUG`) builds.

Rare-opcode traces and GNS debug forwarding share a 20-lines/s capped logger with a later suppressed-count line.
`xml2-fix.log` rotates once to `xml2-fix.log.old` before a write crosses 8 MiB, and control bytes are stripped at the
output boundary so remote text cannot inject lines.

## Build

`cmake -S . -B native -A Win32 -DXML2_FIX_GNS_ROOT=<folder with include/steam and lib/>` (GameNetworkingSockets
v1.6.1 static `/MT`, protobuf and abseil from vcpkg `x86-windows-static`, same toolset). Without `XML2_FIX_GNS_ROOT`
the DLL has no GNS and `Transport=gns` logs that the stock transport stays in use. Native test:
`xml2_gns_transport_test [XMen2.exe]`.

Native participant removals log the entry caller, participant index, both notice
reason codes and flag before the existing removal filter runs. The corresponding
removal logs its caller, record and reason. The best-effort stack can be truncated
by the engine's x86 frame omission; the explicitly captured entry caller is still
available. These rare lifecycle lines are outside the capped packet trace.
Opcode `0x2b` is the native 12-byte participant-removal notice (`0x611550`), so its
presence alone does not distinguish a network failure, frame/checksum arbitration or session
teardown. The diagnostics do not suppress a reset, removal or checksum decision.

### Host self-drop diagnostic

For a native host notice naming its own index zero through the common helper,
the diagnostic records the
caller above the common `0x6133a0` helper. That helper omits a frame pointer, so a
normal stack capture may stop inside the adapter. Its guarded entry stack puts
the upper return at word seven of the `0x611550` entry frame. The diagnostic labels
returns `0x5f2867` (front-end exit) and `0x6149a7` (frame/checksum arbitration), and retains
an unknown return numerically. The read is exception-contained and changes no
removal/filter decision. The next live reproduction must establish which path
caused the unexplained self-drop.


For return `0x6149a7`, the same notice hook also reads the native checksum
manager `0xa536d8`: state, next frame, period and current simulation frame, then
all four sender-indexed lists' counts and first sample frame/hash. Each line
includes the native participant index, record, flags and name; `0x2e` queues by
that sender index, including the local sample. `readable` and `present` distinguish
a failed read, an empty list and a real zero hash. Reads are exception-contained;
participant traversal and output are bounded to four slots. No iterator advances.
The guarded `0x61499b..0x6149be` sequence proves that list clearing happens after
the helper returns, so the notice precedes removal and retains the arbitration
inputs. This uses the existing notice bridge, including when JoinInProgress is
disabled; its checksum hold hooks are unchanged.

Arbitration is reached after a same-frame checksum disagreement OR an input-frame
mismatch (the latter tail-jumps from `0x606caa`). `0x6149a7` alone does not
distinguish those triggers or valid selection from fallback. Two different
nonzero hashes tied one-to-one can select index zero. Capture the per-index
frames/hashes on both PCs to investigate actual divergence versus stale or
misaligned samples/arbitration; the log does not declare the host at fault or
change native selection/removal. Neither candidate call is a broker timeout.
