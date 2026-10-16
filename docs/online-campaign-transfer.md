# Saved-campaign transfer under receive pressure

With raised entity capacity, the native online campaign image is always 2 MiB,
sent in 200-byte reliable messages. The LC file's on-disk size does not set the
transfer size. The stock sender permits five outstanding campaign messages per
peer, so a successful transfer can spend several minutes on the sending screen.
This change preserves that sender, the native wire format, and the durable LC
format. It addresses a separate failure that can leave the transfer incomplete.

## Confirmed failure

The native message cache has 768 headers but only **50 bodies larger than 60
bytes**. The reliable wrapper at `0x6100b0` allocates a header, then a body, before
queuing the message. If body allocation fails, its failure exit returns 1 without
freeing the header. The datagram parser at `0x60dca0` continues to the trailing
acknowledgement request. The sender can consequently retire data the receiver
never stored. Later reliable messages wait behind the missing sequence forever.

There is also a capacity deadlock: future messages can consume the last body
needed to receive the missing in-order message. Merely retrying the failed
allocation does not resolve that backlog.

Native execution tests reproduce both defects with synthetic pools and payloads.
A local two-instance experiment also reproduced the incomplete transfer with a
temporary 32-message sender window: the host queued all 2 MiB, while the client
delivered only 30 campaign chunks, filled all 50 large bodies, and waited for a
missing reliable sequence. That larger sender window is **test staging only** and
is not part of the fix. An ordinary five-message baseline completed; this work
does not establish that every reported long wait has this cause.

## Receive fix

The entity-capacity startup transaction installs a five-byte bridge at `0x6100e8`,
after the native sequence-window check and before allocation. It checks **all four
shared transport pools**: 768 headers, 768 small bodies, 50 large bodies, and 650
queue nodes. Future receives leave one required slot for the missing expected
sequence. These counts are global; a per-peer receive window is not sufficient.

When a valid message cannot fit, the bridge clears that peer's pending datagram
batch and returns the parser's stop result (2). The trailing acknowledgement is
not processed, so the native sender retries. Already queued messages remain
intact. The expected sequence can use the reserve and drain the backlog.

The pools also serve outgoing traffic and other native lists. A bridge at
`0x60f642` preflights an entire outgoing message before allocation, including the
fragment descriptor, all tagged fragments, and fixed-length implicit bodies.
It leaves one slot in each pool it consumes for reception; small control messages
remain sendable while the separate large-body pool is full. If the send cannot fit, it returns
false before allocation and schedules a connection failure. This also prevents
the native unchecked null-copy when an in-order host-info handler replies while
its received large body is still allocated. Under extreme pressure the join can
therefore fail cleanly instead of crashing; headroom is not an unbounded reserve.

Unrelated native users can still consume a reserve. A pressure-blocked peer gets
a 30-second deadline, reset only by reliable-sequence progress. Repeated packets
do not extend it. The maintenance callback at `0x60a880` checks the deadline even
when no further packets arrive. It verifies peer membership before dereferencing
tracked pointers. A failure is logged once and handled through the native
connection-failure callback at the maintenance boundary, after receive/send
handlers have unwound. Cleanup cannot free a received body underneath its reply
handler. A failure still pending when no transport remains is retired without
native dispatch, so it cannot refuse a later session. The deadline covers pressure stalls, not ordinary slow but advancing
campaign transfers or arbitrary network failures.

All three writes belong to the existing capacity transaction, with complete
instruction signatures, dependency hashes, readback, and rollback. The dependency
hashes include the full final return operands. Failed installation retains the
existing stock fallback and online refusal policy. The first receive deferral is
logged; test-pipe status includes a cumulative deferral count.

The fix does not change save contents, the 200-byte chunks, the final short chunk,
message framing, or gameplay rules. GCAP protocol 4 and all rule IDs/revisions
remain unchanged. It can receive the existing sender's traffic without a new
agreement capability; an older receiver retains its old failure behavior.

## Verification

Build Win32 Release and run, supplying a locally owned retail executable:

```
xml2_reliable_receive_test.exe <retail-executable>
xml2_entity_transfer_test.exe <retail-executable>
xml2_entity_capacity_test.exe
xml2_patch_transaction_test.exe
xml2_gameplay_agreement_test.exe <retail-executable>
```

The receive test executes the native parser and wrapper, including the trailing
ACK. Persistent two-peer tests execute retail pool allocation, sorted insertion,
ordered dispatch and freeing: they fill all 650 shared nodes, reproduce the old
permanent gap, and then prove both queues drain with exact bytes under the fix.
A crowded synthetic host-info handler reproduces the native reply null-copy and
proves safe refusal after the fix. Tests also cover successful crowded replies,
fragmented sends, shared small bodies, implicit lengths, a real 30-second full-pool
watchdog expiry without new packets, one failure notification, and cleanup after
dispatch. Every extra dependency's last byte is mutation-tested; all three hook
entries refuse changes and roll back together. Existing partial-datagram,
duplicate, wrap and byte-preservation checks remain. The ordinary campaign receiver's retail/expanded payload
and final-ACK tests remain unchanged.

The stressed live repeat delivered all 10,486 chunks, with equal SHA-256 hashes
for the host's and client's 2 MiB images, then continued for over 60 seconds with
matching party positions and real movement. These are local two-instance checks;
two-PC latency/loss, natural reproduction of the original report, and broader
session histories still need testing. A long but advancing stock-rate transfer
is not proof of the fixed allocation failure.

## Fast transfer (`[Online] FastTransfer=1`)

The stock sender queues five 200-byte reliable messages per peer and tops the
window up when acknowledgements return. On a LAN that is one ~1 KB datagram per
~80 ms round trip: 10,486 messages, about 165 seconds for the 2 MiB image. The
peer's datagram budget (`+0x212c`, 100,000 bytes/s, set at `0x61097b`) would cap
even a wider window at about 21 seconds. The shared pools allow only 50 large
bodies per process, so a wider window alone cannot reach seconds.

The image is mostly zero: real saved slots have 27,068 and 11,230 non-zero bytes.
With both peers agreed, each `'H'` body is one record covering the next image
bytes in order:

- `'D'` followed by 1 to 299 bytes, which are copied;
- `'Z'` followed by a u32 run of zero bytes (64 or more), which are written as zeros.

A record is at most 300 bytes: one body, never fragmented. Zero records and short
data use small bodies. The two real slots need 367 and 242 records. A fully dense
image needs 7,014.

### Sender

A bridge at `0x608004` runs after the native queued-`'H'` count `0x610790`. It
replaces only that peer's chunk loop and resumes at `0x6080ba`, the next peer.
It queues up to 32 records in flight per peer. It never takes the last 8 large
bodies, nor the last 64 headers, small bodies or queue nodes, which keeps receive
capacity across all peers. The outgoing preflight still guards every send. The
peer datagram budget becomes 400,000 bytes/s.

### Receiver

The receiver bounds every record before writing: unknown tag, empty run,
oversized data, or past the payload end. A refused record is logged once. It
acknowledges with `'J'` exactly when the covered bytes reach the payload.

### Safety and compatibility

The receive admission control and the 30-second watchdog above still apply. Live,
one deferral at 49/50 large bodies was absorbed without failure.

The writes and their region hashes join the entity-capacity startup transaction,
with readback and rollback, and installation writes one log line.

Absent or 0, nothing is written and behaviour is that of the stock sender. It is
a gameplay-agreement rule because both peers must use the same wire format. It
needs a rule ID. Tests: `xml2_campaign_sender_test <retail-exe> [captured images...]`
runs the retail sender with the bridge into the production receiver, and
`xml2_reliable_receive_test` runs a 32-record window with loss, lost ACKs,
reordering and a nearly full shared pool.
