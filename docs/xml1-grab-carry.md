# XML1 grab, flying carry and flight policy (candidate rule 45, revision 3)

`[Game] Xml1GrabCarry=xml1` enables ground grab, pickup/recovery, flying-charge
collision, flying carry and XML1 flight admission/upkeep. Rule45 revision3
advertises `{1,15,0,0}` on success. Revision1/mask1 and revision2/mask7 peers
refuse this contract both ways. Missing, empty, `xml2`, `off` and `0` perform no
image writes and add no rule. Invalid settings, unavailable prerequisites or
unsupported bytes register an unverified request, closing host/join.
See "Revision 3" below for installed AnimEnums/LiftRule requirements and the
companion builder's mandatory `XML1_FLIGHT_UPKEEP=True` switch.

Use the companion builder's `--grab-carry xml1`. It owns the matching INI key,
checks the companion DLL recognizes it, and removes the key on an OFF rebuild.
The default remains `xml2`. No release or content identity is assigned here.

The hold's native update at 0x4eda40 is retained. The five bytes at 0x4edb83
are redirected after its native time comparison: the displaced status test and
branch still run, followed by the XML1 large exception (actor stats pointer
at +0x35c, stats +0x2ad bit 3). Only `now > deadline` can release a nonlarge
holder. A large holder bypasses that release, with all validity, immunity and
directional branches unchanged. Both hold cleanup slots at 0x6932f4+8/+12
use 0x4ed890, equivalent to XML1 0xdc360: clear/publish victim flag bit 2,
without the PC interrupt path's target clearing and victim reset.

`ch_grab_attack` shares the existing 0x4fd860 lookup hook with cling and Rogue.
Its entry follows XML1 0xdc140: resolve the target, require a character, look up
`grabattackvictim` in the victim's style collection with null optional actor,
then call 0x432030 with `(move,-1,-1.0,0,0,0)`. No default entry runs; generic
update remains slot 6. A null move takes the native no-op path. The first
character entry logs `move_resolves`, whether the victim's current
move matches, and whether it changed; `[Test] GrabCarryTrace=1` logs each entry.
The authored `juggrabthrowforwardvictim` lookup/current-match diagnostic is a
separate line and never substitutes a move or adds a `grabattackvictim` node.
Current-move observation is not a complete forced-transition acceptance probe.
The PC service retains its additional health/state acceptance paths.

The authored `juggrabthrowforwardvictim` trigger stays distinct from that
callback lookup; no victim node is renamed or substituted. XML1's attack is
one authored hit followed by a throw/release, with no pound-loop counter.
The original `grabattackvictim` definition was absent in inspected XML1 loose
data; loaded-style resolution remains a live check.

The shared `ch_grab_victim` table at 0x693360 follows XML1's table: default
entry, no normal-exit cleanup, interrupt cleanup, and retention per-frame.
Retention requires a character owner whose resolved target is this victim and
whose current move starts with `grab`, `pick`, `tele` or `abil` (four bytes,
case-insensitive). Otherwise existing pair members receive 0.6 temporary
exclusion; the same callback clears/publishes bits 1,2,0 separately, runs
owner-relative cleanup and starts named Idle on the victim. Interrupt cleanup
uses the same detach but no Idle, and clears both auxiliary states when marked.
PC's extra victim-state early-out is removed. Slot 4 uses PC **ret 8**.

These handler callbacks are paired with the source acquisition/reset services.
After native `CCEAtkGrab` writes reciprocal owners, the bridge at 0x4de896
restores XML1's victim marker and bits 1,2,0 before sound/return. The actor-reset
bridge at 0x42c32f restores XML1's marker-gated clearing of the marker and bits
1,2,0. Unmarked actors retain their flags. This also covers ally victim moves
using the default handler; an empty victim normal-exit callback alone would
lose cleanup because PC actor reset omitted the source work. The fixture
executes native accepted ownership writes from clear flags and native
move-start/reset dispatch for shared/default victim moves, unmarked actors,
and reset=false transitions.

Fix 3 is restored as one transaction with the ground grab:

- Ally throw entry at table 0x693318+4 marks the victim at +0x764 bit 0x10,
  publishes entity bits 1,2,0 in source order, resolves `throwallyvictim`, forces
  transition using context +0x64 and `(move,-1,timing,0,1,0)`, then places the
  victim using holder position and reference angles. It preserves acquisition
  handles instead of writing PC's reciprocal owner tokens.
- Pickup-grab entry at 0x693438+4 resolves a physical target, disables both
  motion timers and sets physical +0xc5 bit 0x40 / +0x30d bit 4, including an
  already-marked target. It neither clears a foreign owner token nor rewrites
  selection/context flags. Acquisition eligibility still belongs to selection
  and authored triggers; this callback does not fabricate ownership.
- The source timer setter's `on` path unconditionally stores -1 in both timers.
  PC 0x471a20 adds a +0x259 bit 0x10 refusal. A local helper writes the mapped
  +0x23c/+0x240 timer bits; normal pickup's call at 0x4f0b6e uses the same helper.
  Other callers of the PC timer service remain unchanged.
- Both pickup cancellation slots use the native empty callback 0x59eb10,
  replacing PC's cancellation throw. Normal pickup completion and update stay
  native; their source behavior was retained.
- Thrown-land completion at 0x6933a8+8 always dispatches slot 3 cleanup.
  Recovery bypasses the owner block at 0x4f49e9 and resumes at 0x4f4a3d, retaining
  native health, attack/smash, strict deadline, and cleanup code. A valid owner
  neither blocks recovery nor gets cleared after two seconds. Native 50 and
  10/speed update behavior remains unchanged.

Flying-charge collision keeps the native tag/event and transition paths.
The nonactor branch at 0x4ee87c jumps past PC's contact-Z filter; object/empty
contacts select Special and complete velocity/flight cleanup even below -0.9.
With a selected next move, the bridge at 0x4ee8b3 restores acquisition of a
living struck character only when the held slot equals the native null handle.
It runs after tag 1 and before transition. An integer IEEE-754 predicate expresses
health >0 without new float operations, including +infinity/subnormal and NaN
cases. The native lock at 0x42d8e0 retains PC targeting-history bookkeeping,
an explicit downstream difference from XML1. `[Test] GrabCarryTrace=1` logs
successful collision acquisition. Jump-attack/fly-charge damage producers,
Colossus charge and the other survey fixes are untouched.

Handles go through 0x4654b0/0x4654d0 and the installed world's virtual slots;
there are no handle masks or arena assumptions. Flying carry uses the existing
query owner virtually (see below). Ground grab
adds no float arithmetic: the hold uses its original x87 comparison, and the
ported callbacks only pass exact source constants to native services. The
fixture checks x87 control and MXCSR across precision/rounding modes. Carry's
new arithmetic runs under `det_math::fp_env`.

The patch transaction acquires all pages and verifies all writes. If shared
lookup registration refuses, it rolls back every ground, carry and flight patch. An unsafe rollback
or page-protection failure refuses startup. Multiple independently enabled
lookup resolvers coexist; unknown names still run the displaced native lookup.

Verification (Win32 Release, owned retail input read only):

```
xml2_xml1_grab_carry_test.exe <owned-XMen2.exe>
xml2_xml1_grab_carry_install_test.exe <owned-XMen2.exe>
xml2_xml1_grab_carry_install_test.exe <owned-XMen2.exe> both
xml2_xml1_rogue_decide_install_test.exe
xml2_xml1_rogue_decide_install_test.exe both
xml2_xml1_rogue_decide_install_test.exe raised
xml2_xml1_cling_test.exe <owned-XMen2.exe>
xml2_gameplay_agreement_test.exe <owned-XMen2.exe>
```

The synthetic image fixture executes production callbacks and the native hold
routine loaded into its own reserved image. It checks 4 MiB OFF equality,
each guard refusal, failed-lookup rollback, missing/null/noncharacter targets,
forced-transition ABI, both cleanup slots, prefix and broken-pair release,
same-frame cleanup order, strict timeout and large/directional branches.
The deadline cases explicitly enter the guarded native deadline block: the
retail zero-axis input fixture selects action 15 without reading the clock.
That existing branch is preserved; live input routing to the deadline still
needs observation. The test does not invent an input change to force release.
It launches no game. The rules fixture verifies every guard against retail and
rule 45 agreement/refusal in both directions, including unchanged OFF bytes.

Live checks still required: large/nonlarge holds across the deadline;
attack-victim lookup and transition acceptance; one authored hit and throw;
authored victim distances and poses; interruptions/deaths/recycled handles;
EntityCapacity and cling/Rogue coexistence; on/on and on/off multiplayer.
No game, xemu, visual or multiplayer validation was performed.

## Revision 3: flying carry and flight policy

`[Game] Xml1GrabCarry=xml1` now installs the full rule as one guarded transaction:
revision **3**, feature mask **15** (grab 1, pickup/recovery 2, charge collision 4,
carry/admission/upkeep 8). Revision 2/mask 7 peers refuse this contract both ways.
The rule remains absent when OFF; OFF writes no executable bytes.

The full request requires **installed** `[Limits] AnimEnums=512` and
`[Game] LiftRule=xml1`. AnimEnums must publish its descriptor and all 23 builtin
names in descriptor order. Failed, absent or mismatched publication refuses the
entire rule, with no partial ground/carry install. LiftRule supplies the existing
XML1 heaviness/Might gate for converted props (especially heaviness 4 versus 5);
carry calls that installed native gate rather than inventing a second policy.
No new rule ID or actor allocation is used. The six persistent objects are the
existing grab-attack object and five new carry objects, each with eight slots.

`src/xml1_air_carry.cpp` implements `ch_fly_guard_decide`, `ch_air_grab_pickup`,
`ch_air_grab_pickup_idle`, `ch_air_grab_pickup_throw`, `ch_air_grab_victim` and
normal/interrupted cleanups. The query excludes the carrier, applies `(1,1)` and
`(1,4)`, sweeps from bounds center 48 ahead and 48 down in a 16-unit box, and reads
only the first contact. Rank must exceed two. Team, neutral-AI, conditional
pickup immunity and object branches preserve source order. Query calls use the
capacity owner's published entry bridges and virtual methods.

Pickup moves XY one percent per callback toward bone 6, preserves Z and adds
3.14 to carrier yaw. The default attachment uses bone 0 minus 55 Z (missing bone:
position plus 38 minus 55). Known enemy and ally carry poses align bone-5/6
hand midpoints in beta 9. Extending hand alignment to allies is the requested
visual adaptation: XML1 `30300` uses hands for enemy enums and root offset for
allies. Unknown poses retain the root fallback. Bone
lookup requests real animated skeletons with PC's added flags `(false,false)`.
`(true,true)` selects fixed approximations in `568E90`: bone0 becomes root+25
and the source -55 offset incorrectly puts an ally at root-30. XML1 `12CAD0`
queries the actual skeleton. Pickup
poses interpolate by 0.1. The selector reads the **victim's** selected move enum through PC slot +0xe8
(0x500840, table entry 0x694484); every ID
comes from `anim_enums::extension_id`, checked against `layout()`. Missing hand
outputs are seeded to their actor positions to define otherwise unspecified
source bone-failure results. Deltas use PC movement slot +0x130. All frame
callbacks, including empty ones, use PC `ret 8`. Victim release requires
reciprocal pairing, flying and airborne carrier; mismatch recovery excludes
both existing pair members for 0.6 seconds, detaches and Idles both.

082c's live carrier and victim enums were `-1` (`m31c`, 07:13:16 onward),
which bypassed that enum list and selected the root-minus-55 fallback. For
our victim handler, 082d also maps the eight authored pickup/idle/walk/run
victim/ally move names to the same attachment IDs. Break and unknown nodes
retain the fallback. This repairs attachment selection; the reason the live
enum getter was `-1` and the rendered asset pose still need live observation.

Fix7 replaces the entries of `ch_flying` (table 0x6931f8) and `ch_fly_move`
(0x6932d0). Map restriction reads manager **+0x8c / 0x483de0**, not +0x44.
The two source tests use `xml1_flight_eligible(actor)`: a documented constant-true
emulation of XML1 +0x335 bit0x20, whose PC field was removed. AI suppresses the
notification; restricted actors start Idle. Source flight preparation and entry
animation selection are restored. Matching native exit and landing/input paths
remain. Carry admission runs at **0x433e6c** inside PC flight descent
**0x433e46**, after its Guard **0x100**, no-power-modifier **0x80** and native
controller-refusal tests. XML1 **0x43312..0x43467** then requires a 20-unit
downward movement probe from the actor position (stop flag 1), a character
contact, the active hero in the actor's player slot, a non-large target,
Flight rank above two and the current move named `flying`. Only then does it
force `flyguarddecide` and retain flight without emission. Ordinary descent
continues on a probe miss, a non-character/large contact or an inactive carrier.
As in XML1, a valid contact
suppresses descent even when rank/current move prevents pickup. There is no
airborne-Guard shortcut in `ch_flying`.
082d retains the downward contact's complete handle for the decide callback,
instead of losing a directly-below target in the separate forward/down sweep.
Before starting pickup it lowers the carrier to the authored pickup height
(target root +40), using the native movement probe with character stopping
disabled. Solid-world blockage prevents pickup; the target is not pulled up
to compensate. Completion/interruption discards the saved contact.
The 082d follow-up corrects the probe contract: flag0 intersects property8,
which can include the contacted character. It temporarily supplies that
victim as the native owner exclusion at actor+178 for this one probe, then
restores the actual owner before applying movement. The complete native
42B6C0 fixture runs with owned libIGMath operations and separate character
and solid-ledge intersections. It checks stack balance and owner restoration.
The original 082d CarryTrace began only after pickup; its absence cannot
prove which earlier gate blocked a historical hover. With CarryTrace=1,
flight-input, effective `flight-rank-N`, pre-pickup gate results and position/
control flags are now logged before carry entry. No extra pickup predicate
or bone call is made for these diagnostics.
The decide handler's sweep for other entry paths remains first-contact-only, including its
normal LiftRule service for objects (Might + 1 >= XML1 heaviness, heaviness 5
refused, native refusal flags retained). An isolated prop cannot satisfy the
outer character-contact gate.

Carry Attack and Smash use XML1's generic throw amount: `L2` minimum **9**,
times the caster multiplier, truncated to the source signed damage word.
XML1 `3DF3F..3DFAD` calls `B7E60("L2", nullptr)`: max **11** is only returned
through the optional range output. PC numeric selector **1** (`4C49F0`) is
**DMG3**, not L2 (`6DA240`); the 082c values file gives DMG3 min3/max5. The
earlier fixture substituted 12 for that lookup and missed the wrong table.
The native throw event still executes once and owns motion, target release
and attribution. Damage after defenses and the visible landing remain live
checks.

The UI owner notification maps XML1 slot +0x2c to PC **+0x40** (0x48cbb0,
`ret 8`). Both original slots are no-ops in the inspected binaries. The call is
preserved, without claiming visible feedback. Lift failure uses PC +0x34's
32-byte argument contract, including its returned/replaced message handle.

The central native upkeep entry **0x4ec7a0** now runs XML1's policy: ranks 1..5
cost 40/30/20/10/5, rank zero or energy below the base cost stops flight and
starts Idle; ranks above five bypass upkeep. Energy equality is admitted.
Only `now > lastTick + 1` charges, once, without catch-up. AI skips the spender
but advances both timestamps (+0x6a4 and +0x6a0). Special mode C/value 2 replaces
the debit with 120 **after** the base affordability test, as in XML1. Ordinary
flight's native frame and carry's frame both reach this policy. New arithmetic
uses `det_math::fp_env`, restoring caller x87 control and MXCSR.

### `[Game] Xml1FlyingCarry=0` (beta 8 candidate 080h)

Default on (the key absent or any value other than a false flag = everything above). `0` keeps revision 3 without the
flying carry: `xml1_air_carry::prepare(writes, false)` registers none of the five carry handlers (their names fall
through the shared lookup to the native one) and leaves the descent admission at 0x433e6c native, so a hovering
Guard descends as on PC. Fix7's two flight entries, the upkeep at 0x4ec7a0, the `%flight_pwr` parser filter and every
ground write are unchanged; the dependency checks (AnimEnums, LiftRule, all guards) are the same. The rule advertises
`{1,23,0,0}` (bit 16 = fix7/upkeep without the carry) instead of `{1,15,0,0}`, so the two configurations refuse each
other online. Install lines: `... (rule 45 revision 3, feature mask 23); fix7 and XML1 upkeep installed`, `flying carry
off (Xml1FlyingCarry=0): no carry handlers, PC flight descent`, `thrown bodies forward world impacts (42C09E)`.
Fixture: `xml2_xml1_grab_carry_install_test.exe <owned-XMen2.exe> nocarry`; the rules fixture checks 15 / 23 agreement.

### Builder handoff (must ship together)

In the companion builder, set **`xml1build.anim_enums.XML1_FLIGHT_UPKEEP = True`**
and build with `--anim-enums xml1 --grab-carry xml1` and XML1 LiftRule. This
selects XML1's `flying` node in the existing precedence table and removes PC's
`energypersecond="%flight_pwr"` debit. Converted flight_pwr talentvalues may
remain; a talentvalue alone never spends energy. The three retained PC nodes
`flymelee1`, `flymelee2`, `painairfeetfirst` also carry this drain. Rule45 filters
**only** the case-insensitive `%flight_pwr` expression at the guarded per-second
parser branch **0x4f74b2**, clearing move+0x146 bit0x40 and leaving its expression
field untouched. Other expressions keep the original parser and debit. This
prevents a second flight debit throughout the merged graph; removing the
`flying` powerup alone would have missed these three nodes. No builder files are changed by this
DLL-only branch. Keep the original `velocityup="-200"` smash trigger spelling.

### Code-only validation and live work

Native fixtures execute the production installer/callbacks on invented actor,
query, move and manager objects. They read the owned retail executable only for
byte guards and selected matching native tails. Coverage includes OFF 4-MiB
image identity, dependency refusal, every new guard, lookup rollback, rule45
agreement, handler branches, geometry, enum cases, cleanup ordering, PC callback
ABIs, cost/time boundaries, AI and special mode, and caller FP environments.
Fixtures do not prove live bone output, collision movement, content precedence
or lockstep simulation equivalence. See the work report for exact results.

Pending live checks (never run by this code-only task):

1. OFF baseline bytes/log/agreement; on/on agreement and on/off/revision2 refusal.
2. Eligibility ranks 0/1/2/3/5/6, first-contact obstruction, ahead/below geometry.
3. Ally/enemy/neutral-AI true/false, immunity, heaviness4/5 props, special modes.
4. Pickup/idle/walk/run and turns, all victim/ally clips, missing bones, walls,
   slopes and frame-rate comparisons.
5. Forward throw, smash, interruption, landing, flight loss, death, disappearing
   and recycled handles; both actors' handles and constraints afterward.
6. Energy equality, strict one-second boundary, rank costs, AI, special120 and
   exactly one debit with the rebuilt flight content.
7. Missing enums/nodes, simultaneous/repeated carries, EntityCapacity, cling,
   Rogue and co-op agreement. Animation design L1..L7 remain live-only too.

The carry input bridge leaves an already-selected current idle/walk/run node
running. XML1 **0xdc0b0** selects that chain on every input callback; PC
**0x4341d3..0x4341eb** sends a reported chain through **0x4315d0** and move
entry again. Reporting the current node repeatedly restarted carry entry and
its time-zero victim event in the PC port. Different nodes, including throw
and smash, still select a chain. Guard is not converted into a throw input.

The multi-frame fixture starts with empty owner handles, executes the retail
victim-event constructor and ownership/release instructions, and drives the
retail PC input/chain path. It covers pickup, an established victim following
XYZ movement over 120 frames, five genuine idle/walk/run transitions, turning,
ascending/descending inputs, throw or landing/drop, and cleared reciprocal
owners/constraints, with Guard both held and released at contact. Native victim
reset instructions also run after carrier entry and before frame movement.
Carrier frame restores constraints for a reciprocal flying airborne pair before
attachment, closing that callback-order gap. While pickup owns the pair,
the general PC Guard-descent tail is consumed without another contact probe.
Beta 9 retains the acquisition Guard press through established carry with a
full-handle sidecar, even when the PC's 20-unit probe misses. Release during
pickup or carry clears it. A fresh Guard press retains the probe/descent policy:
contacts consume descent, while empty air permits descent. Movement transitions
retain the latch; interruption, pair loss, a new acquisition and throw clear it.
Landing and flight loss still release the pair. The retail bone path executes
down to a synthetic renderer matrix; actual animation/collision remains live.

World impact forwarding at `42BF70` retains the owner, neutral and friendly
prechecks, then removes PC's extra physical-target/temporary-psy gate at
`42C09E`, matching XML1 `3AF9E`. This covers both carry and TK throws through
their installed rule45 lifecycle. Retail receiver `499100` still owns armed
damage, deadline, speed, ordinary impact fallback and cooldown; no immediate HP
debit is added. The fixture executes that receiver and its native descriptor
constructor/copies with amounts 5/19/42, attribution, expired/stationary contacts
and repeated-contact cooldown. Its terminal damage sink is synthetic, so live
health and defense behavior still require verification.

Beta 9 wraps native `victim_throw` registration `692378` only when flying carry
is enabled. Unrelated events execute the same retail `4DB830` once. Successful
events on our carry-throw handler also restore XML1 `CEC34..CEC74`'s complete
constraint release/detach (PC `4DB9DF` clears only bit2), then arm enemies with
the same evaluated `L2` and caster multiplier as the XML1 generic throw service,
a two-second window and native descriptor/attribution fields. Native authored
motion, ownership exclusions, impact receiver, defenses and cooldown remain.
Friendly-damage-off allies and neutral NPCs do not receive this amount.
The source carry event `CEA90` itself does not arm a separate amount: applying
the generic throw damage policy here is an adaptation to the reported carry
damage failure, not a claim that that event contains an omitted L2 write.
The fixture executes the complete native event including both return paths,
Attack/Smash velocities, eligibility negatives and the arming stores.

082e follow-up. The armed amount is consumed only by a contact impact: in 082e
only a throw that reached a wall cost HP (4), open-floor landings and the
velocity-less Smash cost 0. XML1's thrown/landing handlers (`E0E20`..`E10C0`)
and `CEA90` contain no damage of their own. The victim callback keeps XML1
`DC720`'s carrier-only release rule (carrier ground handle or +4 bit 2); the
carried one's own contacts never end the pair, now pinned by the fixture. Every
082e moving-carry separation was the carrier landing on a prop or raised floor.
With CarryTrace, a release logs `carry trace release reason=...` with both roots
and ground handles.

[Live acceptance and input driver](beta9-moves-live.md) cover the outstanding
hardware, HP/defense and visible-pose checks.
