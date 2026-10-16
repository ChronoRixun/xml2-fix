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
