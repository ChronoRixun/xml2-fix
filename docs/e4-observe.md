# Stage-2 instrumentation (before the 96-actor core)

Enable explicitly in a test candidate:

```ini
[Test]
E4Observe=1
```

The absent key preserves beta-8 behavior. This step leaves actors/controllers at 32,
teams at 4, all native pool layouts and tokens unchanged, and adds no agreement rule
or save feature. It works with stock delayed damage and with `ActorPools=124`.
The later `Actors=96` core must imply these diagnostics; this change does not implement
that key, its prerequisites, reservation enforcement, tier policy, or fold-back.

`E4_OBSERVE` on the existing test pipe returns bounded JSON. This does not enable the
pipe by itself: the existing test-pipe configuration still applies. Present sampling writes `E4 observation` lines at most once per second when counters, snapshots or traces change, even with the input pipe disabled. The existing test-state sampler also feeds that same throttled path. Construction escapes
and native exceptions are logged synchronously before termination. Fatal paths still terminate if diagnostic logging itself raises.

## Installation and composition

The generated retail ledger has 113 spans and hashes of 45 complete owner functions.
No whole native function body is embedded. The generator pins the owned executable's
SHA-256 and checks instruction bytes against the read-only instruction database,
relocates relative instructions, and refuses incoming branches into a displaced span.
Every stub preserves GPRs, EFLAGS/DF, x87/MMX, XMM and MXCSR.

All observer code is prepared in writable memory and then made executable/read-only
before the single protected transaction writes a game byte. A mismatched guard, hash,
allocation/protection failure or unexpected overlap refuses the requested install and
startup; an uncertified rollback retains its backing. The exception observer always
returns `EXCEPTION_CONTINUE_SEARCH`.

Earlier writes from both existing transaction implementations and the guarded TickRate
patch are recorded only when E4 is requested. Their certified original bytes are applied
in reverse to a copy of each current function, whose hash must equal the pinned retail
hash. This composes existing intentional edits without accepting unexplained changes.
Actual write-span overlaps are refused. Rule 16 removes the virtual call at `0x505011`
(`ADD ESP,4`); that verified replacement is retained and its absent callback bracket is
elided. The beta-8 profile therefore installs 112 spans / 677 guarded bytes, while the
stock host installs 113 / 683. All 44 original B1 handler sites are accounted for.

## What is observed

O1 tracks controller states/generations, publication and release, both the controller resolver (`0x525160`) and the team resolver (`0x525100`, CAI vt+0x21c), the B1
update/event/44 handler brackets, embedded-handler ownership and stale tokens. Borrow
stacks are per thread, bounded at 128; their storage is prepared only on an opt-in install, with 32 concurrent active contexts and immediate reuse of idle contexts. Context exhaustion fail-stops before untracked lifecycle work. Individual stack overflow is visible, and a dropped inner bracket
cannot pop an outer one. Events retain the native caller, generation, step and borrowed
return addresses, including aliases held by other threads (up to 128 addresses per event, with the total count retained). The initialization bracket at `0x504f64` obtains its owner from ESI,
because the call itself initializes the handler back-pointer.

O3 tracks admitted actors, factory entry, constructor entry, controller token storage,
constructor return and publication. The admitted phase retains the factory frame so an
exception between allocation and constructor entry is observable. The constructor stub
records ESP and FS:[0]; its return updates the saved ESP after native argument cleanup.
First-chance SEH and C++ exceptions retain their phase, code and fault address. Before
allocation, actor release, CAI reset and the next client-frame entry, an abandoned open
construction reaches the existing fatal process path. It never rolls back a half-built
native object. An exception after PUBLISHED does not reopen its closed record.

All eleven X1 recovery scopes retain their old data-probe recovery (including SAFE console queue insertion) but protect their
native lifecycle calls with opt-in fail-stop. The forced-teams outer C++ catch also
fail-stops when enabled. No SEH record is propagated using a bare C++ `throw;`.

Accounting keeps published, constructing, destroying, orphaned and reserved/pending
counts separate, with peaks. Quiescent checks are suppressed during an open factory,
CAI reset or zone loading. Active arena actors must hold resolving controller tokens;
ownerless published controllers become orphaned observations. These are diagnostics,
not the later core's native reservation/backstop implementation.

O2 records caller, entity, descriptor, pre-allocation occupancy, actual return and
accepted/unsuitable/pending/full classification. Unordered/NaN delay follows the retail
predicate, including its flag-bit override. `would` counts suitable, non-pending
allocation attempts at occupancy >=32, including full-capacity attempts; it excludes
31 and the earlier descriptor/pending refusals. Unknown callers outside the five audited
six-byte virtual calls are counted. No event is rescheduled or accepted by this layer.

O5 brackets the five team writers and the retail timeout, captures all 15 listed field
values/provenance before and after, and observes 23 direct field stores in those writers
and their common helpers. A same-value formation store refreshes its provenance. Team
release marks that provenance dissolved after the native ring push, rather than while cleanup is still running. The two specified consumer probes count only
paths that actually consume formation direction, honoring the validity/branch predicates.
Re-registration at `0x512862` is bracketed too. T/S fields are kept; class-table revision
1 has no D field. Actual tier-1 fold-back snapshots require the later core.

O4 emits bounded normalized actor/team/update-decision traces. Test-only encoding
parameters provide default, mask-255, offset-generation and reverse-FIFO variants under
`XML2_E4_TEST`; release code has no alternate-encoding setting or reader. Applying these
to the raised native pools and running full T21/T14/T15/brain differentials belongs to
core step 8, as do T22 and T25.

## JSON arrays

- `events`: `[kind, slot, caller, generation, step, detail, borrow-count, borrow-returns]`, last 64.
- `event_callers`: caller followed by the 14 event-kind counters.
- Event kinds: release-borrowed, reuse-borrowed, resolve-during-destroy, illegal-transition,
  handler-mismatch, stale-token, escaped-construction, accounting, overflow,
  stale-formation-read, damage-mismatch, first-chance-exception, team-resolve-during-destroy, team-stale-token (indices 0..13). Team event slots are `0x10000 | slot`, with the team pool generation.
- `phase_counts`: ADMITTED, ACTOR_INIT, CONTROLLER, INIT_RETURNED, PUBLISHED.
- `accounting_peaks`: published, constructing, destroying, orphaned, reserved.
- `open_constructions`: phase, actor address, controller slot, saved ESP, FS head,
  exception code, fault address, thread ID.
- `slots`: slot, state, generation token, owner actor. States are FREE, RESERVED,
  CONSTRUCTING, PUBLISHED, DESTROYING, ORPHANED (indices 0..5).
- `damage_callers`: caller, accepted, unsuitable, pending, full, would-at-32, unknown.
- `damage`: caller, entity, descriptor, occupancy, reason, return, step, would; last 64.
- `snapshots`: point/slot/team/step/after, with fields `[offset, observed, provenance-value,
  writer, team-token, provenance-step, dissolved]`; last 64.
- `trace`: point, logical actor, logical team, decision/result, step; last 256.

## Verification and live handoff

Build with the repository's Win32 MSVC CMake configuration. New targets are
`xml2_e4_exception_test`, `xml2_e4_observe_test`, and `xml2_e4_observe_host` (the last
requires the owned retail executable path). The host runs native fragments privately;
it does not enter the game. Its `XML2_E4_TEST` callback seam is absent from the release DLL.
The generator supports `--check --exe ... --db ...`.

Full test/gate commands, hashes, the baseline explanation for the environment's named-pipe
failures, and the required live measurements are in the work-folder REPORT.md. No live
measurement or 96-actor behavior is claimed by this instrumentation step.
