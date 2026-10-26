# Controller enumeration during shutdown

The retail controller worker at `0x618C90` can have several live instances after
returning to the main menu. All of them use manager `0xA6A7E4`. Previously, each
worker cleared that pointer at `0x618E5A` and set completion byte `0xA6A7E1`, even
while another worker was still enumerating. A returning worker reloads the
manager at `0x618D20` for the count call at `0x618D29`; a cleared pointer faults
at `0x627375`, reading address `0xC`. The name lookup also reloads the pointer.
An exit code of zero does not exclude this first-chance exception.

`input_worker` adds lifetime accounting at entry and exit. The last active worker
clears the manager and publishes completion; earlier exits leave both intact.
Entry and final publication use the same short exclusive lock so a newly entering
worker cannot race between the zero count and the pointer clear. Enumeration,
device detection, controller bindings and the native stop request stay native.
No lock is held during enumeration or while waiting for a worker to finish.

The two destinations (6 and 17 bytes) install together before game threads run.
Eleven short retail signatures guard the destinations, dependent manager reads,
thread registration, return and shutdown wait. Any mismatch logs its address and
retains stock behavior. The existing transaction verifies writes and rolls both
sites back on failure; an unverifiable rollback refuses startup. This engine
lifetime repair applies to both games without an INI or gameplay agreement rule.

`xml2_input_worker_test <retail-executable>` checks the actual installer and
bridges against synthetic continuations: suspended-reader/peer-exit scheduling,
two and three overlapping workers, repeated lifetimes, register/stack/FPU
preservation, every signature byte, and partial two-site rollback. The executable
is a read-only source for validating signatures, never a committed fixture.

Live acceptance must distinguish deliberately staged scheduling from ordinary
menu/load/return/Quit sequences. Record first-chance exceptions, thread identities,
the manager and completion byte, not just process exit codes. In the staged
schedule, hold one worker before its manager reload and let a peer finish first:
stock clears the pointer; the guard retains it until the reader finishes.
Physical controller connection/reconnection requires its own hardware coverage.

## Measured on 1.8.0-beta.2 (owned harness, real menus and real Quit)

Staged schedule (debugger holds one worker before its manager reload, lets a peer finish):
without the guard the held reader faults (first-chance access violation at `0x627375`, read of `0xC`) with the
manager already zero; with the guard the peer exits with the manager still set and completion still withheld, the
reader finishes, then clears the manager and publishes completion. Ordinary load, return-to-menu and Quit sequences
without staging: 2 of 5 unguarded quits raised the same fault; 0 of 20 guarded quits did.

## Historical investigation before the repair

# Native input-worker race on Quit

The classic-build investigation found a first-chance access violation at
`XMen2.exe+0x227375` (`0x627375`, reading address `0xC`) during native Quit.
The process then exited with code zero. Exit zero alone does not establish an
exception-free shutdown.

The retail controller-enumeration worker starts at `0x618C90`. Multiple workers
can coexist after returning to the main menu; this also occurs with the released
1.7.1 companion and release-0.2.5 content. The loop checks shared manager
`0xA6A7E4`, enumerates devices, then reloads that manager for the device count at
`0x618D29`. A worker leaving at `0x618E53` clears the shared manager at
`0x618E5A`. Another worker returning from enumeration can then pass null to
the native count routine, producing the observed fault.

Private debugger observations captured two overlapping workers in the combined
build: one cleared the manager, and the other immediately faulted at the count
routine. Ordinary released-build controls exited without this exception, with
two and three overlapping workers observed. A diagnostic deliberately suspended
one released-build worker immediately before its manager reload, let the other
clear the manager, and resumed the first. This reproduced the same access
violation with release-0.2.5 content and release-1.7.1 code. The freshly compiled
release DLL's entire executable section matched the released DLL byte for byte.

That last run is **staged thread scheduling**, not ordinary-play shutdown
acceptance. It establishes the existing retail lifetime race. It does not measure
the probability of encountering it in either build. The combined build also
completed an ordinary approximately 29-minute session and native Quit without
an exception. Normal-run frequency differences remain unquantified.

No classic HUD destination modifies this worker, its registration or these
shared globals. No teardown patch was added in the classic polish change.
A separate repair should establish one owner for enumeration and shutdown,
rather than merely suppressing the count routine's null dereference. Such a
repair needs controller connection/reconnection and repeated main-menu
round-trip coverage of its own.
