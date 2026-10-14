# lcsave: the LC save library

Append-only resident compatibility mirrors `tools/lcsave/zonecompat.py`. Optional
`Data/lcsave/zone-prefix.lcm` attestations bind the saved old map SHA1 to an exact
current map SHA1 after the builder verifies complete definitions and placements
for an unchanged resident prefix. Numbered saved identities also match old ordinal
and placement. Any missing/stale attestation retains exact-hash rejection; malformed
metadata refuses. The durable LC format remains unchanged.

`loaded_zone_metadata` uses current placement provenance for actual runtime guards,
and supplies `fresh_entities` so appended residents retain factory defaults.
`source_placement_sha1` and each entity's `source_placement_index` preserve cached
unvisited records across resaves; a fresh native capture replaces them. The reverse
reader omits matching former smart/dynamic tail states and their exclusive world
dependencies. No older save names alone are treated as proof of unchanged definitions.
Sidecar framing and the release-ledger generation command are documented by the
Python reference. Compatibility needs attestations generated against every supported
previous build; it is not automatically transitive or a new save feature bit.

The sidecar's version 2 also describes original unnumbered name aliases for renamed
tail definitions. A name pair shared by a new resident and an untouched placement
is ambiguous. If an old smart/dynamic state uses it, the record rejects with a
specific ledger explanation; its position is mutable and cannot identify the
original placement reliably. This protects against duplicate old smart objects and
against losing an untouched twin. Full retention for partial promotion of identical
smart twins remains unresolved under the unchanged durable save format.

`src/lcsave/` is the C++ translation of the builder's reference implementation `tools/lcsave` (legends-classic,
branch `feature/save-converter`, 8a7a8ac): the LC save format ("LC save", format 1), the one-time converter from
XMen2.exe's own save files, the reverse direction the loader needs, the saver's direction, and the canonical dumps
the verification compares. The specification is SPEC-new-save revision 2.3 (SPEC-old-save revision 2.1 for the old
format); CONVERTER-PORT.md is the port design. The library has no engine hooks: the disk seam, the start-up
conversion and the talent insertion that use it are separate work.

## API (`src/lcsave/lcsave.hpp`, namespace `lcsave`)

| Call | What it does |
|---|---|
| `load_tables(build_dir, Tables&, error)` | reads `Data\lcsave\catalogue.lcc`, `Data\lcsave\legacy.lcm` and every `.lcbind` beside the build's data files, and verifies them (below). A second overload takes the data folder and the bind folder separately (tests, tools). |
| `convert_old_to_new(old_file, tables[, ConvertOptions])` | an old slot file (0x84 + 0x2fc00 bytes) -> an LC campaign file (`convert.convert_slot`). `ConvertOptions` fixes `saved_at` / the campaign serial (tests) and names the writer. |
| `new_to_old_stream(new_file, tables)` | an LC campaign file -> the old slot image the engine's loader reads (`convert.reverse_slot` through the running build's numbering); bytes the format dropped are zero; a zone record whose placement file changed is not applied (the ledger says so). |
| `old_stream_to_new(stream, tables, NewFileContext)` | the engine's stream (the whole image or the 0x2fc00 payload) -> an LC campaign file numbered by the running build: origin 0, no `LGCY`, the header fields from `NewFileContext` (description, saved_at, slot, campaign serial, save sequence, writer, mods, conversation digest). |
| `dump(file, tables)` | canonical JSON of either format (`dump.dump_any`): indent 1, sorted keys, ASCII escapes, Python's float repr and `round(x, 3)` - text-identical to the reference's dumps. |
| `missing_talents(stream, tables, out)` | every talent a hero's herostat lists (resolved in the hero's talent file first, then in shared_talents) that the hero block's heap lacks, in herostat order, rank 0, base 0; `shared` marks the shared talents (grab). |

Every call returns a result with an error string; nothing throws across the API (the library uses exceptions
internally and catches them at the boundary). `Result.ledger` is one line: the outcome, the start era, the items
era, the zone eras and the drop counters.

## The tables: what is read, and how it is verified

The formats are the builder's (legends-classic `tools/lcsave/lcfiles.py`, builder 0.2.0, its SPEC 70 records the
bytes SPEC-new-save 3.4 / 5.3 leave open): canonical key bytes as `str`, CRC-32 of everything after the CRC field,
`catalogue.lcc` entries with their canonical key (empty when the writing build does not name it), `.lcbind` lists of
(group, position, id, key) with the group / position conventions of SPEC 70.1, and `legacy.lcm` (`LCLGM1`) carrying
every release's fully resolved tables as catalogue ids, the zone ordinal maps keyed by zone id, the holdable shared
talents keyed by hero id, the conversation digest, the hero count, the four enhancement-record ranges and the
fingerprint checks.

`load_tables` checks, and refuses with a message naming the file: each file's magic, length and CRC; the SHA-1 in
every `.lcbind` against the data file it describes (found by the engine's `.engb`, `.xmlb`, `.XMLB` order); every
bind's id against the catalogue's id for its key (id 0 exactly when the catalogue does not know the key); positions
dense where the numbering is a list; `legacy.lcm` made against the same catalogue revision; a reference era present.
The runtime proofs of SPEC 3.5 (registration traces, live counts) are the engine side's: `Tables` exposes the counts
to compare (`hero_count`, `stats_count`, `item_count`, `course_count`, `objective_count`).

Two things are read from the build's data files instead of bind files, exactly as the reference reads them:

- **Zone numbering.** No `.lcent` ships (SPEC-new-save 3.8: identity mode 2 for every zone until N14 closes), so the
  library decodes the zone's placement file (`Maps\<zone>.engb`, else `.xmlb`) with its own XMLB reader (`xmlb.cpp`,
  bounds-checked) and applies `binds.numbered_entities` (the smartent rule of CONVERTER-PORT section 3). The record's
  content hash is the SHA-1 of those bytes - N14 (c) stands: the engine must be shown to open the same file.
- **Herostat talent lists** (for `missing_talents`), from `Data\herostat`.

## Where the specification was open or ambiguous, and what was chosen

1. **The binary formats of SPEC 3.4 / 5.3.** Followed the builder's choices (SPEC 70.1 of legends-classic) rather
   than defining a second set: the fix reads what the build ships. 5.3's code block describes revision 2.1's single
   map, which the era model replaced; SPEC 70.1 defines the era model's binary form.
2. **`KEYS` text** (SPEC 2.3, "the key string that ID had in the writer's catalogue"): the library writes the
   catalogue's canonical key bytes. The reference writes the build's display text (`'/'.join(fields)`), which
   `catalogue.lcc` does not carry. This is the only byte difference from the reference's files on the corpus; the
   first commit of this library read display text and produced 82 of 82 files byte-identical.
3. **Dump names** are the canonical keys with their escapes undone, lower-cased; equal to the reference's
   `name.lower()` for every name in the corpus. They differ only for bytes 0x80-0x9f in a name (the canonical key
   holds `?` there). For an id the build does not name the reference prints the key hash's first 12 hex digits;
   `catalogue.lcc` has no hash for such an entry, so the library prints `<TAG:id 0x...>`.
4. **Item name lists** (NAMB): the builder's tables carry the category number (1 belt, 2 gloves, 3 armor) instead
   of the class text; a refusal for an item of another class names the category, not the class.
5. **The six key-item counters** in the reverse direction compare the canonical key (`key1` ...) where the
   reference compares the display name `KEY1` exactly.
6. **ZONE** has no bind list; zone ids and names come from `catalogue.lcc`'s ZONE keys (the writing build's zoneinfo).
7. **`NewFileContext`** is defined from SPEC 1.1 / 2.2: description, saved_at (FILETIME), slot, campaign serial, save
   sequence, writer version (default 1.4.0, kind 0), writer text, mods, the running build's conversation digest. The
   saver writes no `LGCY`, uses the catalogue's content version, and does not merge a parked set (SPEC 3.6 is the
   engine side's; nothing is parked by construction here).
8. **Writer identity of a conversion** (SPEC 1.1 kind 2 = "XML2 Fix's import of an old save"): `ConvertOptions`
   defaults to the reference's `lcsave 0.1.0` / kind 1 so that files compare with the reference's; the fix may
   set its own.
9. **Missing talents**: the brief's rule taken literally includes the powers of heroes whose heap the engine never
   filled (not yet joined) - 5579 talents over the 82 real saves, about 3 per hero; `TalentAdd.shared` lets the
   engine side restrict the insert to shared talents. Which file wins when a name is in both the hero's talent file
   and shared_talents is not specified; the hero's own file is taken first.

## Places where the reference's behaviour was kept although it reads oddly

- Numbered zone states are written in the order of the **save era's** ordinals (`sorted(names_by_ordinal)`), not in
  ascending reference ordinal as CONVERTER-PORT section 5 says; the saver's direction writes reference order. The
  dumps compare the states as a multiset, as the reference's `semantic_diff` does.
- The reverse writes `def_count` = the number of item definitions in the build, not the pool-cut count the engine
  writes (equal for the reference build: 88 under the 512 pool).
- Where the reference would raise a Python error (IndexError, struct.error, a cp1252 encode error) the library
  returns an error with the same cause in its text.

## Tests (`lcsave_test`)

- Without arguments (CI): every vector of `tests/unit/test_lcsave.py` (keys and CRC-32, SHA-1 / SHA-256 known
  answers, layouts, the numbering rule over an XMLB file written by the test, old save round trip and refusals,
  container damage, the frozen element sizes, catalogue lookups through successor and alias, `align`, the
  conversion of a reference-era and an old-era synthetic save with byte-exact reverse, the talent era rule), the
  API on the same invented tables, and a fuzz of both readers (truncated and mutated files, CRCs refitted so the
  decoders see the damage): an error with a message, never a crash.
- `--corpus <old saves> --python <reference outputs> --data <build> --binds <bind tree>`: converts every
  `saveslot*.save`, compares with the reference's `.lcsave`, `.old.json`, `.new.json` and `reverse_identical`, runs
  the loader then the saver direction on each, and fuzzes a sample of the real saves with the real tables.
  Corpus files, dumps and builds are game data and never enter the repository.
- `--tables <build> --json <file>`: loads a build's shipped set and writes everything `load_tables` parsed (catalogue
  entries, every era's tables, every bind file's rows) as JSON, for a table-by-table comparison with the builder's own
  readers (`lcsave.lcfiles`); on builder 0.2.0's build: 1988 catalogue entries, 8 eras / 144 tables and 35 bind files /
  1688 rows, all equal.


### Revision 2.4 recording and retention APIs

`NewFileContext.zone_captures` holds per-record `ZoneCaptureMetadata` (zone and native f32 timestamp, actual
placement SHA-1, conversation digest, capture status and ordinal/placement/names). With
`require_capture_metadata=true`, missing or incomplete metadata refuses a fresh campaign write. Known historical
metadata remains explicitly unobserved. `loaded_zone_metadata` produces associations aligned with the reconstructed
native map; the runtime resolves actual placements before any removal/restore decision.

`NewFileContext.retained_file` retains LGCY and opaque KEEP sections and pins their original KEYS prefix for campaign
and profile resaves. Unknown extensions cannot assume mutable world-item/list indices stay stable. The profile
converter now emits LGCY. Non-converter unhandled drops refuse. `conversation_digest` provides the deterministic
reference digest. `native_load_demand` is a conservative serialized upper bound, not measured live allocation reuse.
The engine wrapper separately checks available native item/attached capacity.

W11 is open. A native return without observed stream skips is not section-by-section load verification.

Reverse-reader review follow-up: discarded zone records release their exclusive world-item dependencies. Shared
and unowned world handles survive. Preflight rejects numbered states with duplicate placement destinations, and
unsupported capture bits are validated before resets. Successful `new_to_old_stream` results can have a nonempty
`ledger`: callers must log its explanations and notify the player of selective resets. The synthetic parity runner
also accepts `reverse` (full native stream bytes) and `refuse` (no stream plus expected error) fixture modes.

`old_stream_to_new` tolerates only a known native normalization: an unnumbered item state whose world slot is absent
is omitted, with a `Result.ledger` explanation. The native item-restoration lookup treats an absent slot as no item
instance. Numbered dangling references and unrelated translation drops still refuse. Successful runtime writes
log the ledger after the atomic write. Legacy conversion retains its previous drop counts and LGCY bytes.
Synthetic `native-campaign` parity cases provide input.raw, metadata.lcsave and expected.lcsave, testing a complete
native-save direction with observed capture metadata and retained legacy provenance.

`native_zone_metadata` accepts a native payload or old envelope and supplies validated current-table associations,
exact f32 timestamps, flag2 and a zero conversation digest for incoming native transfers with no LC sidecar. It
extracts associations directly from an in-memory temporary translation; it never runs reverse or hashes conversation
files. The runtime installs this metadata before native application. Same-name ambiguity/source factory skips still
need an actual capture sidecar; metadata extraction does not replace available-pool preflight.

## Conversation heard marks across builder eras

The engine ranks loaded conversation files by case-insensitive name, then indexes four consumption lanes per
file (0x45cb5a, 0x45c56e, 0x45c679). A lane counts consumed run-once starts, not line ids or startCondition
ordinals. Historical tables contain hashes of file name plus decimal lane; identical eras share a reference.

New native `ZREC.conversation_digest` is package-aware: SHA-256 of `LCCONV2` plus NUL, the canonical zone key
hash[32], little-endian u32 lane count, and every ordered file/lane identity hash[32]. Dialogue edits do not
renumber these identities. Only an exact current package-ranking digest permits positional reuse. Bare global
conversation-tree digests are legacy provenance keys, never evidence that current ranks match. The pinned
release snapshot for the previously shipped global digest also protects earlier native LC resaves.

`conversation.lcm` emits both historical global keys and qualified package keys, plus the writing build's
current package snapshots. A different package ranking maps surviving identities into their current positions;
removed or unassigned identities get individual ledger notes. Unknown/zero provenance drops heard words with
a note and continues loading. This keeps successful native/online-transfer saves loadable even when their
capture metadata has no authenticated conversation provenance. Other state remains intact. Unknown marks
are not asserted to be retained by this fallback.

Loaded metadata rebases to the reconstructed package digest and retains inferred capture status. Cached-zone
resaves therefore do not remap translated words a second time. Capture hooks and fresh native recording also
write package-aware provenance. Legacy import bytes, LC field widths/layout, and catalogue identities are unchanged.

Sidecar framing remains `LCCONV1` plus NUL, u32 version=1, u32 table count, u32 file length, and u32 body CRC-32.
Each table has provenance[32], zone-key-hash[32], u32 bit count, then identity hashes[32]. Counts are multiples of
four, at most 128. Duplicate keys/identities, bad framing/version/CRC refuse malformed table files. V39 checks
the generated set. Rebuild savefiles after the era ledger changes; game asset preparation caches remain valid.

## Conversation provenance preparation

Historical whole-tree `conversation_digest` values remain era-map keys. They are
not interchangeable with the zone-specific `LCCONV2` package digest. Standalone
whole-tree callers retain version-aware refresh and stable-inventory validation;
engine tables refuse use of that API to label live marks.

LC readiness calls `prepare_conversation_packages` before player-file import/load.
It precomputes the effective package identities for every base or mod-added zone,
using `mod_loader::resolve_file`, the same lookup used by the native file hooks.
Later enabled mods win. Preparation binds to that lookup's executable root, stable
index generation and working-directory context. Capture uses
`prepared_conversation_digest`; restore's `conversation_bits` uses the same pinned
identities. Neither path reads/decodes packages, scans files or resolves mod winners
on a gameplay frame. Missing/unprepared zones and failed preparation refuse.

Overlapped directory notifications cover Packages, Conversations, mods and root
creation/replacement. Polling consumes completed buffers in memory; known directory
metadata-only updates are ignored. File/name changes, mod order/index/context changes,
notification overflow/failure and unstable preparation refuse until restart with
fresh tables. Re-arming a directory notification is asynchronous. No worker performs
game I/O. Initial synchronous preparation can still delay the front end on cold storage.

Conversation-file mod replacements with unknown branch topology, nested package
references, non-PKGB map-package overrides, reparse/unreadable dependencies, effective
packages outside watched roots and mismatched lookup contexts explicitly refuse.
Package ranking overrides using existing conversation identities are supported.
The format and both digest algorithms remain unchanged.

`lcsave_conversation_packages_test` exercises the called capture API and restore
consumer with actual mod indexing, exclusively locked package data, invalidation,
lookup contexts and first-capture/preparation failures. The C1 Python gate runs
that executable; it does not inspect source strings.
