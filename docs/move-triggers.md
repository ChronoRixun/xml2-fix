# Triggers per move

`[Limits] MoveTriggers=64` expands the retail move trigger capacity from 19. The key accepts 19..127;
missing/19 means stock. Invalid values are logged and ignored. Legends Classic emits 64 by default.
The source census found a maximum of 27 after faithful weapon conversion, leaving 37 spare slots.
A port requiring the raise must use this companion DLL and key together with its rebuilt styles.

## Native layout and guards

Target: retail XMen2.exe, image base `0x400000`, SHA-1 `7d95cdb4a9a599b982147a3389ea7aff211cb82b`.
The parser at `0x4f6aa7` compares `CFightMove +0x11c` against 19 and frees the next event.
The stock handle array is `+0xcc`; the count at `+0x11c` follows twenty allocated handle words.
The parser uses only nineteen. Simply raising the comparison would overwrite the count and following fields.

The move manager pointer is `0x789e1c`. Its fixed pool has 332 records of stride `0x148` (allocation,
free and teardown at `0x500df8`, `0x5003c8`, `0x4f62d2`; bitmap at pool `+0x1a960`).
The raise leaves all records, count offsets, masks, pool metadata and event ownership native.
Before patching, the DLL reserves a zero-filled side array for each slot: `332 * capacity * 4` bytes
(84,992 at 64). The sole constructor call at `0x500afd` still executes the native constructor,
then stores that slot's side-array pointer at `+0xcc`. Reuse clears that side array. No runtime
allocation is needed. The arrays live until process teardown, like the DLL's other relocated pools.

All ten direct array accesses are redirected, guarded by their complete retail instructions:

| Path | Addresses |
| --- | --- |
| Destruction / pop | `0x4f6420` |
| Indexed lookup | `0x4f64c6`, `0x4f64d6` |
| Handle / tag lookup | `0x4f6518`, `0x4f6562` |
| Move reset / pop | `0x4f6987` |
| Parser append | `0x4f6aff` |
| Inherited move copy | `0x4f7dab`, `0x4f7e35` |
| Trigger replacement / pop | `0x4f8046` |

Three address calculations become pointer loads. Seven indexed instructions use small DLL thunks
which preserve flags and scratch registers. Pop paths retain their original count-minus-one indexing.
The parser comparison, constructor call, native count initialization/reset call, pool getter/allocation
and pool stride/teardown are also guarded. An unexpected byte, existing move pool, failed allocation
or failed page protection leaves every game instruction untouched. All patch pages are made writable
before the first instruction is changed. An impossible constructor address outside the guarded native
pool logs the address and fails fast rather than allowing an unsafe pointer.

## Actor execution timers

Parsing all handles alone is insufficient: the actor update at `0x430a00` and initializer at `0x42c390`
also loop exactly nineteen times. At the old dispatch limit, a 27-trigger smash executes only its first
seven beam shots. Live tracing identified timer lookup at `0x430f90` and firing at `0x430b33`.

The native actor stores nineteen scheduled floats at `+0x640..+0x688`. Those fields stay at their original
addresses and are mirrored after initialization and each update. The DLL reserves 1024 actor-address
cache entries (the native actor arena reuses 32 addresses), each holding source and scheduled arrays
through the configured limit. The cache is preallocated and retired after the guarded actor-arena destructor (`0x4613e0`) returns. Exhaustion is logged and uses thread-local invocation storage with inactive extra entries, preserving native nineteen-event scheduling without terminating the game.

The original nineteen-float stack packet stays intact. At `0x430f9a`, the DLL captures the packet and
reads additional authored times through the move's existing virtual getter. The initializer's source
and destination address calculations use the side arrays; its two loops and the active/paused dispatch
loops use the raised count. The original code still decides animation-relative versus world-time modes,
scales times with its native x87 operations, delays paused world-time events, fires each event once and
marks it inactive. Cancellation clears all timers. All native metadata, including the first nineteen
scheduled fields, keeps its original layout and values.

Captured stack-packet ownership is scoped to the native move-start call at `0x430d40`, with all eight
stack arguments forwarded unchanged. Failed/reentrant starts cannot leave stale packet identities for
later animations. Every start restores its enclosing invocation's packet, even if that pending move has not become current; current-move identity is checked only when consuming a packet. Unrelated native packets carry their nineteen values with inactive extra entries.
Initializer/tick/start prologues and stack-cleanup instructions are guarded, together with the timer
address calculations, loop bounds and clear path. The nineteen-value packet producer (`0x430f86..0x430f99`), frame allocation and saved registers are guarded; its native loop is never raised. Handles and timers install in one transaction.

These are transient style/event data; no native save field, LC identity or save-code byte changes.
Global event-pool capacity is independent; this patch does not claim unlimited loaded events.

## Verification

`xml2_test --move-trigger-rules` checks key bounds and pool slot arithmetic, executes each redirected
handle instruction at indices above 19 through 127, checks flag/register preservation and canaries,
mutates every guarded byte, verifies patch boundaries, and executes the game's real indexed getter
and destructor loops with synthetic event-pool dependencies. The destructor must free every handle
once, in reverse order, and leave the count zero; repeated clear must do nothing.
Use `XML2_FIX_TEST_EXE` for the locally owned retail executable. It is never distributed in tests.
Native timing fixtures also execute initialization, paused updates, dispatch and cancellation at
20, 27, 64 and 127. Every native actor field is byte-identical to stock after initializing the first
nineteen entries. High events fire once; inactive entries stay inactive; mirrored fields and canaries
are checked. The same production callbacks execute the native packet producer/capture thunk, real initializer and paused/active dispatch. The eight-argument entry/trampoline is checked with a failed nested start between capture and initialization. Cache exhaustion retains the native nineteen and canaries; arena retirement permits thousands of later addresses. `[Test] MoveTriggerTrace=1` logs actor handle generations, arena epochs, occupancy and callback thread IDs for private diagnostics.
The pipe `status` reports `move_triggers 64` only after a successful installed raise.

See the private restore-wave report for source census, full gates and live acceptance evidence.
