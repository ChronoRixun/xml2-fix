# Motion pool

`[Limits] MotionPool=22528` raises the native heap motion singleton from 500 to 22,528 records of 0x20 bytes.
Absent or `500` retains stock bytes and no agreement entry; other values are refused. This is independent of
the ActorSlots owner cache. Rule 51, MotionPool revision 1, advertises `{22528,0,0,0}` after verified installation.

The installer runs after ActorSlots publishes its capacity. It checks owners x 176 <= 22,528, validates the
published capacity byte (40 stock), and refuses if the motion pool already exists. The pool stays lazy and
uses the game's allocator. The new allocation size is 0xdd61c; no DLL-owned pool backing is substituted.

The generated 82-row ledger covers 54 field operands, 14 capacity operands, three last-index operands,
three bitmap-clear counts, three lazy allocations and five owner/reader caves. `tools/generate_motion_pool_sites.py`
reads only block R3 from the approved machine-readable ledger and supports `--check`. The destructor creation at
0x56a528 and clear count at 0x56a125 are included alongside the other two instances of each.

Every owner has 176 handle entries at +0xc..+0x2cb and count at +0x2cc. The guard at 0x56a663 validates the index
before the existing-entry path and the count before appending. Clips 0..175 retain native behavior. Later clips
leave the count/array untouched and exit through the native no-data unwind path, never continuing to 0x56a6c6.
They are counted with one log line per owner lifetime. An observer of the native constructor's count reset
resets logging when a cache slot is reused. In-range gaps retain native append-at-count behavior.

The four reader guards use the native stack argument offsets and reject indices outside count or 176,
supplying handle zero to the native read-only fallback path. Nonzero handles keep retail masking semantics:
no generation or active-bit rejection is added, including for released/recycled handles. Record stride,
fallback initialization and owner-cache allocation remain native.

One startup transaction commits the full ledger, the five bounds caves, an allocation call observer and the
owner-lifetime observer. Failure leaves rule 51 unverified; an uncertified rollback refuses startup and retains
compiled DLL caves. Caves live in the DLL's existing executable text. The status consumer uses the published
capacity/live offset and the matching verified full-check signature; it never reads the old live offset after
installation.

`MOTION_POOL` on the test pipe reports `installed`, `requested`, `capacity`, `peak`, `allocations`,
`allocation_refusals`, `clip_refusals`, `reader_refusals`, and per-owner refusal counters. Counters are atomic;
no pipe thread traverses mutable native pool records. OFF remains uninstrumented.

`xml2_motion_pool_test.exe <owned-retail-exe>` executes constructor, allocation, release, lookup, three lazy
creation paths, owner append/teardown and all readers inside an owned test image with invented clip/file/vector
services. It checks the 127 x176 envelope, long lists, canaries, stack/FS balance, masking, rollback and agreement.
The optional `retain` argument exercises an uncertified rollback in a separate process. Live RV4 memory-pressure
and playback acceptance remain pending.
