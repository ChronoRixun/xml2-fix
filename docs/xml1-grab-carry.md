# XML1 grab lifecycle and charge collision (candidate rule 45, revision 2)

`[Game] Xml1GrabCarry=xml1` enables the grab/pickup/recovery and flying-charge collision candidate.
Missing, empty, `xml2`, `off` and `0` perform no image reads or writes and add
no gameplay rule. Unknown settings or unsupported bytes register an unverified
request, closing host/join. Rule 45 revision 2 advertises `{1,7,0,0}` on success:
installed and feature mask 7 (ground grab 1, pickup/recovery 2, flying-charge collision 4).
The earlier revision-1/mask-1 ground-only candidate refuses this contract in both
directions. Flying carry and flight admission/upkeep are **not implemented**.
A future carry implementation must change the agreement revision or mask.

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
character entry logs whether lookup resolved, whether the victim's current
move matches, and whether it changed; `[Test] GrabCarryTrace=1` logs each entry.
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
there are no handle masks, arena assumptions or query objects. Ground grab
adds no float arithmetic: the hold uses its original x87 comparison, and the
ported callbacks only pass exact source constants to native services. The
fixture checks x87 control and MXCSR across precision/rounding modes. Carry's
future arithmetic still requires an explicit fp_env guard.

The patch transaction acquires all pages and verifies all writes. If shared
lookup registration refuses, it rolls back ground patches. An unsafe rollback
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

Deferred carry/fix7 work: six handlers and their branch fixtures; confirmed session
gate/UI ABIs; rank >2 first-contact geometry and XML1 might object gating;
bone-0 and hand-midpoint attachment/interpolation; exact XML1 upkeep and single
energy debit; throw/interruption/victim cleanup; animation enums and authored
nodes with deliberate XML2 same-name precedence. The builder still retains
XML2's flying moveset and still reports missing `ea_flyingcarry_*` and
`ea_jug_grab_*` enums as deferred. Those missing victim animations mean this
ground-rule change alone does **not** establish full Juggernaut visual parity.

Fix7 remains paired with carry/upkeep: ch_flying and ch_fly_move admission/cancellation are unchanged. The XML1 +0x335 bit0x20 restriction has no confirmed PC mapping. No guessed bridge is installed.
