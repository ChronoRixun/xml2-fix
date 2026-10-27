# Fight moves

`[Limits] FightMoves=1024` raises the retail FightMove pool from 332 slots.
333..2048 are supported; absent or 332 retains stock instructions and contributes
no agreement entry. An invalid requested value or a failed install leaves the
pool unchanged and marks rule 54 unverified, preventing online agreement.

This restores headroom for faithfully converted first-game styles. The current
content census peaks at 306 moves per zone and 108 moves in one style. A style's
own 150-node move map stays at 150.

## Layout and generated ledger

Retail XMen2.exe SHA-256:
`146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b`.
The lazy singleton is `[0x789e1c]`, getter `0x4f7a90`, constructor `0x4f7a40`.
`tools/w3_fightmoves_ledger.py --check --verify` generates and checks 56 operands
from the executable, scans all `.text` for pool field references, and checks the
sole allocator, pop, and findNext callers. Set `XML2_FIX_TEST_EXE` to the retail
executable, used as read-only data.

Packing follows the retail constructor, with `W = ceil(N/32)`:

| Field | Formula | Retail (332) | 1024 |
| --- | --- | --- | --- |
| Records | N * 0x148 | 0..0x1a95f | 0..0x51fff |
| Bitmap A | N * 0x148 | 0x1a960 | 0x52000 |
| Ring, N+1 dwords | bitmap A + 4W | 0x1a98c | 0x52080 |
| Write / read / free | ring + 4(N+1) | 0x1aec0 / 4 / 8 | 0x53084 / 8 / c |
| Bitmap B | write + 12 | 0x1aecc | 0x53090 |
| Live | bitmap B + 4W | 0x1aef8 | 0x53110 |
| Allocation size | live + 4 | 0x1aefc | 0x53114 |

All capacities, displacements, bitmap `REP STOSD` counts and the getter's size
remain imm32/disp32 edits in their original instructions. Record stride stays
`0x148`; guards pin both stride multiplications and the unchanged reciprocal
division at `0x500b94` (`0x63e7063f`, shift 7).

## Exhaustion admission

Retail pop `0x5001d0` does not check free count. Allocation 333 constructs over
live slot 0 and makes free `-1`. The per-style map's node pop `0x500240` is also
unchecked. One entry cave over the six-byte prologue at `0x500de0` checks both:

- Pool live >= N: count a pool refusal.
- Style live at `style+0xc60` >= 150: count a map refusal.

The pool check takes precedence when both are full. Refusal returns `AL=0; RET 8`
before either pop, construction, parsing, or insertion. The caller at `0x5011cc`
ignores AL, so the move is absent. Its saved ECX is the style; insertion uses
`style+0x74`, with the node pool another `+0x0c` inside it. Each reason logs once
per style address using bounded DLL storage. Below both limits the original
prologue and native allocation path execute unchanged. The guard is installed
only with the raise.

Status includes `moves n/N (refused r, map-refused m, aliasing a)`. The aliasing
sampler counts observations with live > N or signed free < 0. It should remain
zero with the guard installed. Counters use atomic snapshots because the test
pipe can read status concurrently with the parser's admission callback.

## Installation and MoveTriggers

FightMoves commits and publishes before `limits::contribute` prepares
MoveTriggers in the shared startup transaction. This occurs before the game's
entry point and before its first lazy singleton allocation. Every operand,
invariant guard, and the null singleton cell are checked in one protected
transaction. The null cell is checked again immediately before writes. Failed
preparation writes nothing; a certified rollback releases the entry cave. An
uncertifiable rollback retains storage and refuses startup.

`fight_moves::descriptor()` publishes the effective count after verified commit
and reads the current singleton base on demand. MoveTriggers uses that descriptor
both for `N * trigger_capacity * 4` storage and aligned slot validation. At
1024/64 this is 262,144 bytes. Its two old cross-owner operand guards become
prefix-only checks while FightMoves owns those operands; stock mode retains the
complete original guards. Constructor and stride guards remain in force.

Rule **54 FightMoves**, revision **1**, values `{N,0,0,0}`, is requested-only.
MoveTriggers stays rule **37 revision 1**, with its original values and emitted
byte plan. FightMoves adds its own rule because exhaustion affects which moves
exist. Mismatches refuse in both directions by name.

## Native verification

Build `xml2_fight_moves_host` and `xml2_fight_moves_off_test` with MSVC Win32.
Run the host with the retail executable followed by a mode: `retail`, `333`,
`1024`, `2048`, `combo`, `negative`, `off`, `stock`, `invalid`, `base`, `exists`,
`badguard`, `protect`, `rollback`, `readback`, `retain`, `counters`, or `guard0`..`guard66`. `failfast` deliberately
terminates and must be checked by a parent process.

The host executes native pool construction, pop, free, stride division, findNext,
teardown, style construction, map insertion/balancing, lookup and removal.
Synthetic services provide move payload construction and parsing. Canaries cover
the expanded allocation. The negative control uses the retail unchecked path;
the combined case exercises every side-array slot, including slots above 331.
The OFF test compares exact MoveTriggers writes and cave bytes against headers
extracted from 69f5115. No game or engine entry point runs.

To run all focused cases, set `XML2_FIX_TEST_EXE` and
`XML2_FIX_FIGHT_MOVES_HOST` (the built host's absolute path), then run
`python -B tools/test_w3_fightmoves.py`. Without the host variable it runs the
generator/whole-text and cross-rule scan checks only. Set `XML2_FIX_STYLES_SOURCE`
to the separate W3 styles checkout when it is outside the default work folder.
The historical OFF comparison target is available when commit 69f5115 is present;
source archives and shallow checkouts can still build the DLL without that
optional differential target.
