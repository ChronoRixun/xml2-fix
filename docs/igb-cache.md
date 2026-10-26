# IGB info cache (`[Limits] IGBCache`)

XMen2.exe keeps every IGB it has precached - a package's `model` entries (the zone's own map IGB last), motion paths,
effect models, HUD heads and on-demand model loads - as one record of a single `CIGBInfoCache2`: a static object at
`0x7b7fb0` with **200 records**. When 200 are live, `precache` (`0x56ed00`) returns NULL at `0x56ede5` without a word.
If that happens to a zone's map IGB, "Loading Zone igb..." fails and CMap reloads the previous zone: the player is
bounced back with no message. A model past the cap is simply missing.

    [Limits]
    IGBCache = 1024        ; 201..2048 (200 is the game's own)
    ResourceNames = 2048   ; every IGB record also takes a resource name

Missing, `200` or a value out of range keeps the game's own cache (a value out of range is logged). Every IGB record
registers a name in the global resource name table, so the name table grows with it: without `ResourceNames` it gets
**2048** (or 450 + one per actor slot over 40 + one per IGB record over 200, if that is more), and a `ResourceNames`
too small for both tables is raised to what they need (logged). The cache is only raised when the name table was.

The IGB records' model nodes take IModel handles: about 3.5 per record, so 1024 records fill about 88 % of
`[Engine] ModelPools = x4`'s 4096 handles. Raise them with it: `[Engine] ModelHandles = 8192`
([model-pools.md](model-pools.md)).

## What the install does

Target: retail XMen2.exe, image base `0x400000`, SHA-1 `7d95cdb4a9a599b982147a3389ea7aff211cb82b`. Research:
xml1-port `research/limits/igb-cache.md` (sections 1-7).

`CIGBInfoCache2` is the same pool template as the actor table (`CAnimMotionCache`, `[Limits] ActorSlots`) with
0x90-byte records: vtable, then the pool S at +4 (records, bitmap A, a ring of N+1 free indices with its write/read
positions and count, bitmap B, the live count, N ids `(generation << shift) | index`, mask, shift). The DLL moves the
object into a zero-filled block of its own (78 KB at 512 records, 152 KB at 1024) and lets the game's own code build,
use and tear it down there:

| Part | Sites |
| --- | --- |
| Pool fields | 82 operands in the cache's 16 functions and `ring<200>::pop` (`0x491180`, its only caller `0x56f6b9`): 11 x N, 3 x N-1, 68 displacements (the cache methods reach the live count and the mask `this`-relative, the pool's methods S-relative) |
| References | the getter's `mov ecx, S` (`0x56fa07`), vtable store (`0x56fa12`) and returned object (`0x56fa2e`), the atexit thunk's object (`0x67e190`) |
| findNext | `bitset<200>::findNext` (`0x490d70`) is shared with two unrelated 200-bit sets (`0x4916b9`, `0x57d8d0`): the cache's three calls (`0x56e8c3`, `0x56eb58`, `0x56ebc6`) go to a copy with N in its five places; the original keeps 200 |
| Guards | the whole getter, the atexit thunk, the ring pop's and the pool destructor's only calls, the findNext source |

Not patched, by design: the constructor's two unrolled seven-dword bitmap clears (they run once, on the DLL's
zero-filled block) and the pool destructor's seven-dword tail clear of bitmap A (`0x56e8d2`; it runs once, at exit,
and nothing walks the bitmap after it). The init guard `0x7bf6c8` stays where it is (outside the object), so the
getter's once-only construction and atexit registration work unchanged. Record contents and the 0x90 stride are
independent of N.

All or nothing: every site's retail bytes, the references, the guards and the findNext source are compared before
anything is written; a mismatch, the object already built (guard bit set) or a failed allocation leaves the game's
own cache and logs one line. At 1024 records an id keeps 21 positive generation bits (mask `0x3ff`, shift 10) before the native reset at `0x56f89b`.

## Online

A full cache refuses a zone's map IGB and every model past it, so peers with different caches can simulate
different games: a raise is **gameplay-agreement rule 31 (IGBCache)**, revision 1, values
`{installed ? 1 : 0, IGB records, resource names, 0}` as installed (`{1,1024,2048,0}` for the Legends Classic
wave-1 ini). Rule 31 registers only when an IGB raise is requested. Independent named rules 32..37 also cover
`ResourceNames`, `ActorSlots`, `ItemEnhancements`, `FightStyles`, `EffectCurves` and `MoveTriggers` respectively.
Each has revision 1 and values `{effective capacity,0,0,0}` and registers only for a requested or effective
non-stock capacity. Thus `ResourceNames=450` versus `2048` refuses by name even with `IGBCache` absent or `200`.
Absent/stock limits add no entries, preserving the released record byte for byte. A requested raise that did not install registers unverified, which closes host/join. A
raised PC and a PC without the key refuse each other at admission naming `31 IGBCache` (`(not on the other PC)` /
`(disabled on this PC)`); older DLLs say "a rule this PC does not have".

## Telemetry

`status` reports `igb n/cap` from wherever the cache is (the live count at block + 4 + live; retail `0x7bf39c`). The
startup log says where the block is, its size and id mask, and how many fields were patched. The same log has the
address-space line of E12: `LAA on/off (XMen2.exe header); highest user address ...; highest reservable address ...`.

## Proof without the game

- `xml2_test --igb-cache-rules`: the ini rules, the layout (retail 200 field for field, 512 as igb-cache.md 7.1,
  1024), the 82 rows against the retail layout, and against the exe: every byte as the table says, the stock
  writes reproduce the retail image byte for byte, the 1024-record writes change only the listed operands, and the
  patched code runs on blocks of the test's own: the constructor builds 1024 free records, `takeFreeIndex` (through
  the patched ring pop) and `allocAt` hand out all 1024, `alloc` hands out 1024 on a second pool, `bumpGeneration`
  steps by `1 << 10`, cache release (`0x56f850`) frees slot 1023 and advances its generation, reuse returns
  that slot, and the real precache entry (`0x56ed00`) returns NULL at 1024 without changing the block. At 1023 it
  reaches the independent name-full check, proving the capacity branch boundary. The cache destructor
  (`0x56f980`, through `0x56e840`) visits all 1024 records exactly once. External string/name services and
  record cleanup are test stand-ins; allocation, release, capacity branching and destruction iteration remain
  native. Guard bytes past the relocated object remain intact. The clone scans the 1024-bit bitmap past record 200 while the retail findNext stops at 200.
- `python scripts/check_igb_cache_text.py XMen2.exe --self-test`: the whole `.text` - every reference to the object is a row,
  every 32-bit cache/name-table field operand anywhere in `.text` is a row or an exact-byte allowlist entry, no data pointer into the object or at a pool method, the class
  vtable stored only by the cache, the pool methods called only from the cache, `findNext`'s outside callers exactly
  the two that keep it.


### Whole-text allowlist and negative control

`scripts/igb_cache_text_allowlist.tsv` pins each accepted collision by instruction address, operand offset,
complete retail instruction bytes and reason. There are no range exemptions and no automatic baseline
acceptance. The field-value inventory in the checker is independent of the patch tables, so deleting a row
cannot erase the search value. Both cache and name-table rows are checked; stack displacements are included.
All moved field offsets require disp32. Fixed record-internal disp8 fields do not move; the existing explicit
unrolled-bitmap-store checks remain in place. Bitmap word-count patch immediates are imm32.

The large constants overlap other independently sized pools: `0x7080` is the effect-curve bitmap,
`0x73c0/0x73c4` belong to the actor-cache layout, and `0x1c20` also occurs in a separate 100-node pool
(`100 * 0x48`). Small constants (`200`, `199`, `15`, ring-relative offsets) also occur in unrelated
arithmetic, call arguments, stack frames and object fields. Each such instruction is pinned separately;
changing it or introducing another use fails. The original shared 200-bit scanner stays stock because the
IGB cache calls its clone. Five exact references to `0x7ac244` remain valid: the name table's singleton
pointer storage is stable while its allocation relocates. Missing or changed allowlist entries also fail.

`--self-test` changes only an in-memory image at `0x560e90` to
`call 0x56f9f0; mov eax,[eax+0x73ec]; ret` and requires the whole-text field scan specifically to reject
`0x560e95`. The retail EXE is never written. This is a decoded-instruction completeness gate, not a proof
against dynamically computed offsets or alternate instruction entry points; live save/co-op acceptance
still requires the documented game runs.
