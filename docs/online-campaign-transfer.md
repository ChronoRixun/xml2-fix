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
after the native sequence-window check and before either allocation. It checks
the cache counts and leaves one header and one large body available for the
expected sequence. Small messages do not require a large-body slot.

When a valid message cannot fit, the bridge clears that peer's pending datagram
batch and returns the parser's stop result (2). The trailing acknowledgement is
not processed, so the native sender retries. Messages already queued from that
batch remain intact; native sequence checks handle retransmitted duplicates.
The missing in-order message may use the reserved slot, allowing the ordered
queue to drain. Neither its expected sequence nor the campaign chunk counter is
advanced by a deferral.

The entry signature and full receiver, parser, packetizer/ACK ordering, cache
accessor and allocator regions are checked before installation. The existing
transaction provides write verification and rollback. A failed capacity install
retains the existing stock fallback and online refusal policy. The first receive
deferral is logged; the test-pipe status includes a cumulative deferral count.

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
ACK. It checks the unfixed allocation loss, reserved gap capacity, partial
datagrams, duplicate and wrap behavior, byte preservation, signature/region
refusal and rollback. The ordinary campaign receiver's retail/expanded payload
and final-ACK tests remain unchanged.

The stressed live repeat delivered all 10,486 chunks, with equal SHA-256 hashes
for the host's and client's 2 MiB images, then continued for over 60 seconds with
matching party positions and real movement. These are local two-instance checks;
two-PC latency/loss, natural reproduction of the original report, and broader
session histories still need testing. A long but advancing stock-rate transfer
is not proof of the fixed allocation failure.
