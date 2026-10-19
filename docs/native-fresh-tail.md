# Fresh appended residents in native campaign transfer

Loading an older LC campaign can retain an authenticated resident prefix while
leaving newly appended residents at their factory defaults. The lobby's reset
discards the in-memory LC capture metadata before the native campaign stream is
loaded by either peer. A zero native ordinal alone cannot distinguish that fresh
tail from a historical removal. Native zone records do not contain placement
SHA1s; the native translator normally assigns the receiving build's current hash.

The reverse adapter now carries the authenticated decision in the existing
native stream. It does not add durable LC fields, feature bits, native bytes,
records, ordinal entries, or entity states. For an applied prefix only:

- Sentinel state zero has payload `LCFRESH1`, an ordinary definition-name string
  handle, and zero instance handle, flags, and position.
- That string is `LCFT1|zone-key-hash|source-SHA1|current-SHA1|source-placements`.
  Hashes are lowercase hex; placements are comma-separated canonical unsigned
  decimal indices in retained ordinal order.
- Existing string-table capacity checks refuse an overfull reconstruction before
  exposing a native stream. There is no truncation or reset fallback.

The receiver validates the zone key, actual current placement hash, canonical
framing, unique bounded source indices, a nonempty proper prefix, every saved
ordinal/name association, and an entirely empty appended ordinal tail. Invalid
recognized metadata refuses the transfer before native load. The receiving
build supplies the fresh tail's current names and placement indices. Its optional
prefix sidecar is unnecessary: source history, like the transferred entity state,
is supplied by the host, whose LC reverse adapter already authenticated it.
This is not independent authentication of the old map by the receiver.

Unmarked records keep existing zero-slot removal semantics. Missing host-side
attestations still discard an incompatible record. Cached, unvisited marked
records retain their source SHA1 and source placement indices when saved to LC;
a real capture replaces them with current observed state. No marker is persisted
in the durable LC container. New Game has no marked records.

Native behavior supporting the carrier:

- `0x465c27` rejects a zero ordinal-map entry before dereferencing state zero.
- Dynamic and smart lists cannot refer to zero in validated native input.
- `0x466090` marks names from state zero as live during string collection;
  `0x4662c0` remaps those handles during compaction.
- `0x4664e0` calls clear at `0x465ec0` before a new capture. Clear resets the
  sentinel name handles and invalidates its magic without interpreting an item
  payload, since its item flag is zero.

GCAP protocol **4** requires this transfer contract and refuses protocol 3 or
older before native admission. Agreement framing remains 108 bytes; no gameplay
rule ID is allocated. The existing hook transaction and restore hook are reused;
there are no additional patch sites. The durable save schema and release/content
version constants are unchanged.

Synthetic verification is in `lcsave_durable_parity`: the companion builder's
`export_lcsave_zonecompat_vectors.py` includes both native layouts, a missing
receiver sidecar, full fresh/source metadata, cached resaves and malformed input
refusals. Live acceptance is recorded separately from these codec tests.
