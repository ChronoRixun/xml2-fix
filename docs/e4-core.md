# E4 Stage 2 core

Binding design: e4-live-actors-design revision 3.2, section 5.2 and referenced
sections. Opt-in key: `[Limits] Actors=96`. Release identity is unchanged.

amended 10-27 by the coordinator: bit 2 because bit 1 is NATIVE_W2

Design section 7, amended 10-27: conservative marking after review - demand
counting missed remembered zones.

The actor-capacity save requirement is `0x4`, named
`feature_actor_capacity96`. `0x2` remains `NATIVE_W2`. Every campaign written
while the Stage 2 core is installed (the save layout gains `0x4` only after a
verified core publication) carries `0x4`, whatever its current or remembered
actor count: the earlier demand count (current live actors, remembered
actor-class dynamic records) missed placed actors in remembered zones and
mixed dynamic-plus-hero demand. Saves without `0x4`, which is every existing
save and every save written with `Actors` absent, load unchanged. A save
carrying `0x4` is refused by older builds with their readable newer-save
message. This build (it carries the core) refuses it naming the setting when
`0x4` is the only bit it is missing (with or without the W2 bit 1): Load Game
says "This save was made with 96 actors turned on. Turn on Actors=96 ([Limits]
in xml2-fix.ini) and restart to load it." and the slot list shows `(needs
Actors=96)`; when `Actors=96` is in the ini but did not install this start,
the text names the startup refusal instead ("Actors=96 is in xml2-fix.ini but
did not start (missing prerequisite [Limits] ActorPools=124); fix that and
restart to load it."). An unknown bit keeps the newer-build text, and the file
is never touched (an overwrite keeps it as `.newer`). `[Limits] Actors=96` is an opt-in candidate setting; no native save
structure or content identity changes, and no engine query runs at save time.

Required prerequisites in the same protected startup transaction:
EntityCapacity=1, ActorPools=124, AnimEnums=512 (128 cache records),
CharSounds=127, ResidentStats=320 (22 hero / 320 NPC payloads and residency
guard), MotionPool=22528. Requested failure poisons ActorAI agreement.
When a prerequisite is missing or wrong, stage 2 writes nothing: no core
backing, caves, operand edits, descriptor or save capability. Features that
do not depend on Stage 2 keep their own contributions and commit as they
would without `Actors`. The log line names the prerequisite, for example
`Actors: missing prerequisite [Limits] ActorPools=124; stage 2 writes nothing
(other features keep their own transactions); ActorAI unverified`, or
`wrong prerequisite [Limits] <key>=<value> (needs <required>)`.

That wording applies only to the key checks (and to the image-base, consumer
registration and backing-allocation refusals in preparation), where Stage 2
never contributes to the startup transaction. A refusal inside the
transaction is different: when every key is right but a prerequisite's
contribution is not in the transaction or does not verify (`prerequisite <owner>
not prepared` / `not verified: ...`), or the core ledger, cave protection or
mandatory instrumentation fails, Stage 2 adds a veto and the **whole startup
transaction is refused**: no feature in it installs this start. The log says
so: `Actors: prerequisite ActorPools not prepared; the whole startup
transaction is refused (no feature in it installs this start); ActorAI
unverified`. An uncertified rollback retains the backing and refuses startup
(`Actors: uncertified core rollback; backing retained and startup refused`).

Rule 1 revision 4: `{96,768,1024,10}`.
Rule 44 revision 2: `{96,0x00020060,0x007c007c,15}`. Revision 2 means the
fold-back policy of xml2-fix 739cc0f (stale memberships retired through the
native leave, below; the class table revision 1). The never-released fe3f621
also registered revision 2 but fail-stopped on a dead stale listing and
cleared a living moved member's current +0x538; a peer on fe3f621 is not
distinguished by the revision.

Teams retain eight members and the native acceptance rule. Tier zero uses
its own four-slot FIFO; tier one uses lowest free slot. The P2 team cap is
4/8/12 at controller bands <=32 / <=64 / <=96. Above32, new teams wait below
the cap after a band decrease. At32 or fewer, new tier0 admissions ignore
lingering extended teams, as the stock rule restricted to tier0 requires.
At the next quiescent simulation boundary, extended teams dissolve through
native leave. Controller +0x538 clears;
all T/S fields survive and native updates re-register members.
Extended teams may list stale memberships that retail itself leaves behind:
reactivation re-registers and overwrites +0x538 without leaving the old team
(0x516845-0x51684e), and destruction leaves only the current +0x538 team
(0x511cdb-0x511ce4). Fold-back retires each stale listing through the same
native leave (which works on the token alone) instead of fail-stopping: a
member that no longer resolves is only removed, as the retail timeout skips
a null resolver (0x5276a4); a living member whose +0x538 names another team
keeps that current membership and every byte. Only a member whose +0x538
names the dissolving team has it cleared. Retired stale listings are counted
in the E4 JSON as `stale_fold_retirements`. Reset class
table revision 1 has T fields +0x2e8 / +0x350 and no D field.

Fidelity against `T_ref` (the retail one-member team timeout, design 2.8).
After fold-back each controller's bytes equal those `T_ref` produces from the
same pre-state except for these differences, each a retail-reachable state:
(a) teams of any size dissolve (`T_ref`: at most one member, after five
seconds); (b) hero members get +0x538 = 0 (`T_ref`: a stale token every lookup
refuses); (c) members leave through the stock leave, so the last leave frees
the team through the normal free path; (d) **a living stale member keeps its
current +0x538 where a retail timeout would zero it (0x5276b3)**: a member
re-registered into another team (tier 0) by reactivation still appears in the
dissolving extended team's list; fold-back leaves that listing but keeps the
member's current team, while the retail timeout path stores 0 into +0x538 of
the member it times out regardless of which team it currently names.

T22 compares the complete controller bytes after fold-back with the actual
retail timeout run on a stock-layout clone of the same pre-state (50,048
bytes), allowing only the hero +0x538 difference (b). Its stale cases
(`stalefold` / `stalelive`, reactivate-then-destroy while shrinking 34 to 32)
pass the same comparison, with this caveat: the reference forces the retail
timeout only for controllers whose current team is extended, so the living
stale member (its current +0x538 names a tier-0 team) is not timed out in the
reference. For that member T22 shows that it keeps its tier-0 +0x538 and every
other byte - the documented exception (d) - not byte equality with a retail
timeout of the dissolving team, which would zero it.

Instrumentation is compulsory with the raise. The pre-core observation
contract and diagnostic meanings remain in [e4-observe.md](e4-observe.md).
No live acceptance is implied by host tests. The binding Section 9 live
procedure is recorded in the local `temp/fix/LIVE-PROCEDURE.md` handoff.

The external R2/R3 checker's source-regex overlaps and source-sensitive
appendix mismatch are an accepted tooling limitation. The consumer guards
use `stats_layout::expected()`. The checker is unchanged; exact R2/R3
ledger/retail tests and the actual guard checks remain required.

The released 0.4.2 profile omits ActorPools because Stage 1 was opt-in. Add
both `ActorPools=124` and `Actors=96` to that profile; retain all its other
settings. Adding only Actors intentionally refuses Stage 2 (stage 2 writes
nothing) and names the missing ActorPools prerequisite. No builder default was changed.

Game-free gates:

```powershell
python -B scripts/e4_core_sites.py --exe <retail-XMen2.exe> --db <native.sqlite> --check
python -B scripts/check_e4_core_guards.py --exe <retail-XMen2.exe> --db <native.sqlite>
python -B scripts/test_e4_core.py --host <bin>/xml2_e4_core_host.exe --exe <retail-XMen2.exe> --out <workspace-output>
python -B scripts/run_native.py
```

The core driver covers prerequisite failures and every operand guard,
96-actor/controller boundaries, the rebased generic arena, two-tier FIFO
differentials, cap-band decreases, fold-back into an actual stock-layout
clone, native timeout byte comparisons, stale memberships left by
reactivation and destruction while shrinking 34 to 32 (dead and living
resolvers), reservation/backstop and recovered
constructor failures, lifetime aliases, 10,000-operation churn, native brain
paths and complete FX-state preservation, actual delayed-damage returns,
and all three test-only token variants. The full runner also checks save
vectors and old-file round trips. Corpus fixtures must be copied to the
workspace: durable parity writes `actual.*` files beside its inputs.

With the core installed, E4 JSON additionally exposes `native_reserved`,
three `pending_reservations` bitmap words, `foldbacks`, and
`stale_fold_retirements`. Fold snapshots
use point `0xe4000001`. The null-team fallback's authored callback is
bracketed too, with the actual generated call return retained in borrow
events. Test-only encoding controls have no release INI key or DLL symbol.

No game was launched for this implementation. Stage 3/default enablement,
all Section 9 live runs, model/HUD headroom, resource-variety peaks, co-op
lockstep, real in-game save/load and measured instrumentation cost remain
live acceptance work.
