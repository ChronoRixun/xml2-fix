# E4 Stage 2 core

Binding design: e4-live-actors-design revision 3.2, section 5.2 and referenced
sections. Opt-in key: `[Limits] Actors=96`. Release identity is unchanged.

amended 10-27 by the coordinator: bit 2 because bit 1 is NATIVE_W2

The actor-capacity save requirement is `0x4`, named
`feature_actor_capacity96`. `0x2` remains `NATIVE_W2`. Existing saves without
`0x4` load unchanged. A save carrying `0x4` is refused by older builds with
their readable newer-save message. The bit is needed only above 32 live
actor-class entities or above 32 actor-class dynamic records in any
remembered zone; no native save structure or content identity changes.

Required prerequisites in the same protected startup transaction:
EntityCapacity=1, ActorPools=124, AnimEnums=512 (128 cache records),
CharSounds=127, ResidentStats=320 (22 hero / 320 NPC payloads and residency
guard), MotionPool=22528. Requested failure poisons ActorAI agreement.

Rule 1 revision 4: `{96,768,1024,10}`.
Rule 44 revision 2: `{96,0x00020060,0x007c007c,15}`.

Teams retain eight members and the native acceptance rule. Tier zero uses
its own four-slot FIFO; tier one uses lowest free slot. The P2 team cap is
4/8/12 at controller bands <=32 / <=64 / <=96. Above32, new teams wait below
the cap after a band decrease. At32 or fewer, new tier0 admissions ignore
lingering extended teams, as the stock rule restricted to tier0 requires.
At the next quiescent simulation boundary, extended teams dissolve through
native leave. Controller +0x538 clears;
all T/S fields survive and native updates re-register members. Reset class
table revision 1 has T fields +0x2e8 / +0x350 and no D field.

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
settings. Adding only Actors intentionally refuses Stage 2 and names the
missing ActorPools prerequisite. No builder default was changed.

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
clone, native timeout byte comparisons, reservation/backstop and recovered
constructor failures, lifetime aliases, 10,000-operation churn, native brain
paths and complete FX-state preservation, actual delayed-damage returns,
and all three test-only token variants. The full runner also checks save
vectors and old-file round trips. Corpus fixtures must be copied to the
workspace: durable parity writes `actual.*` files beside its inputs.

With the core installed, E4 JSON additionally exposes `native_reserved`,
three `pending_reservations` bitmap words, and `foldbacks`. Fold snapshots
use point `0xe4000001`. The null-team fallback's authored callback is
bracketed too, with the actual generated call return retained in borrow
events. Test-only encoding controls have no release INI key or DLL symbol.

No game was launched for this implementation. Stage 3/default enablement,
all Section 9 live runs, model/HUD headroom, resource-variety peaks, co-op
lockstep, real in-game save/load and measured instrumentation cost remain
live acceptance work.
