# lcsave: the LC save library

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
