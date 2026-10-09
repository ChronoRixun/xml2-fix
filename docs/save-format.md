# The LC save format: what the fix does with `[Save] Format=lc`

For a mod whose campaign keeps growing (the X-Men Legends I port): XML2's own save file stores every item, talent,
objective, Danger Room course and zone object by its *position* in the tables the game loaded, so inserting one
item or talent silently turns every save made before it into another save. The LC format ("LCSAVE1", `.lcsave`
files in `SaveLC` next to the game's `Save` folder) stores permanent identities from a catalogue the builder owns,
so content can be added without a save break. The format, the catalogue and the one-time converter are the
builder's (its `tools/lcsave` package is the reference implementation; the C++ translation lives in `src/lcsave/`,
`docs/lcsave-library.md`).
This page is about the fix's side: where it sits in the game, what it does at start-up, and what it does not do.

**The key does nothing in XML2 as it ships.** Without `[Save] Format=lc` (or with `off` / `xml2`) nothing on this
page is active and the game reads and writes its own files, byte for byte. The port's builder writes the key.

## Legacy streamed placements that later builds pin

A legacy-import or inferred zone record can store an existing placed entity in its smart/dynamic list even
though the current build numbers that placement. On load, the reverse reader promotes that saved state only
when the definition/instance pair identifies one current placement and one saved unnumbered state, and no
explicit numbered state owns the slot. Explicit tombstones win; absent and ambiguous identities retain the
existing removal policy. Observed native captures without legacy provenance are excluded.

Promotion preserves position, payload and flags, adds native alive bit 8 (the unnumbered list entry proves
existence even when dormant), and removes the entry from its old list. Loaded capture metadata follows the
reconstructed numbered map, including promotions. This prevents a factory's missing inferred ordinal from
silently removing a retained cutscene target. Later saves record the promoted identity as numbered.

The Python reference and C++ reverse/metadata implementations share the rule. Conversion bytes, wire format,
compiled placement data and catalogue are unchanged; previously imported LC files receive the repair on load.

## Where the fix sits: the disk seam

The game's own serializer (`CGame` vt+0x208, 0x46baf0) and loader (vt+0x20c, 0x46e2b0) are not touched: they keep
filling and reading their 0x2fc00-byte stream in the engine's own layout. The one object that moves that stream to
and from the disk is `CMemoryCardWin32` (object 0x6e03d8, vtable 0x69aa8c), and nine of its vtable slots become
the fix's own methods, with the same arguments and the same stack cleanup the retail ones have:

| Slot | Retail | The fix's method |
| --- | --- | --- |
| +0x5c | enumerate slots (device, first, records, count) | fills CSaveLoad's ten 0xa8-byte slot records from `SaveLC\saveslot<N>.lcsave` (the `META` section's description); never sets the corrupt flag |
| +0x6c | read slot (device, slot, u32* size, data) | reads and validates the `.lcsave`, has the library rebuild the engine's stream (`new_to_old_stream`), adds missing talents (below), copies the 0x2fc00 bytes into the game's buffer |
| +0x64 | write slot (device, slot, description, size, data, 0) | tests the overflow flag (below), has the library express the stream as an LC file (`old_stream_to_new`), writes it atomically |
| +0x58 / +0x54 | read / write profile (device, [u32*] size, data) | the same for the 0x228-byte profile, `SaveLC\profile.lcsave` |
| +0x70 | delete slot (device, slot) | deletes the `.lcsave` and its `.bak`; the save sequence's own delete before an overwrite (returning to 0x4b1615) deletes nothing (below) |
| +0x28 | delete every file of the save folder (device) | does nothing and logs (the old folder is never written) |
| +0x2c | profile exists (device) | whether `profile.lcsave` (or its backup) exists |
| +0x74 | message text (code, device, argument) | the game's own text, then the reason the last read, write or delete was refused |

**Writing** follows the spec's procedure: the file is built in memory and re-read through the reader, written to
`<name>.tmp`, flushed, re-read and compared, the previous file kept as `<name>.bak` when it is intact (deleted when
it isn't), then moved into place with write-through. A crash leaves the old file, or the backup plus a `.tmp`, or
the new file. **Overwriting** a slot: the game's save sequence deletes the slot (card vt+0x70, the call at
0x4b1612) before it serializes and writes it; that one delete is told apart by its return address and removes nothing,
so the write keeps the old file as `<name>.bak` and a complete file is on disk at every moment (also when the write is
refused). The menu's own Delete still deletes the file and its backup. **Reading** uses the backup when the file is damaged (size, magic, a CRC, the structure) and says so
in the slot list; a file that is intact but written by a newer format or build (a higher major, an unknown required
feature, an unknown REQUIRED section, a known section's higher major or a higher minor without the droppable flag)
is listed as `(needs a newer version)`, refused with that message when loaded, and its backup is never tried (D10).

The campaign's serial, sequence and retained sections are staged by the card read and committed after the native
load returns without an observed stream failure. The actual resetgame path clears campaign retention; play time is
not a campaign identity. Profile retention has its own lifetime. Only a successful atomic write advances sequence.

## Start-up: tables, then the old saves

At the first file operation (the game's first save menu, or its profile read at start-up) the fix:

1. reads the save folder the game built (`<Documents>\Activision\<SaveFolder>\Save\`) and makes `SaveLC\` beside it;
2. loads and verifies the build's tables through the library: `Data\lcsave\catalogue.lcc`, `legacy.lcm` and the
   `.lcbind` files beside the save-bound data files (their CRCs, the SHA-1 of every data file a bind describes,
   every id against the catalogue). A failure **fails closed** (D11): nothing is read or written in either folder
   for this session, the slot list carries one entry naming the cause, loading it shows the message, and so does a
   save attempt. The menu asks for a profile first and, finding none, writes one; that refusal is the game's "Save
   failed!" message with the cause, except in the main menu's Load Game (CSaveLoad 0x75cbc0, mode +0xd4 = 3), where the
   game's own "Load failed!" text is shown with the same cause;
3. converts the old saves once: every `Save\settings.dat` and `Save\saveslot<N>.save` not yet in the ledger
   `SaveLC\legacy-import.txt` (one tab-separated line per file: name, size, modification time, SHA-1, result, slot,
   fix version, detail) is opened read-only with share-read, converted by the library and written as
   `saveslot<N>.lcsave` (its own number when free, else the lowest free slot; the profile as `profile.lcsave` when
   that doesn't exist). The originals are never written, renamed or deleted. A refusal is a ledger line with the
   library's message and one notice in the slot list; a file whose name, size and time are in the ledger is not read
   again; one whose bytes (SHA-1) were converted before is not converted again (a restored or copied file); a
   refusal is retried by another fix version. When all ten slots are taken the file waits for a free one.

The notice: while the import left something to say, the highest free slot shows `XML2 Fix: <notice>`; loading it
shows the text, saving over it or deleting it dismisses it.

## Missing talents on load

A save made before a shared talent was added never got it (the port's Grab, for one). After the library rebuilt
the stream, the fix inserts every **shared** talent the running build lists that a hero's heap lacks (the library
reports them with a `shared` flag): at rank 0, base ranks 0, into the hero block's 19-entry heap, kept sorted by id
with the count and the bitmap as the engine keeps them. A full heap refuses the insert and logs it; every insert is
logged per hero block. A hero's own powers the save lacks are left to the engine (heroes not yet in the party hold
shared talents only, and the engine grants their powers when they join); the log counts them.

## The stream's silent skips

The four stream helpers (0x4688f0 write, 0x468940 read, 0x468990 tag writer, 0x468a30 tag reader) and the two
inline copies of the serializer and the loader skip a copy that doesn't fit the 0x2fc00-byte buffer and say nothing.
Each of the twelve `cmp ..., 0x2fc00; jge skip` sites becomes a jump to a stub that repeats the compare, continues
when the copy fits and otherwise sets the fix's overflow flag before taking the skip. A write that follows a flagged
serialization is refused with a message, so nothing truncated is ever written; the flag is cleared at every read,
write and enumeration.

## The zone placement file's bytes

A zone record of an LC save is applied only to the zone file it was captured with (identity mode 2: the record
carries the file's SHA-1). The converter hashes `Maps\<zone>.engb`; the fix must hash what the engine actually
opened. `CMap::load` (0x484ce0) opens the zone's own XMLB through the reader 0x564eb0 (call at 0x485689; the path
`maps/<zone>`, from the zone package or the loose file), and the reader converts the file's bytes in place at
0x565153 (`call 0x564d30`, the reader in ecx, the byte count pushed) - the one moment the file's own bytes are in
memory. The call at 0x485689 goes through a bridge that raises a flag while the reader runs; the call at 0x565153
goes through a bridge that, with the flag up, hashes the buffer and logs the SHA-1 beside the one the tables hold
for that zone (`same` / `DIFFERENT`). The pipe's `status` counts the zones hashed and the mismatches. On the port's
0.2.0 build the engine's bytes of `menu/main_back` (327 bytes, from the package) and `haarp/int/haarp2_1` (43,601
bytes) hashed to exactly what the library computed from `Maps\<zone>.engb` - the one proof so far that the two
sides hash the same bytes.

## The test pipe

`save status`, `save dump <file.lcsave>` (the live game through its own serializer, expressed as an LC file; on the
game's thread, at a moment a save would be legal - in a zone, not in the menu) and `save check` (the same, dumped
and compared line by line with the file last loaded; the result is logged). The library's canonical dump matches
the builder's `python -m lcsave dump`, so the file a `save dump` writes can be compared with any other by the
builder's tools.

## Capture and preservation

At the placement loop, the fix records the actual zero-based placement index, ordinal and entity names before
restoration. At capture it pairs those observations with the opened placement SHA-1, conversation digest and native
record timestamp. Cached records keep their own provenance through later saves. A placement/name mismatch resets
that record before a wrong restore; a skipped factory is matched by placement before an empty ordinal is interpreted
as a removed entity. Explicit tombstones preserve the placement identity of removed numbered entities.

Capture status uses bit 0 (mask 1) for incomplete and bit 1 (mask 2) for historical/inferred provenance. Bounds are
checked before the native ordinal access and capture lists can overflow. A new incomplete/unverified runtime capture
blocks campaign writes. Legacy inferred records remain marked unknown. Zero/different conversation digests reset
only conversation bits with a diagnostic. Reader ceilings remain broader than native capacities; unsupported
quantities, positions, stack shapes, references and native allocation demands refuse before narrowing/loading.

Campaign and profile imports carry LGCY, and continuing saves retain LGCY and unknown KEEP payloads with their
original KEYS index prefix. Full-file Python/C++ parity includes canonical KEYS text. Unknown identities still
refuse intact; this is not partial parking. Opaque extensions must use the documented stable/self-contained
reference contract. Native pool preflight uses a conservative serialized demand bound including restore equipment.

## Not done, and why

- **W11:** no side-effect-free full post-load observer is proved. The native return/overflow check is narrower;
  automatic serialization in the menu remains unsafe. The manual pipe check is diagnostic and is not W11.
- **Full parking:** unresolved identities are refused. Complete decode/partition/merge is due before the first
  content retirement update; originals must remain protected by the companion refusal safeguards.
- **Mode 1:** persistent entity tags and .lcent provenance remain deferred; mode-2 records stay readable.
- The kind-3 network transfer and full mod-keyref/effective-overlay support remain deferred. Official online/version,
  mod refusal, newer-original protection and DR/local/online acceptance belong to the companion release work.
- Core runtime hooks are statically guarded and synthetically tested; these statements do not replace live
  save/load, capture/reset and multiplayer acceptance on the combined release.

The decisions, every retail byte the hooks rely on and the pure rules (names, the container header as the slot
list reads it, the ledger, the hero blocks and the talent heap, the stubs and bridges) are in
`src/save_format_rules.hpp`; `xml2_test.exe --save-format-rules` checks them without the game, and against a copy
of `XMen2.exe` when one is at hand.

Revision 2.4 review follow-up: load translation removes world handles owned exclusively by discarded zone records,
retaining shared or unowned handles. Numbered states must have unique placement destinations even when a record
would otherwise be reset. Unsupported capture-status bits refuse before reset decisions. Successful runtime loads
log each reverse-reader explanation and display a notice for zone-state or conversation-bit resets. These fixes
change no field widths, section revisions or container major version. W11 remains open.

Native dangling-item normalization: an unnumbered dynamic/smart item state can survive after its world-item slot
has been released (for example, a zone-record reset during online loading). Native item restore tolerates an
absent slot and cannot bind an item instance. The live writer omits that unusable state with an explicit ledger
explanation, rather than refusing the whole campaign. Live numbered identity loss still refuses; valid referenced items,
carried/key-item counts, capture provenance and other strict no-drop checks are unchanged. Legacy importers keep
their existing recorded-drop/LGCY policy. This is a native bookkeeping normalization, not a wire-layout change.

Native transfer provenance: host mode0 and peer mode2 loads can carry old native zone records without an LC file
sidecar. Their current-table numbering/names are validated before native application, and inferred flag2 metadata
is installed first. Missing metadata is no longer treated as evidence of a placement change. Unsupported inference
refuses before application. This preserves saved zone object/fog state on same-build transfers without claiming
observed capture provenance. The inference path does not hash conversation files or rewrite the incoming bytes.

Dead numbered items with a missing world slot retain their placement/name identity as a tombstone. The native
factory checks `flags_b & 8` at 0x465c35 before item restoration and removes the authored entity when clear.
The writer keeps `flags_b`, position and identity, clears the unreachable item flag/payload, and records the
normalization in its ledger. Alive numbered items remain strict; legacy imports keep their recorded-drop policy.
This revision 2.4 follow-up changes no field widths or section versions.

For a campaign refusal, the runtime now logs the complete translation ledger. Optional
`[Debug] SaveDumpRefused=1` also retains the actual native image as `SaveLC/refused-native-<FILETIME>.save` in the
configured save folder. It defaults off and never replaces the campaign. The dump alone lacks the runtime's
capture sidecar, so an offline replay may infer associations differently from the original capture-aware write.

Legacy Forge contact recovery (maintainer import policy): only pre-LC campaign imports receive the one-based
`x1v03` bit 31 (`0x40000000`) when a catalogue-known current zone, used side-mission return frame, or retained
zone record proves the campaign reached HAARP or a later main-campaign directory. Later mansion hubs count;
NYC's opening mission, the first mansion, training/flashbacks and unknown zones alone do not. The source
first-mansion briefing starts HAARP; subsequent missions preserve the contact flag. Zone names are stored
literally in every supported legacy era, so the gate does not depend on era-specific objective numbering.
An already-set flag is unchanged. A missing word is appended if the native 100-variable store has room;
otherwise import refuses rather than losing an unrelated variable. Only the flag changes, and the import's
LGCY/external ledger records once: "Forge contact unlock granted (0.1.x builds did not record it)".
Native saving, new games and LC resaves never run this repair. Previously imported LC files are not repaired
retroactively. The Python verifier lists this deliberate change in `legacy_repairs` while checking all other
semantics. No wire layout, section version, catalogue or content-version change is needed.
