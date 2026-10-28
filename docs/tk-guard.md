# Phoenix telekinesis and guard selection

These are independently selected additions to SharedHandlersC, gameplay rule 46
revision 1. `TelekinesisRule=xml1` is bit 512; `GuardDecideRule=xml1` is bit 1024.
Both are under `[Game]`. Missing, empty or `xml2` leaves the native callbacks,
parser and agreement unchanged. Neither key enables the other.

Both require **installed** `Xml1GrabCarry=xml1` (rule 45 revision 3 in this base).
That in turn requires installed `[Limits] AnimEnums=512` and `LiftRule=xml1`.
An invalid request, missing installed dependency, foreign service hook or failed
transaction leaves the entire requested rule-46 mask unverified. Requested keys
are never silently advertised as installed. The rule-46 installer now runs after
rule 45; rule 18 retains stun ownership and rule 39 retains its handler lookup.

```ini
[Limits]
AnimEnums=512
[Game]
LiftRule=xml1
Xml1GrabCarry=xml1
TelekinesisRule=xml1
GuardDecideRule=xml1
; The independent rank-2 jump cling/backflip handlers:
Xml1WallCling=xml1
```

## Telekinesis contract

The owner XML1 callbacks are entry `DFC80`, maintenance `DF5B0`, control/input
`DFF10` and cancel `DF640`. The PC registrations at `6938A4/AC/B0/B8` are replaced
as one unit, retaining PC update/input scheduling and the native no-op normal
completion. The native acquisition at `4F2ED0` retains its world enumeration,
ordering, 300-unit cone, 120-unit vertical band, candidate priority and line test.
Its custom-register candidate predicate at `4F2DA0` is replaced with XML1
`DF3B0`: script admission, claimed/locked/hidden objects, signed heaviness limit,
actor admission, tile exclusion and friendly-team policy. Token lookup always
uses the installed world's services; no actor-array indexing is added.

The parameter owner is **CCEAtkSuspend**, not an ordinary punch. The PC parser
`4E39F0` and native evaluators `4ABF70/4DAD20` supply the event parameters. An
opt-in alias at that parser maps XML1's authored `damage` to PC `ImpactDamage`.
Without it, converted Phoenix `damage=%xph_telekinesis_dmg` goes through the base
attack parser and leaves the suspend word at its native default zero. The alias retains
native encoded talent references and forwards every other attribute unchanged.
The same selection also restores the constructor default to source five at
`4F8D60`; the native constructor starts that word at zero. Native/source
constructor fixtures also verify damagelevel=1, tklevel=1 and object return.

| XML1 value | XML1 storage | PC input to the replacement |
|---|---|---|
| impact damage word | event+18 -> actor+538 | evaluated suspend event+18 |
| signed collision damagelevel | event+1B -> actor+53B | attack-info record event+14, byte+1C |
| signed heaviness limit | event+1A -> actor+53A | evaluated suspend event+1A |
| signed tklevel | event+1C -> actor+53C | evaluated suspend event+1C |

`damagelevel` and `tklevel` are distinct. Collision damage is signed
`damagelevel / 3`, truncating toward zero. Control energy and release use signed
`tklevel`. A full-token sidecar stores these parameters, source distance, held
sound, offset, last position and saved heading/time. It never overlays PC's
compact token cache. An active same-handler hold entry retains the sidecar and
does not execute another suspend trigger.

After an entry acquisition miss, 082d retains the evaluated parameters and
retries the native acquisition while the original TK button stays held.
Release or interruption discards the attempt. Target filters, range, ordering,
heaviness and team checks still belong to the same native acquisition path.
Pre-entry and auxiliary events run once per attempt, including a two-second
miss; retry never repeats their energy/event work. `[Test] TKTrace=1` logs
attempts 1/2/4/8/... with selected/auxiliary handles, current energy and limits.
The historical 4/10 misses have no acquisition telemetry, so their individual
causes cannot be recovered from the live report.

082d follow-up: TKTrace now reports candidate/accepted counts, rejection
categories and `post_filter_miss`, plus periodic control owner/flags/dt. The
082d trace's selected=0 through128 proves repeated native misses; it does not
identify eligibility versus height/team/LOS rejection. The native fixture
reproduces128 misses with a different first LOS contact, and acquires when
that obstruction clears. That is a preserved XML1 rule, not evidence that
the historical misses were all obstructed.

082e follow-up: a selected, owned, controlled target that has not moved for 15
control frames is logged once per hold as `tk blocked` with its contact byte
(+30C), height and the receiver's last-impact age (+2F4, written by `499100`).
Trace only; no decision changes. 082e's one such hold was a soldier placed by
`setPos` 18.6 units from Beast's centre: he took the armed TK amount at 0.1 s
without moving. XML1's entry `DFC80` sets the same victim flags and no
collision exclusion, so no behaviour was changed. Target choice was compared
with XML1 `DF740` and is unchanged: locked target first, then query order; an
enemy at once, an object at once only within 100 units in front, else the
object is the fallback (PC `4F2ED0` has the same branches and constants).

The `psycarry` victim uses `ch_grab_victim`. Its normal prefix policy retained
an owner named `tele...`, but the converted Phoenix start is `power1`.
Installed rule46 now supplies a complete-token ownership predicate to rule45,
so the victim callback can run before the start-to-hold transition without
dropping the pair. No blanket retention of arbitrary `power...` moves is
added. Missing ownership, a different generation, or cancellation still
allows normal cleanup.

The moving-hit flag also uses LiftRule's decoded source heaviness. Testing the
native byte would miss source heaviness 2, which the conversion encodes as byte
1. Control and cancellation reread the actor's current held token, and invalid
tokens do not authorize flag writes even if a resolver returns a stale pointer.

Entry selects source `psycarry`, sets the victim's physical owner and ground
flag, saves distance/heading, and adds 120 to the relative lift height. The
normal control path retains XML1's 600 movement limit, 360 vertical rate,
off-camera multiplier 0.1, 0.98 motion retention, 120 displacement clamp, small
motion thresholds and AI distance path. Non-actors retain the held rotation.
Energy is charged when the new simulation-time remainder is lower than the
previous remainder, using **double(float(0.2))**, not decimal double 0.2. Cost is
`(21 - signed tklevel) * 0.2f` (100 in the source special mode). Equality with the
available energy permits control; a strictly lower balance releases it.

Release and interruption clear/publish the victim ground flag before the native
release service. PC's separate physical-mode cleanup is retained. Voluntary
release supplies impact/damagelevel, the source release sound, heading and signed
tklevel; cancellation supplies the current victim velocity. Both clear active
state, stop the held sound and discard the sidecar. Rule 45's shared victim
retention recognizes the `tele` move family and its pickup entry accepts the
source's already-marked objects.

For a voluntary TK throw the final PC-only argument to `42E570` is private
mode **2**. The guarded bridge at `42E923` admits the native arming block even
when the normalized moving direction has speed exactly one. Retail skips that
whole block at speed <=1; setting its ordinary overwrite flag alone therefore
does not fix moving throws. The subsequent nonzero selector at
`42E99B..42E9BA` writes computed native `L2` damage, matching XML1's generic
throw policy `3DF3F..3DFAD`. Ordinary PC modes 0/1 replay their original speed
condition; cancellation passes zero. The regression executes `42E914..42EA13`,
including the real speed comparison, native L2/caster evaluation calls,
deadline and complete descriptor stores, for speeds 0/1/1.0001/500 and modes
0/1/2. It covers actor/object and moving/still TK releases.

Stationary/vertical-only releases retain a zero horizontal direction through
normalization. XML1's release service `3DCA9/3DD0B` and PC `42E6DE/42E752` then
use the **victim's facing**, including the native upward impulse. XML1's higher
controller `E0A6E` itself has the undefined reciprocal; copying that operation
would supply NaNs and bypass the service fallback. All bridge normalization
divisors require a positive finite length; the velocity/time divisor retains its
positive timestep threshold. Rule45 also restores XML1 world-contact forwarding
to the native thrown-body impact receiver; see [grab/carry](xml1-grab-carry.md).

## Guard policy and the wall paths

XML1 entry `DA3A0` sets context bit0; input `D99D0..DA193` corresponds to PC slot
`693018`. Beta 9 performs the one-shot interaction policy at entry `693004`,
so admission no longer depends on a later input tick while Guard is held. This
is a candidate fix for the hardware-input symptom; no live hardware run was
performed. The incoming context's move is used for chain resolution (`501AF0`):
PC installs it at actor+378 only later, at `431252`. The incoming pending move
is cleared first. An actual result is dispatched once, and context bit2 aborts
the parent transition at `4310B0`; no unconditional pending dispatcher runs.
The slot-6 policy remains available if an authored chain cannot be resolved.
An empty decision selects Idle. Immediate wall-lunge dispatch still returns
**false**, matching XML1 `D9E73`, preventing an extra dispatch.

082d requires the native stun flag (`victim+765 & 1`) before an enemy ground
grab. General admission at `429228` is not a stun test. Round two made its
ordinary action-3 chain (`guarddecide -> grabstart`) execute immediately at
entry, exposing untouched-enemy grabs. Ally Fastball and object pickup retain
their separate branches; TK and flying carry retain their own eligibility.
This implements the brief's untouched-enemy negative. The inspected generic
XML1 callback itself also selects action 3 for an admitted nonstunned enemy
(`D9F5A..D9F70`), so that callback alone does not establish the narrower live
XML1 expectation. The host test records this deliberate policy difference.

The 082d follow-up also recognizes a native active `shared_stunned`
attachment when reaction bit0 is clear. Native traversal starts at entity
`+21C/+220`; attachment slot64 supplies its definition and slot4C its next
reference. All dereferences use the pool's generation-validating virtual
methods, including raised AttachedPowerups. For an active timed stun, a
private call bridge retains native `42B840` and `42925E` onward, bypassing
only the reaction-or-recent-hit clock gate just as reaction bit0 does.
Other native status/ownership restrictions remain. This treatment applies
to nonparty stunned allies too. Party heroes retain rule18's reaction-owned
eligibility: a refused/interrupted reaction cannot be reinstated merely by
the pending PC powerup; a genuine flagged stun retains Fastball admission.

`[Test] GuardTrace=1` (or TKTrace with both rules enabled) records reaction,
attachment handle, computed stun eligibility and native admission. The old
082d JSON records idle/visible stars, not those raw fields; do not present
the attachment/reaction-clear fixture as a captured historical state.

Use [the human-length pad/keyboard acceptance procedure](beta9-moves-live.md).
Direct handler fixtures establish scheduling behavior, not hardware routing.

The policy includes an existing held interaction (`use_button`), repeated
movement fallback, the 48-unit probe and 16-unit box, actor and neutral targets,
ally/fastball eligibility, rule-18 stun, named `def_pickup` immunity (XML1 17 ->
PC 2C), acrobat air-grab, pickup claim and cannot-lift feedback. The initial
actor-aware Might test uses PC's effective lift statistics, which the converted
Might affecter supplies; the raw-stats path looks up the talent `might` itself.
It does not substitute an object's heaviness for raw Might. XML1's low-four-bit
talent rank is retained. The shared actor-admission body is reused after PC's
additional nonhumanoidskeleton preamble; no global predicate is patched.

Guard's rank **greater than 2** acquisition is `walllunge` (CName initialization
`E90CA..E90F4`, string `3D8904`). It accepts level/tile geometry or the source
physical-wall flag, traces facing * 24, checks signed normal Z < 0.2 and facing
dot normal < -0.5, turns toward the wall, applies the trace shift, and starts the
named move. Source guard compares signed Z here; the independent rule-39 jump
cling path uses an absolute-Z comparison. Rule 39's rank-2
`clingwalldecide -> clingwallbackflip` pair is neither replaced nor used as the
guard's named move.

The source six-argument query wrapper `A17C0` applies axis mask 7 (all axes),
expands facing/distance and forwards flag 1 to `A1770`. PC `4AA1E0` uses all axes;
`4AA180` forwards query byte+32 twice where the source forwards that flag. PC
initialization `4AB43B..4AB450` sets byte+32=1 and its extra defaults +30=1,
+31=0. Those bytes and the wrappers are guarded. Capacity-owned constructors,
contact accessors and team filters accept only their owner's exact published
jump target, so raised query layouts use their owner's virtual services.

The guard sets physical-object byte+30D bit 04 before entering pickup. Rule 45
keeps that claim and admits the object; the guard never clears another actor's
owner token or installs another pickup lifecycle.

## Code-only verification

Build Win32 Release below normal priority with MSBuild `/nr:false`. Run:

```text
build/native/bin/Release/xml2_tk_guard_test.exe <XMen2.exe> <default.xbe>
build/native/bin/Release/xml2_shared_handlers_c_test.exe <XMen2.exe> <default.xbe>
build/native/bin/Release/xml2_shared_handlers_c_test.exe <XMen2.exe> <default.xbe> --off-proof
python -B scripts/shared_handlers_c_matrix.py <XMen2.exe> <default.xbe>
```

The fixtures hash-bind the owner's PC/XBE images. They execute XML1 entry,
control, cancel, candidate and guard code with synthetic downstream services;
native PC parameter/parser instructions also execute. Checks cover signed
parameters, authored parser alias, same-family retention, energy boundary,
release/cancel, claims, rank/wall boundaries, immunity and air-grab. The installer
fixture checks every guarded byte, dependency refusal, exact write footprint,
foreign/published query ownership, and agreement selections. The OFF proof makes
the fixture's complete four-MB image inaccessible during native selections and
then compares every byte plus the serialized agreement. The matrix covers all
2,047 nonempty selections. The full native runner includes both suites and OFF.

The acquisition fixture also executes complete owner XML1 and PC acquisition
routines for ordered/locked objects, same-team rejection and the inclusive
120-unit height boundary. Auxiliary dispatch covers same-team objects and checks
the stack and callee-saved registers. The Might fixture distinguishes effective
actor Might from raw rank zero. Hit-flag fixtures cover all six source heaviness
classes and the signed damage threshold.

These are handler/ABI/installation proofs with synthetic services, not live
physics certification. Integration still needs Phoenix acquisition/hold/release
and interruptions on actors and objects, psylift/psycarry transitions, sound and
energy over authored ranks; guard pickup ownership, ally throws and wall-lunge
alongside rule39; and two-peer agreement checks. No game or emulator was launched
for this work.

The PC audio name resolver is **vtable 0x69d1ac +0x38**, function **0x590bd0**,
with three stack arguments and `ret 12`. XML1 sound helper **0xdf4a0** uses
Xbox slot +0x34 for that operation; the PC slot +0x34 is **0x590120**, a
one-argument boolean query (`ret 4`). Both loop start (`power1a`) and thrown
sound lookup (`power1b`) use PC +0x38. Copying the Xbox slot corrupted the PC
stack. The TK fixture now executes the retail disabled-audio lookup and return
instructions through entry and release, with the real incompatible +0x34
callee present as a regression trap. The installer guards the lookup table,
callee ABI and loop/validity/position/stop slots before enabling TK.
