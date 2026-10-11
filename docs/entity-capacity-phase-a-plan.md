# Phase A implementation plan, implementation revision

2026-10-11, implementation revision of the round6 plan (docs commit cdb4eed), after
two reviews (GPT Sol: revise, dispositions folded into round6; Claude: READY TO
IMPLEMENT with required edits P1/P3, applied in section 0 below and in place).
Actor32 / generic768 / World1024.
Phase B retains actor96, CAI and CAITeam work. Target retail executable SHA-1:
`7d95cdb4a9a599b982147a3389ea7aff211cb82b`.

Contiguous ten-bit indices remain. The revised design removes VM provenance,
limits minted generations for float32 exactness, corrects known legacy saved ID
fields at the LC boundary, and specifies receive retirement and save claim failure.
**The blanket old-token nondurability equivalence was refuted**, not assumed.
The ordered implementation gates are in section8; native/live proof remains due.

## 0. Implementation base and review-required edits

**Base.** The implementation base is the 0.2.0i release head, not the pre-release
clone the round6 verifier pinned: legends-classic `release-0.2.0` **2c95436** and
xml2-fix `release-1.4` **a84474f** (D:/Projects/agent-work/rel-020, candidate 0.2.0i).
The round6 verifier assertions on builder 9569696 / native cdb4eed are superseded by
`phaseA-impl/tools/step0_gate.py`, which re-pins to these heads.

**Step-0 gate (P1).** Before any write set is composed, the gate re-runs, against
the retail PE: the 535 union guards, 341 disjoint ranges, 31 region hashes and all
344 old-byte sites; the base heads; a screen of every XMen2.exe address literal in
the release DLL sources against the guard union, where every hit must be a composed
site, a comment or a read-only call/guard target; the typed registry regenerated
from the release builder's prepared scripts (cache_i, 1538 files) against the plan's
12 keys and fingerprint; and the 82-save corpus (round5 SHA-1s, retail parser) plus
the recorded release Python conversion baseline. Result on 2026-10-11: all pass
(`phaseA-impl/step0-gate.json`): 144 literal hits = 131 composed + 13 read-only,
0 unclassified; registry fingerprint `9dc5e7ad...` identical.

**MoveTriggers fold (P1).** Release commit ac69b65 (MoveTriggers 19->64) writes an
E9 over the six bytes at **4613e0** (`move_triggers_rules.hpp` wrap of the actor
arena teardown, `limits.cpp` retire_actor_times) and guards the whole function
8b0dbc8f7100..c3. Section 2's `AllocatorShutdown` replaces the same function. Both
writes belong to the one transaction (section 7): MoveTriggers' plan is built with
`hooks->retire = 0` (no wrap at 4613e0) and its remaining writes are contributed to
the transaction; `AllocatorShutdown` runs the native teardown sequence once on the
prepared backing and then calls the registered `retire_actor_times` (timer table
retirement, epoch increment) after teardown, exactly where the wrapped original
returned. MoveTriggers' whole-function guard at 4613e0 is checked by the transaction
before the write, as a union guard. With MoveTriggers off, AllocatorShutdown calls
no retire hook.

**Begin-join inside a receive scope (P3.1), proven statically.** `604e30` is a cdecl
packet callback: it tests the event's opcode byte (`'$'` 0x24, `'%'` 0x25) and reads
the context at event+0xc, then calls `6097b0` and **609b80**. Packet callbacks run
inside the 60dbe0 dispatch loop (`60dc3a`), so begin-join can run inside a receive
scope. `6050a0` (lobby/socket-address path, `ntohs` on its sockaddr argument) and
`60a0a0` (UI join from `5b0781`/`5b98f7` menu handlers) also call 609b80. **All three
callers discard 609b80's result** (`FUN_00609b80(...); return 0;` / `return;`), as
does the native path when 609b80 itself returns 0 at `60eec0` failure. Contract on
result 0: a begin-join refused because a receive/list/callback frame is held returns
native 0 immediately and records exactly one owned deferred join (endpoint copied,
never the caller-stack pointer); at depth zero, after the purge of section 5, the
owned join runs through the original trampoline. If that owned join itself returns
0, or the deferral slot is already occupied by a different endpoint, the joiner
failure path (`fail_local(joining=true)`: native 609ad0 then the incompatibility
dialog) runs so the UI is never left waiting on a silently dropped attempt. The UI
has already transitioned (60a0a0 runs its menu changes before the call), which is
the native behaviour for a late failure too.

**Constructor fail-stop (P3.2).** At 48735e the native code is `jz 487369; mov ecx,eax;
call 487440; jmp 48736b; xor eax,eax; ...; 487373: mov [esi+3b0],eax`. A null or
wrong return from `ConstructPreparedSaveManager` would be published at 487373 and
dereferenced later. The wrapper therefore never returns on failure: it poisons the
claim, latches startup/load/save/online failure, logs, shows one modal notice and
terminates with `RaiseFailFastException` (the same disposition MoveTriggers uses for
an impossible pool address). Native unwind through 673cb8 remains the path for a
native exception raised inside 487440, and poisons the claim the same way.

**Reset-time allocation telemetry (P3.3).** The World counts allocation attempts
made while `resetting` (refused and latched per section 3) and reports the count
in the status line and the refusal log; the census/HAARP live gates require zero.

**Two-instance fixture with pre-retired slots (P3.4).** The co-op gate includes a
host whose World has retired slots (forced by the test-pipe `entity retire <n>`
command before the join) so the peer's resolved-entity checksum at 614a40 is
exercised against a non-identical free ring; both matching peers must still agree.

**Explicit tests (P3.5).** Token 0x00ffffff asString->asInt round trip (native
`%i` formatting path, 4d8850) and the float identity cases; a live load of a 0.2.0i
save with populated typed keys (Mastermold catwalk `base`/`part1..3`/`checkpart1..2`,
Mansion `actor_c..actor_g`, rescue `triggerid`/`lastid`) checking the conversion
log counts and the scripts' subsequent behaviour; the test-state entity scan at
`test_state_rules.hpp:424-431` reads the installed layout descriptor (pointer table,
live bitmap, handle table, capacity) instead of the retail 0x778b70 constants.

**Shared online agreement.** Already on the release head (`docs/gameplay-agreement.md`,
`gameplay_agreement*`). The entity layout entry is registered as section 5 specifies
(rule 1 revision 3 `{32,768,1024,10}` when installed and verified; revision 1
`{32,224,256,8}` verified when capacity is off; unverified when requested and
failed); a peer with a different layout is refused at admission and must rejoin.

**Deviations recorded during implementation** are appended to section 10.

## 1. Authoritative review artifacts and exact byte notation

`round6/patch-spec.json` supersedes the Round5 active patch ledger:

- **344 non-overlapping write sites**:223 complete literal old/new strings and121
  exact DLL relocation expressions. Sixteen VM-origin sites are removed; fourteen
  receive/lifecycle sites and two save-constructor/unwind calls replace them.
- **83 new owner vtable slots**, each with a guarded retail pointer and inherited
  ABI evidence. Native tables stay unchanged; new DLL tables use explicit symbols.
- **535 guard records**,341 disjoint ranges covering3,695 bytes, plus31 region
  hashes (five inherited owner regions and26 focused native dependencies).
- The historical closure manifest has1,135 records (1,131 distinct). Removed
  provenance records remain historical evidence, not active writes or obligations.
- Runtime socket IAT67f290 is separate: compare its loaded resolved WS2_32!socket
  pointer, never the file's import thunk.

`abs32(symbol)` is LE32 of the linked Win32 DLL symbol. `rel32(symbol,next)` is
LE32(address(symbol)-next), modulo2^32. Bind after building, verify exact lengths
and non-overlap, and compare installed readback. No DLL exists this round and no
invented symbol address is a runnable patch. Every site, old bytes, new expression,
owner, purpose and overwrite span is in the JSON. Literal examples:

| Site | Old bytes | New bytes / relocation |
| --- | --- | --- |
|4615db |81bed0e60700e0000000 |81be1492180000030000 |
|48733d |68dcaf0100 |68240c0800 |
|487344 |e897920d00 |E8 rel32(AllocatePreparedSaveManager,487349) |
|487362 |e8d9000000 |E8 rel32(ConstructPreparedSaveManager,487367) |
|673cb8 |e8d3b2e1ff |E8 rel32(PoisonPreparedSaveClaim,673cbd) |
|4876c0 |e8db8f0d00 |E8 rel32(ReleasePreparedSaveManager,4876c5) |
|465d05 |85ff743a8b17 |E9 rel32(RestoreFactoryResult,465d0a) 90 |
|60dbe0 |5356578b7c2410 |E9 rel32(DispatchWithCapability,60dbe5) 9090 |

`round6/layouts.json`, `native-evidence.json`, `token-analysis.json`,
`generation-policy.json`, `typed-svar-fields.json`, `receive-retirement.json` and
`save-claim-model.json` supply the companion contracts. Generators under scripts/
write private review artifacts only; the verifier is read-only by default.
Existing SaveCore/SaveFormat writes are composed into the transaction, including
complete guard468a85 with its intervening POP ESI. No independent second installer.

## 2. Owner architecture and memory

Use one immutable `EntityRuntimeLayout` and one prepared owner bundle. Reserve
capacity storage with `VirtualAlloc(MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)` before
publication. Generate stubs into separate RW pages, then make those pages RX and
flush them before publication. Keep vtables and immutable descriptors read-only
after construction. Never pass this backing to the game's heap free function.

Preserve existing game-facing ABIs. World and Grid getters return replacement
objects with separate DLL vtables; their native static objects and native vtables
are not enlarged or globally rewritten. The new owner methods must preserve
native ordering, geometry, class filtering, lifecycle and error behavior while
using the new capacities. This is a substantial port of the affected owner
methods, not permission to substitute simplified query/physics behavior.

| Storage | Planned layout / backing |
| --- | --- |
| Allocator | Actor32 unchanged; generic base `+109a0`, stride `7d4`, 768 slots; complete allocation **189218** bytes |
| CWorld | **24b50** bytes, ten-bit indices, 128-byte entity sets; all five coupled fixed containers scale to1024 |
| Grid | **403574** bytes; preserves coarse map and128-block domain; ordered wide cells described below |
| Query manager | **700c** bytes; three Query objects at offset0, stride544; four-byte array tail atfcc; three Orders atfd0, stride2014 |
| Save manager | **80c24** bytes: five19b88 records plus existing mission/flags/time fields |
| Native stream | Payload **200000**, wrapper **200078**, usable serializer end **200000**, no origin trailer |
| Navigation scratch | Owner-keyed128-byte set plus separate output byte; preserve the CPathGrid object's existing layout |

`round6/layouts.json` gives all offsets. Hexadecimal values in this section omit
the `0x` prefix. Normal actor+generic storage totals800; world1024 leaves224
registrations beyond those arenas. Headroom does not promise arbitrary mod sizes.

Allocator getters/shutdown at `461910`/`4613e0` use the prepared backing and
preserve the singleton pointer at718fbc. Retain the native actor/generic allocation
algorithms with the manifest's generic operand edits and the two full-bitmap-clear
helpers at46155c/4618d6. Actor routing, stride848 and bounds32 stay unchanged.

The World getter is4c7f20. Existing assignments to interface718430 still receive
the replacement through the getter. New World methods implement allocation,
generation validation, destruction, reset, names, hierarchy, relationships,
properties and returned sets. Keep property records16 bytes and property count112;
only the reciprocal entity-membership sets become128 bytes. Preserve reserved
padding and separate internal node/token namespaces.

The Grid getter is465470. A cell has a u16 count, u16 reserved pad and1024 ordered
u16 indices: stride804; sixteen cells make block8040;128 blocks occupy402000
bytes. Keep native insertion/removal order rather than replacing ordered lists
with a differently ordered bitset. Coordinate/extents bytes still describe spatial
coordinates. Block IDs remain bytes for128 blocks. Entity extents start402438;
two caches start40343c with stride9c, each containing a128-byte result set.

Query getter4ab550 and the enumerated QueryRef/OrderRef API entries route to the
new manager. A Query keeps World* at+4 and its bitmap at+8; trailing fields move to
88/8c/90..94; CCollisions starts98 and Query stride is544. CCollisions map begins
**+4**, has1024 byte-valued trace indices; used keys remain u16 at408; count470,
trace-slot list474, trace count4a8, size4ac. The independent native trace pool stays
50 records of64 bytes. The old byte load4a9e0d is bypassed by the replacement,
which decodes the full token before indexing the wide map.

Orders keep eight-byte Entity*/score pairs beginning+4, count2008, Query*200c,
busy2010. Trace-sort callback4a9a80 needs owner-aware dispatch because it retains
Query* at policy+4 and otherwise reads the old+38 collision layout. Other sort
callbacks retain thiscall, one Entity* argument and x87 ST0 results. Do not globally
change shared setter61fde0 or iterator44d0d0; the new query vtable supplies an
owner-specific flag setter, and wide entity loops use private helpers.

Replace complete caller frames at the twelve enumerated physics entries,
navigation491210/493330 and named-query caller4cd350. Query-local frames such as
4aac80/4aaec0/4aaac0/4ab1f0 are owned by the new Query methods. Do not merely widen
a callee writing into an old32-byte local. CCollide+ d4 remains a borrowed pointer.
The original50-entry output collection in4cd350 is a separate domain.

Unchanged borrowed-trace ownership remains a recorded risk. The same allocation/
free sequence reuses the same50-slot pool at stock and raised capacities, but
denser content can increase events. The live trace test across49c65d/49c6f0 is
binding. Do not claim stock-invariant mechanics imply equal failure frequency.

Native save backing is claimed at the **allocation CALL487344**, not by replacing
the whole487320 routine: that routine also stores CMap+3b0 and registers events
and variables. Keep those side effects and the descriptor-adjusted constructor.
After native destruction, CALL4876c0 releases the prepared backing's claim rather
than passing it to native type14 free. Constructor and unwind failures poison the claim as specified in section6.
World/Grid/Query teardown is registered once by their new getters; destroy all
dependents before releasing backing, or retain backing safely until process exit.

### Explicit dense-index versus full-token lookup

Use distinct `DenseIndex` and `WorldToken` types and distinct `FindDense` and
`FindToken` helpers. Query methods4aa5b0/4aa760 (vtable+18/+1c) pass dense bitmap
indices at4aa683/4aa833 into the native full-token calls4aa68a/4aa83a. Their
replacement contract is explicitly **LegacyEmptyDenseDisposition**: retain the
empty lookup result and all other native side effects. Do not silently repair
these methods to return a different geometry result, and do not leave an implicit
integer cast that could change behavior under another encoding. Minted contiguous
tokens start at1024 while dense indices end at1023. Dense257 is not token257;
token0x501 is generation1/slot257. The interface ledger marks both methods.

## 3. Token policy: contiguous encoding and bounded generations

Keep `token = (generation << 10) | index`, index 0..1023. The generation field
has 21 bits, but **mint only generations 1..0x3fff**. Zero is null. Validate the
index, live bit, whole stored token and operational generation bound. Never wrap,
reuse a retired slot, or reseed generations during an in-process reset.

Retail's eight-bit layout is exact for every slot through generation `0xffff`
(`g < 2^16`). Contiguous ten-bit tokens are exact for every slot through `0x3fff`
(`g < 2^14`); the largest minted token is `0x00ffffff`. At the next generation,
`float32(0x01000001) = 0x01000000`, which changes the slot. Split encoding is
rejected: generation-one slot257 already rounds `0x20000101` to `0x20000100`.
The split dense-query and `%4096` bank collisions are additional objections.

Not every script assignment is float storage. `getID` and `getZoneVar` return
`i`; the parser assigns integer type2 at `4d9d8e`, and `4da550` allocates integer
storage for type2, float storage for type1. Float-target assignment calls the
numeric getter at `4d4be9` and executes `FSTP float32` at `4d4bec`. Float literals,
float-target assignment, and `fadd/fsub/fmul/fdiv` at `4d91b0/4d9200/4d9250/4d92a0`
are the lossy routes. A subsequent integer conversion cannot recover lost bits.
Float-to-string at `4d87f0` uses `%f` with a 16-byte buffer, integer-to-string at
`4d8850` uses `%i`; minted positive tokens fit either decimal integer part without
exponent notation. Identity-preserving float round trips must pass; arithmetic
that intentionally changes a token is not a supported identity operation.

The supplied 1,538-script census has 39 direct getID assignments. Their API return
type is integer, and no tracked direct getID local enters an f-operator. This is
a scoped source census, not proof about arbitrary user scripts. Enforce the float
bound regardless. `round6/token-analysis.json` contains the boundary examples,
closed-form argument and conditional rate estimates.

Retirement is part of the World owner, not an isolated increment patch:

1. Normal destruction retains native entity/name/property/grid/arena cleanup
   ordering. Set a destroying bit so reentrant release cannot destroy twice.
   Clear the pointer/live bit and decrement live count once. Advance generation,
   or retire at `0x3fff`, **before** publishing a reusable free-ring entry.
   Enqueue only nonretired slots and only outside reset. The original sequence
   `4c753a -> 4c7590` enqueues before `4c7542 -> 4c7550`; do not copy that unsafe
   intermediate state into the replacement.
2. Reset enters a quiescent resetting state. Allocation during reset is refused,
   counted (section 0, P3.3) and latches reset/load failure; it must not allocate
   from a half-rebuilt ring.
   Destroy occupied entities in native order without enqueuing. Preserve native
   advancement semantics: occupied slots advance through destruction, then every
   nonretired slot advances once more. Retail rebuilds at `4c77a9` before the
   all-slot increment at `4c77b3`; instead decide retirement first, then rebuild
   an ascending FIFO containing every reusable free slot exactly once.
3. At observable boundaries, live/free/retired sets are disjoint and their counts
   sum to1024. Allocation uses `free_count`, not merely `live_count < capacity`.
   Preserve retired slots across zone changes and new-game resets; process startup
   alone begins generation1. Keep a 272-byte sidecar: retired bitmap128,
   destroying bitmap128, and four count/state words. Never serialize its process
   history as a proposed durability fix.

This policy prevents both float aliasing and earlier stale-token wrap. It can
eventually reduce available slots; log retirement and maximum generation. With a
stable FIFO of225 reusable slots (799 normal arena entities pinned), 100 releases/s
takes about10.24 hours to reach the last generation; 1,000/s takes about1.024 hours.
A deliberately hot single slot can exhaust in roughly164 seconds at100/s. Resets
advance generations too. These are assumptions, not measured session rates.
Long/stress sessions can exceed the exact range; the engine must retire safely,
and the mixed-session gate must measure the actual rate and remaining headroom.

## 4. Old saves: refuted equivalence and narrow migration

**The proposed blanket retail-equivalent nondurability claim is false.** For an
old retail token `t = 256*g + s`, and fully occupied tables at uniform generation
`k > 0`, retail accepts exactly `g = k`; unconverted contiguous accepts exactly
`g in {4k,4k+1,4k+2,4k+3}`, at slot `256*(g mod4)+s`. These sets are disjoint.

At cold generation1, retail `0x101` can resolve to slot1 but becomes null under
contiguous decoding. Retail `0x401` is null but becomes contiguous generation1,
slot1; `0x501` newly resolves to slot257. Across positive retail minted values
there are 256 retail-only potentially live values, 1,024 contiguous-only values,
zero both-live values, and 2,147,482,112 both-null values. Restricting to the
16,776,960 all-slot float-exact retail values leaves the same two exceptional
counts and 16,775,680 both-null values. These are set cardinalities, not estimated
failure probabilities. With partial occupancy, the first counts are the numbers
of occupied retail and contiguous slots. Actual cold reconstruction can reset or
recycle slots: use `live[s] && stored_token[s] == t` in each table, not a claim
that every finished cold load is generation1.

Retail nondurability still exists: constructor `4c6440` seeds generation1 and
SVAR stores raw integers without a saved slot-generation binding. Raising capacity
changes allocation order too. Therefore Phase A promises no durable identity,
but must avoid the additional **radix-induced** aliases just demonstrated.

Remove every Round5 VM provenance mechanism: all16 script hook sites, origin
side table, SVAR origin bitmap, `4d8c3f` argument proxies, 19-operator rules,
implicit-context/string-view origin tracking, and the native origin trailer.
Review item5 and its 1,556-value temporary-pool lifetime problem are consequently
moot. No runtime scalar carries an origin tag in this design.

Use a narrow LC-boundary correction instead:

- Allocate existing header `required_features` bit0 as
  `ENTITY_LAYOUT_CONTIGUOUS10`. No header-size, major-version or SVAR section
  change. Feature0 means legacy retail layout; bit0 means Phase A current layout.
  Reject unknown required bits. Old readers already reject nonzero required bits.
  Update **both encode and decode** in Python/C++; current writers hard-code zero.
  Preserve the marker through keep/carry-over, rewrite, parse/build and CRC work.
- At LC-to-native conversion, only fields in the verified typed entity-ID
  registry are translated: `(g,s)=(value>>8,value&255)` to `(g<<10)|s`, or null
  when `g==0`, `g>0x3fff`, or the original value is not a valid positive retail
  token. The last cutoff is deliberately stricter than the 21-bit field: runtime
  cannot mint larger generations. Log a typed-field normalization-to-null count.
  Preserve null. Feature1 values already use ten-bit layout; validate known typed
  fields against the operational domain but never shift them again.
- `round6/typed-svar-fields.json` identifies 12 known zone-variable keys, their
  direct getID/zero writers, all scanned file hashes and the script fingerprint.
  This closes the supplied catwalk/force-field/rescue raw-ID stores without
  rewriting counters. The adapter matches scope plus exact registered key; it
  does not treat arbitrary integers or similar-looking names as tokens. Preserve
  unrelated SVAR integers, `GAME.value_608`, item-world slots, ordinal references,
  hero keys and opaque retained data unchanged. Earlier analysis found no World
  token consumer for `GAME.value_608`; that exclusion is not a guessed token type.
- Verify the registry against the builder's owned script inputs before enabling
  legacy migration. A changed script schema must supply a verified registry;
  otherwise clearly refuse legacy load. Graphics-only changes do not alter this
  fingerprint. Direct-literal source scanning is evidence for the supplied
  corpus, not a universal mod-language data-flow analyser. Unknown/dynamic token
  stores cannot safely be inferred from scalar bits, and must not silently pass
  under a universal compatibility claim.
- Normalize every known typed field before native application, then all newly
  captured fields are current layout. Phase A saves always emit feature1. Old
  offline converters keep emitting feature0: existing82-save conversion bytes
  must remain unchanged. Retail native imports use an explicitly selected retail
  descriptor, producing feature0 LC. Current native campaign transfer is selected
  by matching GCAP and E10A2; it needs no origin trailer. Do not guess a native
  descriptor from payload size or a token's numeric value.

For representable generations, typed migration preserves `(generation,index)`
and hence the validation result for corresponding logical tables; it does not
ensure two independently reconstructed worlds place the same entity in that slot.
Generations above `0x3fff` become null even if a very long retail process could
have matched them. This is a disclosed safe-null boundary, not unconditional
retail outcome equivalence. Existing float-corrupted saved IDs cannot be repaired.
The later durable-reference fix can replace this registry's value adapter with
name-based IDs and post-reconstruction resolution. Do not introduce that fix now.

## 5. GCAP agreement, admission and receive retirement

Use the one shared `gameplay_agreement` / `gameplay_capabilities` mechanism from
first-hit commit `c312538b2ee92f6b3b6e369e95fd0be022d62cad`, with the pinned read-only
snapshots in `round5/shared-framework.json`. Do not use its earlier boolean EFH
wire or add a capacity-only handshake. Advance GCAP protocol revision to2 for
the receive-lifecycle contract; layout rule1 revision3 is `{32,768,1024,10}` and
defines contiguous encoding, max generation0x3fff and E10A2. Verified capacity-off
uses layout rule1 revision1 `{32,224,256,8}` and the retail native descriptor;
it still uses GCAP protocol2 and the receive barrier. First-hit rule2
revision1 still records its verified effective boolean. Unknown, missing,
duplicate, late, unequal or unverified rules refuse. Even two failed installs
must not agree. Change the checked actor+generic condition from `==world` to
`<=world`, retaining uint64 overflow checks and all other profile bounds.

The canonical record remains 12-byte header plus sorted20-byte entries, up to8.
Two entries occupy52 bytes; maximum record172 produces a largest admission body
of288, below the native301-byte fragmentation threshold. Append to the unchanged
native bodies18/1A/1B/1F (116/48/8/80 bytes).

| Message | Exact send hooks | Compare before native handler |
| --- | --- | --- |
|18 request |607d9d |6096a3 registration -> wrapper ->6090d0 |
|1A information |6077c1 |609cc3 -> wrapper ->609970 |
|1B follow-up |609a5d |609dec -> wrapper ->607810 |
|1F reply |608477 broadcast and6085f5 single |609ee2 -> wrapper ->6079b0 |

Retain corrected endpoint roles:18/1A accepting host;1B/1F/1D joiner. Mismatch
sends native1D and an actionable incompatibility dialog. The host preserves other
participants; the joiner runs `609ad0/6097f0` cleanup independent of timer state.
Run cleanup at the quiescent barrier described below, not inside held iterators.

### No earlier application dispatch

Replace **60dbe0** before its callback loop, preserving native flags and callback
order. Direct parsing `60ddcf`, reliable delivery `60ffd7`, and fragment completion
`61027d` all reach this floor, including computed callback `60dc3a`. Require a
current matching certificate before any nonbootstrap callback, including native
campaign47/48 at608630 and gameplay input at6117b0. Check again after **each**
callback; if retirement or epoch change occurred, stop immediately with native
result2. Never continue through a cached next callback after deferred cleanup.

Before agreement allow only the earlier ledger's transport/discovery/admission/
refusal bootstrap opcodes `0a,0f,10,11,14,16,17,18,19,1a,1b,1c,1d,1f,6e`.
Opcode00 requires a bounded bootstrap inner opcode. Fragment12/13 is not a
bootstrap escape. Registration flags alone never authorize a message. Sensitive
outbound gates remain60f620 (enqueue),60d9c0 (direct),60f940 (queued drain), with
the source Node epoch attached to owned records. A received exact match and a
current-epoch local advertisement are both required before sensitive exchange. Host receive18 may record Matched before its native
admission callback; it becomes Ready only when the reply send adapter has recorded
the current local advertisement. The narrow bootstrap callback scope may progress
negotiation while Matched, but cannot dispatch or enqueue gameplay.

### Concrete retirement and reentrancy algorithm

Node `+19` is only a connection byte. `6106f0` does **not** clear receive data.
The Node is30c0 bytes; receive bytes begin+52e, cursor+1e98, bound1964. Reliable
RX is the list+3090/count309c with expected sequence+308c; TX is+30a0/count30ac;
fragment assemblies are+30b0/count30bc. These bytes cannot become current merely
because a new certificate was written at the same address.

1. Use a prepared six-Node sidecar, monotonic u64 local lifetime epochs, state
   Empty/Negotiating/Ready/Retiring, raw-buffer birth epoch, and a768-slot native
   header metadata table. Overflow of an epoch/allocation serial refuses online;
   never wrap. Whole receive/list scopes encompass616340,60dca0,610680,60ff00 and
   6101b0, with RAII/SEH-finally depth release. The610680 **outer** list iterator
   is included; protecting only its60ff00 child leaves the next Node dangling.
2. Disconnect6106f0(false), begin-join609b80, destruction610a50 or a new admission
   cycle immediately invalidates the certificate, increments epoch, closes
   application traffic and marks Retiring. Repeated retirement is idempotent.
   No matching GCAP can promote a Retiring Node. Raw append60dc60 drops incoming
   bytes while retiring; for an empty current buffer it stamps the birth epoch.
   Certificate comparison never retags old raw/reliable/fragment records.
3. At60eac0, only for a verified Node+3090/+30a0 list, stamp the successful header
   insertion with owner/epoch/allocation serial. Header poola3e588 has768 entries
   of20 bytes; clear metadata at60e300 before reuse. Preserve other list callers.
   The reliable pump verifies that metadata before copying/dispatching. On
   retirement it leaves the held record for the eventual purge, matching native
   result2's exit-before-free branch at60ffdc. Never free a record twice.
4. Fragment6101b0 must check assembly existence and its **original** epoch before
   any continuation copy. The audited opcode12 path only obtains the static pool;
   no assembly creator exists in that path, while13 can dereference a missing
   assembly. Do not manufacture current provenance for an old/untagged pointer.
   Refuse that continuation cleanly. GCAP, enumerated input/hero packets, and
   200-byte native-save chunks do not require new fragmentation. This does not
   claim to repair an otherwise missing native fragment protocol. Native gates
   must exercise a synthetic pre-existing assembly and detect any actual creator
   before permitting such an assembly in a live implementation. After dispatch at
   61027d, the replacement checks the captured epoch again: on retirement it
   returns2 without native611e90 removal or any further assembly access. Retail
   removes unconditionally after this call; merely wrapping that call is unsafe.
5. **Defer complete topology operations**, including unlink/pool reuse, until all
   receive/callback/list frames unwind:60f010 removal,60f0a0 pruning,60f070 reset,
   60ece0 module cleanup,616600 socket-close plus cleanup,6097f0 session cleanup,
   and609b80 join. Do not defer only610a50 while its caller frees the pool slot or
   list node. Capture bounded command arguments in prepared storage; copy the
   endpoint value, never a caller-stack pointer. Deduplicate per-Node commands,
   with session teardown superseding lesser commands. Overflow closes online and
   retains unsafe-to-release backing until orderly exit. Unexpected low-level
   destruction while a held frame remains is a fail-stop invariant violation;
   do not return into a scalar-deleting destructor that would free the object.
6. At depth zero, before any fresh certificate or owned deferred join, purge a
   retained Node by invoking the real **610a50** destructor (not610c80), clearing
   its30c0 bytes, then610950(original flags,false). Restore only immutable NodeID,
   address/port and fresh module clocks307c/3088. Keep the containing module's
   pool/list membership if retaining the Node. Ctor restores sequences3070,
   3072,3074,3076,308c to1 and empty cursors/lists. Do not copy old queue state.
   Run this via original trampolines under an internal-purge scope so hooks do
   not recursively retire it. A deferred real removal instead uses native pool
   removal once and leaves the sidecar epoch tombstone for address reuse.
7. Native610a50 drains reliable RX/TX payloads using the native small/large pool
   frees, releases20-byte headers through60e300, and removes list nodes with
   611e90. Its fragment path removes list nodes; it does not own an independently
   proven assembly-body heap free. Follow that ownership exactly; no invented
   HeapFree. If native fixtures find separately owned assembly backing, add its
   proven owner-release before the fragment gate can pass. Clear all sidecar
   record/assembly references and raw bytes after destruction. Assert zero list
   counts, reset cursor, fresh sequences and no old metadata before Empty.
8. Only after purge may a new native join advertise and accept GCAP. A begin-join
   during pumping returns native failure0 for that invocation and schedules one
   owned fresh join at quiescence; caller behavior must pass native fixtures.
   Section 0 (P3.1) proves 604e30 is such a receive-scope caller and that every
   caller discards the result; a failed owned join runs the joiner failure path.
   Incoming bootstrap while Retiring is dropped and must be retried after purge.
   An unexpected fresh join request to a still-Ready Node retires/refuses that
   attempt; it cannot immediately replace the certificate over buffered traffic.
   Duplicates within the existing negotiation are idempotent. The new peer begins
   native sequences at1; test both join directions and both admission families.

Logical retirement is immediate; physical destruction is quiescent. A current
certificate is never sufficient to bless an older receive record or held frame.
The abstract model covers12 source/depth cases and100 same-address epoch reuses.
It is not native protocol proof. Local epochs cover engine-resident buffers and
frames; the original UDP wire has no authenticated session nonce. This plan does
not claim protection against arbitrary delayed/replayed external datagrams.

## 6. Native save adapters, claim failure and DLL integration

NativeLayout **E10A2** retains the Round5 expanded descriptor:4096 states including
sentinel,1024 ordinals/dynamic/smart entries, five19b88 records, manager80c24.
Payload200000 and wrapper200078 now use **usable_end200000**; the origin trailer
is gone. The existing strict stream-end comparison remains strict. Five records
plus the enforced1MiB non-zone budget fit; overflow blocks saving, never truncates.
Keep retail OLD_* import/export and LGCY constants unchanged. Python/C++ codecs
and native stream adapters take explicit immutable descriptors, including bound
checks and failures before every copy/allocation.47/48 transfers use <=200-byte
chunks, checked total/index/order and a short final chunk. A native save format
is not inferred from the file or packet's size.

Claim the prepared manager at allocation CALL487344, preserving the complete
CMap initialization around487320, its+3b0 publication and event/cvar registrations.
Normal release CALL4876c0 returns the claim after native destruction; it must
never native-heap-free VirtualAlloc backing. Add two explicit call adapters:

- **487362 -> ConstructPreparedSaveManager**: ECX is the claimed backing, no
  stack arguments, EAX returns the constructed object. Use a Win32 unwind-safe
  wrapper: Prepared -> Constructing -> InUse only after native487440 returns
  exactly that pointer. Exception, null or wrong return sets terminal Poisoned,
  latches startup/load/save/online failure and preserves native unwind. Do not
  continue to487373 publishing an invalid pointer: the wrapper fail-stops (section
  0, P3.2) instead of returning a null or foreign pointer to 487367.
- **673cb8 -> PoisonPreparedSaveClaim**: cdecl two arguments, caller cleans8;
  compare the allocation argument with the active claim and poison idempotently.
  Original target48ef90 is a bare RET. It performs no recovery. Preserve that
  no-free disposition; do not call a destructor on a partially constructed owner.

A poisoned claim retains its backing until process exit, cannot be allocated
again, and refuses startup or the active load. Do not return null from the
allocator and let unchecked native initialization continue. Constructor failure
after publication is a terminal session/startup failure, **not** permission to
shrink a populated world. In native fixtures force failure before/during ctor,
wrong return, unwind, repeated requests and normal destroy/reclaim.

Composed save hooks retain reconstruction failure epochs. At465d05, a required
dynamic factory null latches failure before native skip465d43; allocator or World
registration refusal does likewise during reconstruction. Do not confirm a load,
clear protection during nested reset, or overwrite originals after failure.
Ordinary transient projectile failure is logged without discarding save history.

DLL responsibilities remain:

- `entity_capacity.*`: immutable descriptor, prepared owners, private wide
  helpers, generation sidecar, guarded transaction and instrumentation.
- `save_format.*`, save core/format rules, both lcsave implementations: expanded
  descriptors, LC required-feature marker, typed registry conversion, claim
  poisoning and reconstruction latches. No VM-origin source changes.
- `gameplay_agreement.*`, capabilities and game_version: single shared record,
  checked `<=` headroom condition, receive lifecycle and transaction contributions.
- Ladder: replace `&255` buckets with1024 decoded dense-slot buckets; preserve
  full-token equality. Actors still32 can occupy world slots1 and257.
- Break-rule: retain `%4096` plus full-token comparison under contiguous tokens.
  Distinct live dense slots are injective modulo4096; same-slot old generations
  still require full-token checks. No split-bank collision exists in this plan.
- Test state and diagnostic observer: immutable installed descriptor and1024
  scan; new World logs refusals directly. Remove the independently installed old
  observer that would read moved fields. Cross-check native count and observer.
- Trigger timers: integrate the separate actor-address keyed1024-entry table,
  teardown retirement and pointer reuse contract; do not resize actor count or
  fold in the independent parser limit. The integration branch is not in this
  clone and remains an explicit dependency.
- Builder: own `[Limits] EntityCapacity=1` for port builds, require LC, generate
  the verified typed registry. Unconfigured retail DLL defaults off. Preserve
  normal INI ownership; the release integrator controls CONTENT_VERSION.

## 7. One protected transaction and rollback

Refactor the upstream shared agreement API first: its present `prepare` already
writes three admission/refusal sites and `finalize` writes nine transport sites.
Calling those functions before the capacity guard pass violates this transaction.

`prepare_refusal` and `prepare_transport(candidate_record, prepared_epoch_state)`
must return **contributions**: expected bytes, unresolved writes, prepared
storage/vtables/stubs and publication callbacks. They do not call VirtualProtect,
write executable bytes, expose callbacks, set failure_ready/transport_ready or
freeze an effective record. Rule registration is provisional. A pure binder
resolves symbols and composes every write; a single transaction owns the pages.
`commit_verified(receipt, effective_record)` only publishes already-verified
state and freezes the canonical record; it emits no further patch writes. Late
registration and reentrant installation refuse and poison the agreement.

The first-hit branch patch may remain its separately guarded feature because it
does not overlap capacity sites; import its **verified effective** snapshot, not
the requested INI value. Its shared agreement sites belong to this transaction.
The socket IAT exclusion at67f290 is a separately verified safety gate whose old
runtime pointer must equal GetProcAddress(WS2_32, ordinal23), not the on-disk thunk.
If that exclusion cannot be established, refuse startup before network use.

Prepare every owner, native adapter, sidecar, helper, new vtable, executable stub,
cache and observer before publication. Require empty stock owners and startup
quiescence. Union expected bytes once; overlapping expected guards must agree.
Reject any unexpected byte, conflicting write, unknown executable or unresolved
symbol. Bind exact literals/relocations from the ledger into an immutable write
set. A page ledger acquires each distinct **touched** page once, including writes
crossing page boundaries. Remember original protection per page.

Apply the whole set, verify readback, flush instruction caches and restore exact
protections before publishing owner pointers, enabled state and the effective
record. Any prior failure restores all changed bytes, verifies rollback and
retains possibly referenced storage. Stock limits remain. Requested-but-failed
capacity installation poisons online, even if physically stock. Use only verified
stock LC adapters after fallback; if disk-save exclusion is unverified, refuse
startup. If rollback itself cannot be certified, refuse startup rather than run
a mixed layout. Tests inject failures at every mutation/publication stage.

After publication, never hot-disable or shrink. A changed INI applies next process.
Rollback means exit and restart with the prior DLL/verified stock profile, retaining
original saves/backups. Capacity-off builds refuse feature1 LC files and expanded
native payloads; old readers also refuse required feature1. Never strip the marker
or truncate expanded state to make a downgrade appear to work. Restore a matching
build to access those saves. Durable reference behavior remains separately queued.

## 8. Ordered implementation and acceptance gates

Each step is a separate reviewable change in the later implementation job. No
later gate is satisfied by this round's byte checks or abstract models.

| Order | Work | Gate before proceeding |
| --- | --- | --- |
|0 | Second-model review (done: GPT Sol, Claude) and the step-0 gate of section 0 on the release base | Review dispositions accepted; step-0 gate passes (guards, heads, literal screen, registry, corpus) |
|1 | Pure shared-agreement contributions, immutable descriptor, binder and page transaction in isolation | prepare writes nothing; each guard mismatch and injected protection/readback/flush failure yields no partial publication; rollback failure refuses startup |
|2 | E10A2 descriptors, known-field LC adapter/feature1, save claim states | Python/C++ parity, malformed/unknown-feature/downgrade refusal,82/82 old conversions byte-identical, wrong/throwing ctor never reuses backing or confirms load |
|3 | Actor32/generic768 allocator and World1024 with generation retirement | Full/recycle,255/256/257/1023, reset double advancement, maxgen/retired counts, no duplicate release or reset allocation, native lifecycle/ABI fixtures |
|4 | Wide Grid/Query/Order/Collisions/caller frames, caches/timers/observer | Dense cells33..1024, cached/uncached and navigation parity, stack canaries, x87/thiscall callbacks, explicit dense-policy tests, timer and full-token reuse cases |
|5 | GCAP revision2/layout3, receive retirement and protected Node purge | Both admission families and endpoint roles; every old receive source, nested callback retirement, node reuse, sequence reset, fragment ownership, deferred join failure0 behavior; no dispatch before matching effective record |
|6 | Compose all modules and builder configuration into one startup transaction | Final linked-byte binding/readback passes; guard mismatch leaves stock; no standalone conflicting installer; failed peer cannot join; no populated-world downgrade |
|7 | All existing game-free gates plus new integration suites | Units, native suites, parity vectors,82/82 corpus, old/LC fuzz, content guard and whitespace; all results recorded against built DLL hash |
|8 | Authorized own-build live proof | All acceptance rows below pass; report staged setup distinctly and record DLL SHA-1/commits |

| Test family | Required cases and result |
| --- | --- |
| Domains | Dense lookup257 finds entity257; token lookup257 is null; token0x501 finds gen1/slot257; both legacy query methods explicitly retain empty results; rejected split257==token0x101 regression case |
| Float | Retailgffff and contiguousg3fff across all slots exact; next-generation odd slots alias; splitg1/high banks alias; actual integer/float assignment, asString/asInt and fadd(x,0)/fmul(x,1) round trips |
| Stale saves | Legacy101,401,501 and g1..8, boundary generations; same logical tables before/after typed normalization; counters unchanged; explicit safe-null cutoff; feature1 idempotence; unknown schema/required bit refusal; save twice after legacy load |
| World | Generic767/768/full, actor31/32/full unchanged, World1023/full; retirement on release and reset; live+free+retired invariant; ring ordering; duplicate/reentrant release; real generation-rate logging |
| Queries | Cell32/33/1024, ordered insertion/removal, filters/geometry, cached/uncached invalidation, sorting score/ST0, navigation and all complete widened frames; unrelated shared-helper users unchanged |
| Native persistence | Five full cache records, high ordinals/states, chunk boundaries, required reconstruction failure before confirmation, malformed/missing/reordered transfer, poison claim and inability to overwrite originals |
| Receive retirement | Old raw buffer, reliable records, partial fragments and held frames across rejoin; retire inside callback; matching certificate while old frame exists must stay blocked; purge once at depth zero; no stale free touches new records; opposite join directions and repeated same-address reuse |
| Agreement | Matching enabled/disabled profiles; mixed capacities/layout revision/first-hit; either install failure; malformed/unknown/late rules; direct/queued/reliable/fragment floors; clear mismatch refusal before gameplay/native-save dispatch |
| HAARP | Label staged first-gate entry; leader creates freeze-gun projectiles which reach and hit targets; zero refusal log entries. Teleport or spawn alone is not proof |
| Magneto | Arena props appear and interact; compare placement expectations and diagnostics |
| Census | Ten worst existing census zones with comparable routes/workload, including late spawns: zero allocation refusals; actors stay within32 |
| Cold load | Three dense zones: save -> exit process -> cold load; high-index entity state, normalized legacy fields and declared nondurable script-reference behavior recorded |
| Mixed session |30 minutes, first-chance exception capture, no relevant exceptions; borrowed50-trace identity before49c65d/after49c6f0; max generation and retired/free counts logged |
| Two instances | Matching peers exchange campaign and gameplay; mismatch/failed-install peers refuse first; cancellation/rejoin with buffered traffic; client save then solo cold load |
| Pre-retired peer | One peer with retired slots before the join (P3.4); matching peers still agree at 614a40 |
| Typed keys live | Load of a 0.2.0i save with populated typed keys; conversion log counts; script behaviour (P3.5) |
| Token codec | 0x00ffffff asString->asInt; float identity; test-state scan on the installed layout (P3.5) |

Only own processes/builds/save copies; staged steps labelled; no real desktop
automation. Phase B (actor96, CAI, CAITeam4x8 and actor imm8 bounds) remains planned
in `PHASE-B-BACKLOG.md`. None of the native/live acceptance was run this round.

## 9. Offline verification and review disposition

Run `python -B scripts/verify_audit.py --read-only` from the private workspace.
Default is also read-only: no result files, bytecode, SQLite journal or Git-index
refresh. `--write-results` explicitly emits verification.json and the synthetic
binding example. Bound example addresses are fake audit values, never an installer.
The verifier checks the requested PE, every active guard/region, inherited closure
records, all787,102 exported instructions, relocation lengths/non-overlap, each
individual guard corruption, abstract models, script fingerprint and unchanged
82-save baseline hashes. Models are re-executed; they are not claimed as native
or new-codec parity tests. A separate before/after filesystem audit proves no
content/mtime/path changes during the read-only invocation.

Review items1/3/4/6/7 are addressed by explicit plan contracts and future native
gates; item2's split alternative is rejected and its underlying stale-source
ambiguity is constrained by typed LC migration; item5 is moot; item8 is fixed in
the audit tooling/status. Shared agreement contribution refactoring is mandatory.
This is a **revised implementation plan for second-model review**, not a claim
of complete engine-wide reader certification or a working capacity fix.

## 10. Implementation deviations

Appended during implementation; each entry names the plan section, the evidence
and why the change stays within the reviewed design.

1. **Added site 0x44ea40 (CCollide vtable slot 3), section 2.** The twelve enumerated physics
   entries do not include slot 3, which forwards a caller's entity set into 0x44d940; that
   helper's two failure paths (0x44d9d4, 0x44db8c) clear only eight dwords of the set. Query
   slot 0x7c (0x4aa2e0) reaches it with the Query bitmap. Found independently by the query
   and frames ports. Disposition: the frame is replaced whole like the twelve
   (`entity_frames::Physics_0044ea40`, E9 + 3 NOPs over `8b44240c8b542404`), its body
   0x44ea40..0x44eaf6 is hashed as an extra region, and the entry bytes are an extra guard
   (`entity_capacity_rules.hpp`). Same shape as the reviewed caller-frame replacements.
2. **Grid slot 8 body guard, section 2.** 0x614280 (`lea eax,[ecx+0x1c]; ret`) lies outside
   the grid region hash; added as an extra 4-byte guard.
3. **Late agreement enlistment, section 7.** A participating installer with neither LC nor a
   first-hit key (a controller mod registering a rule after the startup transaction) cannot
   join the already-committed transaction; `gameplay_agreement::prepare()` then runs the
   refusal+transport sites in a transaction of their own (same class, same guards, same
   all-or-nothing). With LC or capacity the sites are always in the startup transaction.
4. **Startup fault test, section 7.** The release test expected changed guard sites to remain
   after a flush/restore failure (no rollback); the transaction rolls back and verifies, so
   the expectation is now "no changed site in any fault case" (69 cases pass).
5. **Native save layout profile in save_format, section 6.** The stream-bound stubs, capture
   bound bridges, ordinal bound and record offsets follow a `native_profile` (retail or
   E10A2) chosen before the seam is contributed; the symbol names
   `entity_capacity::StreamBound_*` / `SaveCore_*` of the ledger are the seam's own bridges
   (same sites, same E9 shape), emitted by save_format's contribution.
6. **Typed registry provenance, section 4.** The builder generates the registry from the
   shipped `Scripts` tree (the last module), and the DLL verifies it against the installed
   tree; a mod shipping scripts leaves it unverified (legacy loads refuse with the reason).
   The step-0 gate separately checked the prepared tree against the plan's fingerprint.
7. **Query owner, section 2** (from its differential-tested port): order slot 8 null-tests
   the Query before reading its trace count (native reads first and faults); acquiring a
   fourth busy query fails fast instead of the native null-vtable fault; bool arguments to
   the collide slots are passed as clean 0/1; three small helpers are inlined. 0x4a9a80 is
   owner-aware (wide map for DLL queries, retail +0x38 layout otherwise).
8. **Frames owner, section 2**: two Ghidra decompiles were wrong (slot 0x38 of CActor takes no
   stack arguments; model slot 0x14 takes ten) and were ported from the listings; 0x44e650 is
   ported privately so 0x44d360 never re-enters native code; 0x44d0d0 is neither patched nor
   called; 0x685658 is the inflation constant 1.0f.
9. **Grid owner, section 2**: the native cell-overflow defect of 0x465400 (the 32nd entry
   overwrites the next cell's count byte at 256) is reproduced at 256 and impossible at 1024;
   the x87 control word is read per call so the float chains match retail under either
   precision; nothing reaches the World from the grid (the brief's assumption was wrong).
10. **lcsave, section 4**: profiles (kind 2) carry no marker (no entity data); feature-1
    idempotence on real saves is semantic (legacy placement promotions re-index zone states
    on the first load, as the existing round-trip gate already treats it); the second load
    is byte-exact between Python and C++.
11. **save_startup fault DLL**: the fault shim now wraps the transaction's Windows calls
    (`patch_transaction.cpp` under `XML2_SAVE_STARTUP_FAULT_TEST`).
12. **Bridged natives keep their EAX pass-through (query owner, section 2).** The native
    0x4a9730 (trace policy constructor) ends `mov eax, ecx` and ten callers (0x441950,
    0x441c00, 0x432130, 0x450130, 0x451cc0, 0x497760, 0x4dd980, 0x4e4540, 0x4e4730) push that
    EAX straight into 0x4ab770 as the policy; the port declared it `void`, so the camera's
    order fill dispatched through the *query's* vtable (slot 0 = reset-by-name), re-filled the
    camera query with a class set, dropped its trace record and drove the camera to NaN on
    the first HAARP ramp (black screen, streaming at the origin). `QueryApi_004a9730` now
    returns the policy and `QueryApi_004ab7d0` its argument (native `mov eax, [esp+4]`, no
    caller consumes it). A capstone sweep of every E9-bridged native against the port's
    declarations found no other dropped return (tools: PROGRESS.md 2026-10-11 16:05).
