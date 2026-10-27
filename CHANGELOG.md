# Changelog

## 1.9.0 - <date TBD>

1.9.0-beta.1 is the port's beta 8 (Legends Classic 0.4.1-beta.1, content 28 / `LC37`): the first game's bosses, moves,
grabs and damage rounding, the raised engine limits and the 4 GB exe, saved rooms kept across the beta 8 map edits,
Colossus's charge hitting and the GRSO Officer's and Captain's guns and grenades (the builder's style-name fix). The
Legends Classic 0.4.1-beta.1 builder writes every key below except `TelekinesisRule`, `GuardDecideRule` and
`[Limits] ActorPools`, and writes `[Game] Xml1FlyingCarry = 0`: Phoenix's XML1 telekinesis, the XML1 guard decide and
the flying carry are in progress for the next beta (switched off, not removed), and the actor pools stay opt-in. A
1.9.0 PC and a 1.8.0 PC (beta 7 or stable) refuse each other at join. 1.9.0-beta.2 is beta 8's hotfix: the
controller scan cache below (the once-a-second pause with a controller connected), with no gameplay rule or agreement
byte changes, so beta.2 and beta.1 PCs play together and the 0.4.1-beta.1 build needs no Rebuild. The entries below
are newest first.

- **Controller scan cache** (`[Input] CacheDeviceScan`, default `1`; 1.9.0-beta.2 and 1.8.1,
  [docs](docs/controller-scan-cache.md)). The retail zone timer (`0x6256E0`) asks for a DirectInput controller scan
  about once a second (worker `0x618C90` -> `0x628E20`, EnumDevices at `0x628E54`); the enumeration measured
  170-900 ms, and with a pad connected it showed as a pause (Team Management's clouds). Controller enumerations are
  now collected on a background worker and replayed from complete immutable snapshots on the caller's thread through
  the existing spoof and virtual-pad callbacks (order, structure size, bytes, HRESULT and DIENUM_STOP preserved). Raw
  Input topology is compared every 250 ms, with a 30-second safety rescan; a cold query still waits for its first
  complete list (the startup scan, main-menu input, libIGDisplay's one-shot count). XInput slots are discovered on
  XInput's own worker once a second and after a failed read; frames read only the selected slot.
  `CacheDeviceScan=0` restores the previous enumeration and discovery paths. New native fixtures
  `xml2_controller_scan_cache_test` and `xml2_xinput_connections_test`. Owner live check on the beta 8 candidate
  080i: the pause is gone with a pad connected.

- **Resident stats: gameplay rule 50** (`[Limits] ResidentStats=320`; candidate). Raises outer CStats and NPC
  payload pools together to 320; hero payloads stay 22. Admission checks payload availability before allocation,
  with native fallback and once-per-definition refusal logs. All 275 ledger rows, moved consumers and agreement
  `{320,22,320,1}` install atomically. Absent or 31 preserves stock writes/agreement. Native saves are compatible
  for the same 21 resident heroes; live RV3/RV3b acceptance remains pending. See `docs/resident-stats.md`.

- **Motion pool: gameplay rule 51** (`[Limits] MotionPool=22528`; candidate). Raises the 500-record
  native motion pool to 22,528 with all three lazy allocations, all bitmap clears and the generated 82-row
  ledger applied together. Owners admit clips 0..175; longer lists are refused and logged once per owner
  lifetime. Four readers return the native fallback outside the valid list. The published ActorSlots bound
  must fit and the pool must not exist yet. Absent or 500 preserves stock bytes and agreement; other values
  refuse online. The test pipe reports motion peak and refusals. Live RV4 acceptance remains pending.


- **Character sound cache: gameplay rule 49** (`[Limits] CharSounds=127`; 1.9.0-beta.1). Raises the
  16-record cache to 127 so a seventeenth live sound directory keeps its own sounds. Six address edits and both
  getter/teardown bounds commit together; the published actor capacity must be below 127. Absent or 16 keeps stock
  bytes and no agreement entry; other values refuse online. `CHAR_SOUNDS` on the test pipe reports peak occupancy,
  refusals and empty-sounddir actors. Native fixtures verified; live RV1 acceptance pending.

- **Address space (E12).** The Legends Classic build's `XMen2.exe` is large-address-aware (4 GB on 64-bit Windows and
  Wine/Proton). The fix logs `address space: laa on|off; 2 GB guard ...` at start and, with the flag on, keeps the
  64 KB block below 2 GB reserved, so no allocation can straddle 0x80000000 (a D3DX8 mip-filter loop linked into the
  exe, cgD3D8.dll and libIGGfx.dll compares pointers signed and only misbehaves for a buffer across that line). The
  test pipe's `status` adds `memory laa on; top ...; above 2 GB n MB; private n MB; highest ...; 2 GB guard on`;
  `memory high <MB>` (test pipe only) reserves the free space below 2 GB except <MB> for a controlled high-address
  test. An unhandled crash writes one `crash: exception ... at ... (module+offset), writing/reading 0x...; laa on;
  ...` line to `xml2-fix.log`, then the crash goes on as before. Nothing is refused: with the flag off it only logs.
  The `limits: address space - LAA ...` line stays.

- **Saves keep the zones you already cleared across definition-only map edits** (1.9.0-beta.1, the save reader,
  [docs](docs/lcsave-library.md)). The reader dropped every saved zone record whose map file hash changed unless an
  append-only prefix row covered it, so beta 8's heaviness tags (65 maps, nothing moved or renumbered) would reset
  visited zones: enemies, pickups, doors and one-time scenes back. It now also accepts the builder's full-length
  equivalence rows (`Data/lcsave/zone-prefix.lcm`, format unchanged) and reads such a record exactly as an
  unchanged map's. New offline gate `scripts/zone_records_gate.py` (target `lcsave_zone_records`): per zone, real
  saves apply at least as many records on the candidate as on stable.

- **The first game's revival price: gameplay rule 10 revision 3** (`[Game] RevivalCost = xml1`; 1.9.0-beta.1 and
  1.8.0, [docs](docs/xtraction-shop.md#revival-price)). A fallen hero costs 200 x G^2 Tech Bits, G = min(5, L / 10 +
  1), L the roster's highest current level without guests: 200 / 800 / 1,800 / 3,200 / 5,000 for 0-9 / 10-19 /
  20-29 / 30-39 / 40-45, whoever fell. That is X-Men Legends I's price: default.xbe 0xaf6b0 charges 200 x G^2 with
  the group registry vt+0x58 = 0x54150 makes from vt+0x50 = 0x54c30's highest level. 1.5.0 to 1.8.0-beta.7 charged
  200 x L^2 (80,000 at level 20 instead of 1,800): their notes took the neighbouring slot vt+0x54 = 0x54cb0 (the
  acknowledged level) for the group. The 0x4b8830 hook stays; a fourth guard covers 0x449960's roster loop (guest
  skip, running maximum). With the key set, rule 10 is revision 3 (values unchanged), so a fixed PC and a beta 7 PC
  refuse each other naming `10 Xtraction services/RevivalCost`; without the key the entry stays revision 2 and the
  OFF record is unchanged.

- **The objectives page lists what the first game listed** (`[Game] ClassicObjectives = 1`; 1.9.0-beta.1, display
  only, [docs](docs/classic-objectives.md)). XMen2.exe's PDA page gives slot n to the n-th top-level objective
  including hidden ones (0x5cce40), so a hidden objective left a hole and the page opened on slot 1, often a finished
  mission. The key makes the selector walk (0x5cce98) and the page walk (0x5ce148) count visible objectives only, opens
  the page on the current one (0x5ce484, the remembered-focus cell 0x8afe40) with a six-slot window (slot 7 stays the
  optional page) and leaves the Danger Room results page native. Checked live on 10-25 at 1280x720 windowed and
  2560x1440 borderless, beside X-Men Legends I in xemu.

- **XML1's grab lifecycle and flying carry: gameplay rule 45 revision 3** (`[Game] Xml1GrabCarry = xml1`;
  1.9.0-beta.1, [docs](docs/xml1-grab-carry.md)). Juggernaut's grab-and-hit (`ch_grab_attack`: the grabattackvictim
  lookup the PC never registered, the large-actor hold after the strict deadline at 0x4edb83), the shared grab victim's
  retention and cleanup, the ally throw, pickup admission and cancellation, the thrown-landing recovery (0x4f49e9's
  owner gate and two-second clear bypassed) and the flying charge acquiring the live character it strikes (0x4ee87c /
  0x4ee8b3) follow X-Men Legends I. New in revision 3: Phoenix's, Rogue's and Storm's **flying carry** (XML1's
  `ch_fly_guard_decide`, `ch_air_grab_pickup`, `..._idle`, `..._throw` and the victim handler: acquisition, hand
  attachment, walk / run, throw, smash, interrupted drop and both actors' cleanup), the fix7 flight admission and
  XML1's strict one-second **flight upkeep**; the parser filter (0x4f74b2) drops only the `%flight_pwr` per-second
  expression, so a converted PC flying node is never charged twice. Revision 3 needs `[Limits] AnimEnums` and
  `[Game] LiftRule = xml1` installed; without them it writes nothing and stays an unverified request (host / join
  closed). Values `{1,15,0,0}`
  (mask 15 = grab, pickup/recovery, charge collision, carry/fix7/upkeep); revisions 1 and 2 (080b) are refused both
  ways. Absent: nothing written, no entry. The carry input no longer reports the established carry node as a new
  chain when the idle / walk / run choice resolves to itself (the PC re-entered the carry every frame and the victim
  never followed; live 080d); logged as `flying carry keeps its established node (no self-chain)`. Live 080e/080f fixes
  (fix3): the carrier frame restores the victim's constraint bits after the native node reset (0x42C270), a paired
  pickup no longer falls through the general descent (0x433E6C) while Guard is held, a thrown body's world impact reaches
  its receiver again (the PC-only gate at 0x42C09E is opened; amount, deadline, defenses and cooldown stay native) and
  the carry pose reads the animated skeleton (0x41D620 with `false,false`) so XML1's offsets no longer sink the victim
  30 units; logged as `thrown bodies forward world impacts (42C09E); carry pose on animated bones`. Not played yet.
  `[Game] Xml1FlyingCarry = 0` (beta 8 candidate 080h; default on) keeps the rule without the flying carry, which is
  in progress for beta 9: the five carry handlers are not registered and the descent admission (0x433E6C) stays PC's,
  while Juggernaut's grab, pickup/recovery, the charge collision, fix7 and XML1's flight upkeep are written as with it.
  Values `{1,23,0,0}` (bit 16 = fix7/upkeep without the carry), so peers with and without the carry refuse each other
  both ways; logged as `fix7 and XML1 upkeep installed` and `flying carry off (Xml1FlyingCarry=0): no carry handlers, PC
  flight descent`.

- **XML1's animation enums: gameplay rule 48** (`[Limits] AnimEnums = 512`; 1.9.0-beta.1,
  [docs](docs/anim-enums.md)). XMen2.exe's animation enum table holds 210 names, every slot taken; the key raises it to
  512 (and the animation-set cache 16 -> 128 records) and gives XML1's 23 flying-carry and Juggernaut-grab-victim
  enums fixed IDs 0xD2..0xE8 (81 operand edits, five caves, two hooks, one startup transaction). The DLL publishes the names as a marked table (`XML2FIX.ANIMENUMS.1`) that the Legends
  Classic builder compares with its own. Absent or 0: nothing written, no entry. Not played yet.

- **Shared handlers wave C: gameplay rule 46** (eleven `[Game]` keys = `xml1`; 1.9.0-beta.1,
  [docs](docs/shared-handler-c-rules.md)). `JumpAttackDamageRule` / `FlyChargeDamageRule`: the producer's damage
  from `values.xmlb`'s L3 minimum with the first game's signed 16-bit stages (0x4ee99b / 0x4eeb88; velocity,
  deadline and knockback stay native). `ColossusChargeRule`: input 0x4eced0 and contact 0x4ed220 as one unit
  (event-100 affordability, the -0.71 crash test, special action, 450 recoil). `BeastBounceRule` (0x4ed1a0 always
  emits, stops and timestamps), `FastballRule` (0x4f4adf emits for actors, objects and null contacts),
  `IdleBoredRule` (no controller/team or non-humanoid exclusions; stun stays rule 18's), `LocomotionScaleRule`
  (0x4f01d4), `AnimationPhaseRule` (0x4f026a), `TeleportVisibilityRule` (0x4efc7e / 0x4f44a4). One mask
  `{installed_mask,0,0,0}`; rule 42 unchanged. `TelekinesisRule` and `GuardDecideRule` add the coupled suspend
  contract and interaction policy, requiring installed rule45; guard restores rank3 walllunge beside rule39.
  [Contracts and native fixtures](docs/tk-guard.md). Live integration checks remain.
  Phoenix's telekinesis sounds (`power1a` / `power1b`) resolve through the PC sound manager's name lookup (slot 0x38,
  0x590bd0); slot 0x34 is a PC bool query with a 4-byte return, and the XML1 call corrupted the stack at the first
  telekinesis hold (live 080d crash). Logged as `telekinesis sounds resolve through PC audio slot 38`.
  Live 080e/080f fixes (fix3): a telekinesis throw released with no velocity keeps the victim's facing fallback with
  the native upward impulse instead of dividing zero by zero (`a still telekinesis throw keeps the victim's facing (no
  zero divide)`); guard decide runs its policy in the input slot (0x693018) with XML1's short entry, so a tapped guard
  exits without moving the stick (`guard decide runs at input and exits after a tap`); Colossus's charge evaluates its
  converted damage, knockback and range on the caster, as the PC's own 0x4ED220 does, not on the struck soldier (zero
  damage before; `Colossus charge evaluates its talents on the caster`).

- **E4 stage 1, actor pools: gameplay rule 44** (`[Limits] ActorPools = 124`; 1.9.0-beta.1,
  [docs](docs/actor-pools.md)). The path, shadow and delayed-damage pools sized for 32 actors grow to 124 in one
  guarded transaction over an independently audited ledger (367 role rows / 357 instructions, five null-team guards);
  actor/controller/team counts and the save layout stay stock, the CAI descriptor is published for later stages, and
  NpcPowerRule's side tables reset when a controller slot is allocated (unhooked again if rule 16 is withdrawn). Values
  `{32,4,124|124<<16,1}`; any other value is unverified; outside the game image the key is ignored (no entry). Absent:
  nothing written. None of its live runs has happened; Legends Classic 0.4.1-beta.1 leaves it off by default.

- **XML1 boss brains: gameplay rule 40** (`[Game] Xml1Bosses = dopples,avalanche,pyro,mystique,shadowking,marrow,magneto,blob`;
  1.9.0-beta.1, [docs](docs/xml1-controllers.md#shared-boss-dispatcher)). One module owns the brain-dispatch
  detour (0x5173f3) and the script-table entries Master Mold used; Master Mold is its first client and writes exactly
  what it wrote before (rule 3 unchanged). The dopples restore X-Men Legends I's overlay (default.xbe 0x116f70):
  Wolverine and Colossus throw the fastball special every 5 s or more when within 400 of each other, Jean sets off
  the arena's hazards near the hero every 7 s, all four taunt; Cyclops' reply stays as dormant as in the original.
  `avalanche` restores his brain (0x111ab0) in both fights: power smash at close range and quake/ranged power at
  mid range on 4 s deadlines, smash/attack rolls between them, and the health-band flag at 83/66/50/33/16 %.
  `pyro` (astral3_3: Z filter, attacker reservations, fire up/down), `mystique` (copies a hero and changes back after
  the authored time or when the copied hero is gone, grenades, paths), `shadowking` (both forms' attacks, the timed
  shield powers, the floor relocation kept), `magneto` (asteroid2_1: the mag2-5 phase requests, the shield's return
  at mag4, five tile slots) and `blob` (XML1's two opcode programs, pickup and throw, slam/flop ranges, companion
  jumps) follow the first game's brains; `marrow`'s backboss brain is already native on the PC (recorded, never
  replaced). Each boss is a bit of rule 40's mask with its own revision; Master Mold keeps rule 3. Absent key:
  nothing written, no agreement entry. None of the fights has been played yet.

- **Receive-side damage stages and the damage cap: gameplay rule 41** (`[Balance] ReceiveRounding = xml1`,
  `[Balance] DamageCap = xml1`; 1.9.0-beta.1, [docs](docs/balance.md#the-receive-side-and-the-cap-receiverounding-damagecap-gameplay-rule-41)).
  X-Men Legends I's damage receive converts the hit to a whole number after the defense and after the scale
  (default.xbe 0x45517..0x45558); XMen2.exe kept a float (0x437a54..0x437a7c), so 10 against a defense of 0.5 and a
  scale of 0.5 was 4.75 instead of 4. `ReceiveRounding=xml1` calls two stubs at 0x437a54 / 0x437a75 that truncate the
  damage, the clamped difference and the product toward zero as XML1's `_ftol` does. `DamageCap=xml1` caps a hit at
  XML1's 32,000 (0x5ce0d) instead of 1,000,000 (0x453bfd / 0x453c0c point at a 32000.0f). Both are executed side by side
  with the first game's code in `xml2_balance_parity_test` (72,180 / 72,180 equal cases in 24- and 53-bit x87
  precision). Absent/`xml2` is byte-identical and adds no agreement entry. `scripts/prove_balance_install.py` now
  proves every `[Balance]` key (StrengthBonus / MindBonus / MightBonus / Rounding included) installs at run time.

- **Enemy AI fidelity, part 3: gameplay rule 16 revision 14** (`[Game] NpcPowerRule = xml1`; 1.9.0-beta.1,
  [docs](docs/npc-power-rule.md)). (19) The power picker's lock test goes for all seven other callers too (self buff,
  ranged/party-target helper, Sentinel probe and attack, FlyNoNav, HitAndRun/ShadesNoWing, flying creatures):
  0x50fe63 jne -> jmp. (20) The power decision's lock test goes for every caller, the officer weapon brain included
  (0x5187c4 jne -> jmp; revision 11's caller bridge is retired). (21) Class selection: Advanced Sentinel matches XML1's
  "dvanced" fragment again (category 0; XMen2.exe's "xxxdvanced" let it fall to Sentinel) and Juggernaut's default
  category is XML1's 4 (0x694f1e, 0x511fb8). (22) No Danger Room reaction gate (0x435b09 -> jmp 0x435b52): XMen2.exe
  suppressed a pain reaction within 2 s of the last in Danger Room mode (0xfd), XML1 never does. Revision 14 also
  carries the character bullet ray below; a revision 11 (1.8.0-beta.7) peer is refused by revision, as are the
  unreleased candidates 12 (these items alone) and 13 (the ray alone). Officer cells, Shades/Sentinels, Advanced Sentinel, Juggernaut and a
  Danger Room course need live remeasurement.

- **One-pass engine limits, wave 1: the runtime-only raises** (`[Limits] IGBCache`, `[Engine] ModelHandles`; 1.9.0-beta.1,
  [docs](docs/igb-cache.md)). The IGB info cache (`CIGBInfoCache2`, 200 records at 0x7b7fb0: every
  precached model file; full, a zone's map IGB fails and the game reloads the previous zone) moves into a block of the
  DLL at 201..2048 records, built and torn down by the game's own code: 82 operands, 4 references, the cache's three
  calls of the shared `bitset<200>::findNext` cloned; the name table comes along (2048 by default). `ModelHandles`
  raises the IModel handles and transforms past `ModelPools`' profile (8192 for 1024 IGB records). The cache is
  gameplay-agreement rule 31 (IGBCache, requested only); the handles are part of rule 28's values. The startup log
  reports LAA on/off and the highest reservable address (E12). Absent keys write no byte and add no agreement entry.
  Proof: `xml2_test --igb-cache-rules` (runs the patched pool at 1024), `scripts/check_igb_cache_text.py` (whole
  `.text`), the model-pool plan and lifecycle tests at 8192 handles.

- **XML1 character bullet rays** (`[Game] NpcSemiAuto = xml1`, rule 16 revision 14; 1.9.0-beta.1,
  [weapon brain](docs/npc-semi-auto.md)). An admitted bullet trace (beta 7's half-second trace gate) no longer uses
  the PC beam's two collision queries: it casts X-Men Legends I's zero-width ray from the gun's muzzle (weapon slot,
  `Gun1` bolt) 36 units back to 550 forward along the cached aim, blended toward the muzzle direction below a 0.9
  dot as default.xbe 0xBA0CD..0xBA1C8 does, and hits the first contact only (a character or a destructible prop takes
  the hit; the world stops it). Three spread draws stay before the gate, all float work under the game's own
  precision; 21 new guards. Not yet played: the 120/300/520 unit cells against the original are still to measure
  (the 300-unit target is 0.71-0.76 hits per admitted trace).

- **Seven shared combat handlers as in X-Men Legends I: gameplay rule 42** (`[Game] SkatingRule`,
  `TeleportLandingRule`, `PopupRule`, `PopupLandRule`, `ChargeRule`, `NightcrawlerRule`, `GambitRule` = `xml1`;
  1.9.0-beta.1, [docs](docs/shared-handler-rules.md)). Ice/lava skating's speed and height bands, Nightcrawler's four
  teleport landings, the popup / air-spin decision and landing reset, the charge's timed end with its sound and
  per-update event, Nightcrawler's buddy-teleport targets and Gambit's 60-unit object charge follow the first game's
  callbacks (executed side by side with default.xbe's in `xml2_shared_handlers_test`, 5,793 checks; every selection
  mask installs). One requested-only rule records the mask; absent keys write nothing. Not yet played.

- **XML1's wall cling: gameplay rule 39** (`[Game] Xml1WallCling = xml1`; 1.9.0-beta.1, [docs](docs/xml1-cling.md)).
  The acrobat moveset's `clingwalldecide` / `clingwallbackflip` (acrobatics 2) ran as XMen2.exe's default handler;
  they now turn the hero to a wall within 24 units and back-flip off it (60 back, 500 up), as default.xbe 0xD9740 /
  0xD9590 do. One jump at the FightMove handler lookup (0x4fd860), shared with Rogue's decide below. Not yet played.

- **XML1's lift rule: gameplay rule 38 revision 2** (`[Game] LiftRule = xml1`; 1.9.0-beta.1,
  [docs](docs/lift-rule.md)). Objects the Legends Classic builder marks with `xml1heaviness` (the first game's
  heaviness 4 and 5, which the PC folds into its unliftable class 3) are lifted by XML1's rule: Might rank 3 lifts
  heaviness 4, nobody lifts 5; their throw impact levels and carried damage, and Phoenix's telekinesis limits, follow
  XML1 too. Every other object keeps the game's rule and its heaviness byte. Not yet played.

- **Rogue's power decision: gameplay rule 43** (`[Game] Xml1RogueDecide = xml1`; 1.9.0-beta.1,
  [docs](docs/xml1-rogue-decide.md)). XMen2.exe has no `ch_roguedecide`; the handler (through the same lookup as
  the wall cling) sweeps 60 units ahead of Rogue, drains the first character it finds (`ability_drain`, with the
  target lock) or plays `drain_notarget`, and charges the energy once on the decide move, as default.xbe 0xDBD30.
  Needs the matching Legends Classic heroes output. Not yet played.

## 1.8.1 - 2026-10-26

A hotfix for 1.8.0 (Legends Classic 0.4.0, content 27 / `LC36`, unchanged): the once-a-second pause with a
controller connected. No gameplay rule or agreement byte changes, so 1.8.1 and 1.8.0 PCs play together; no Rebuild.

- **Controller scan cache** (`[Input] CacheDeviceScan`, default `1`; [docs](docs/controller-scan-cache.md)). The
  retail zone timer (`0x6256E0`) asks for a DirectInput controller scan about once a second (worker `0x618C90` ->
  `0x628E20`, EnumDevices at `0x628E54`); the enumeration measured 170-900 ms, and with a pad connected it showed as
  a pause (Team Management's clouds). Controller enumerations are now collected on a background worker and replayed from
  complete immutable snapshots on the caller's thread through the existing spoof and virtual-pad callbacks (order,
  structure size, bytes, HRESULT and DIENUM_STOP preserved). Raw Input topology is compared every 250 ms, with a
  30-second safety rescan; a cold query still waits for its first complete list (the startup scan, main-menu input,
  libIGDisplay's one-shot count). XInput slots are discovered on XInput's own worker once a second and after a failed
  read; frames read only the selected slot. `CacheDeviceScan=0` restores the previous enumeration and discovery paths.
  New native fixtures `xml2_controller_scan_cache_test` and `xml2_xinput_connections_test`. Owner live check on the
  beta 8 candidate 080i: the pause is gone with a pad connected.

## 1.8.0 - 2026-10-25

The first stable release since 1.7.1, the engine half of Legends Classic 0.4.0 (content 27 / `LC36`, the same
game as 0.4.0-beta.7; `dinput.dll` is the 1.8.0-beta.7 sources plus the revival price fix below, so it refuses a
beta 7 PC that charges the old price). In short: online co-op by code and
public games with drop-in and solo-segment watchers, online at 60 fps, the team-combo and quit crashes fixed with
four times bigger model pools, the classic HUD and Codex, enemies that fight like the first game (combos, every
gunman's weapon brain and bullet timer, powers on time, power attacks never blocked), one-sided walls in the first
game's levels, the "Alison" speaker label and the first game's revival price. The entries below are 1.8.0's own
fix and then the betas' changes, newest first.

- **The first game's revival price: gameplay rule 10 revision 3** (`[Game] RevivalCost = xml1`; 1.8.0,
  [docs](docs/xtraction-shop.md#revival-price)). A fallen hero costs 200 x G^2 Tech Bits, G = min(5, L / 10 + 1),
  L the roster's highest current level without guests: 200 / 800 / 1,800 / 3,200 / 5,000 for 0-9 / 10-19 / 20-29 /
  30-39 / 40-45, whoever fell. That is X-Men Legends I's price: default.xbe 0xaf6b0 charges 200 x G^2 with the group
  registry vt+0x58 = 0x54150 makes from vt+0x50 = 0x54c30's highest level. 1.5.0 to 1.8.0-beta.7 charged 200 x L^2
  (80,000 at level 20 instead of 1,800): their notes took the neighbouring slot vt+0x54 = 0x54cb0 (the acknowledged
  level) for the group. The 0x4b8830 hook stays; a fourth guard covers 0x449960's roster loop (guest skip, running
  maximum). With the key set, rule 10 is revision 3 (values unchanged), so a 1.8.0 PC and a beta 7 PC refuse each
  other naming `10 Xtraction services/RevivalCost` instead of debiting different amounts; without the key the entry
  stays revision 2 and the OFF record is unchanged.

- **XML1 bullet trace timing** (`[Game] NpcSemiAuto = xml1`, schema 4 rows; 1.8.0-beta.7,
  [weapon brain](docs/npc-semi-auto.md)). Converted XML1 bullets keep their shot presentation (sound, muzzle,
  tracer) while a separate per-actor trace timer gates collision and damage, as XML1's weapon trace time does: the
  MP5's 0.5 s gate admits equality and survives input, burst and target changes. Rows append the source trace time
  and accuracy (schema 4); rule 16 binds all eight words with SHA-256. Admitted traces keep the PC beam geometry;
  the character bullet-ray adapter is deferred. Legends Classic 0.4.0-beta.7 (content 27 / LC36) writes the
  schema 4 rows and the bullet markers and refuses a fix that cannot read them.

- **One-sided level collision for movement: gameplay rule 30** (`[Game] WorldBackfaces = xml1`; 1.8.0-beta.7,
  [docs](docs/world-backfaces.md)). XMen2.exe's common movement trace helper (0x4a9650) asks the collider
  for the level hull's back faces before every sweep (0x4a96c0 `push 1` -> collider vt+0x88); X-Men Legends I's helper
  (0xa0af0) never does and its hull test is always front-faces-only. In the first game's levels actors therefore
  stopped or wedged against walls seen from behind: HAARP's officer in haarp2_1's 520 staging pocket is boxed in by
  them and, below 60 fps, wedges at the free end of a wall strip (the G14 positioning stall at ~500). `xml1` changes
  that argument to 0 in the zones `[WorldBackfaces] ZoneCount / Z1..Zn` lists (a port's XML1 levels; the list is
  required, without a valid one nothing is patched): the six bytes at 0x4a96c0 jump to a bridge that pushes the current zone's flag (the zone manager's path), so
  XML2-origin maps a port install still carries keep XMen2.exe's two-sided collision (owner decision); the
  entity-capacity owner of the helper asks the same flag. Entity collision and every other trace are unchanged.
  Absent/`xml2` is byte-identical and adds no agreement entry; revision 2 `{1, scoped, zones, 0}` plus the zone list's
  SHA-256 as the rule's detail: an `xml1` PC and one without the key or with another list refuse each other naming
  `30 WorldBackfaces`.

- **Enemy AI fidelity, part 2: gameplay rule 16 revision 11** (`[Game] NpcPowerRule = xml1`, `[Game] NpcSemiAuto =
  xml1`; 1.8.0-beta.7; [docs](docs/npc-power-rule.md), [weapon brain](docs/npc-semi-auto.md),
  [round 8](docs/npc-weapon-round8.md)). (1) The brain runs through locked moves, as XML1's (items 16-18):
  XMen2.exe's attack-state update returned at once in every input-locked frame of an enemy that had used a power
  (0x512f01..0x512f14; XML1's 0x10ad70 has no such exit), so its visibility query, brain and power decision did not
  run during attacks, powers and reactions (2.6 power-decision calls/s in attacks, 0 in powers, against the
  original's 23-39) and a power re-check due then went off on the first free frame, 0.5-7 s late (median 2.5 s on
  the still-hero bench). The exit is never taken (0x512f0b jz -> jmp), the melee brain's power decision skips its own
  lock test (0x5187c4; the officer weapon brain's call keeps it) and its picker is entered past the picker's lock
  test (0x50fe5c; the per-slot chain test still refuses). NpcSemiAuto's own lock bridge at 0x512f0d is withdrawn (rule
  16 now covers every actor), and the interim call-gap fronts (beta7 development) are withdrawn with it. (2) A block
  is a counted melee to-hit miss, as in XML1 (items 14-15): an attack whose damagescale isn't `normal` (XML1's
  PowerAttack) is never rolled (0x44f58e), and the damage receive's `block_chance` roll never zeroes a hit
  (0x437b08); on the PC about a quarter of such rolled power attacks were blocked with no damage. (3) The weapon brain
  takes schema 3 rows (`actor|recoil|range|burst|leader|warmup|flags`) and covers every XML1 gun family: the input
  gate's warm-up deadline (nullifier, knockback: before every shot; lightning, flame: once per hold), the continuous
  play flag, the pistol's default-handler primary, the original 0.25 s idle transition (was 0.05 s), the bullet
  line-of-sight drop (0x519e3a, XMen2.exe's visibility test through rule 16's XML1 ray) and both empty-query branches
  (0x51a359: bullet bursts and pauses, the other types' burst/strafe with the continuous-beam sweep). Schema 2
  tables are refused. (4) No scheduler change: the original's measured "query starvation" was a stale reservation
  planted by the measurement staging; `xml2_npc_query_scheduler_test` now executes XML1's and XMen2.exe's admission
  code side by side. The record: absent/`xml2` = revision 3 `{0,0,0,0}` (released OFF, nothing patched);
  `NpcPowerRule = xml1` = revision 11 `{1,1,0,0}`; with the weapon brain `{1,1,1,first four bytes of the table's
  SHA-256}` and the whole SHA-256 of the canonical rows as rule 16's detail (the agreement digest covers it; a 32-bit
  fold of the rows let two different tables agree). Revision 11 refuses 1.8.0-beta.6's revision 10 (and 3, 4, 5-9)
  both ways naming `16 NpcPowerRule`. A weapon brain that cannot install takes the power rule's sites back out too
  (all or nothing; online stays closed). The aiming's exponentials and trigonometry are deterministic
  (`det_math.hpp`, correctly rounded double operations only), not the CRT's, and every weapon-brain entry runs under a
  fixed x87 control word and MXCSR, restored on return. A failed withdrawal refuses startup.
- **Gameplay agreement: rule details.** A rule whose effective state does not fit four words registers the SHA-256 of
  its complete canonical data; details extend the canonical list only when present, so every OFF and released digest
  is unchanged (protocol 4, 108 bytes, no wire change).
- **XML1's speaker labels, opt-in** (`[Game] ClassicSpeakers = 1`; 1.8.0-beta.7): XMen2.exe takes a conversation
  line's name label, portrait and talk-animation entity from one `%TOKEN%` (the line parser 0x458820 -> 0x456d00
  writes the stats entry's display name and a newline in front of the text). X-Men Legends I keyed its 213 `%ALISON%`
  lines to Magma but labelled them "Alison" (default.xbe string 503), so the Legends Classic builder keeps `%MAGMA%`
  on the party Magma's ex-`%ALISON%` lines and writes `speakerLabel="Alison"` beside it. With the key the fix
  redirects the line parser's two calls of its common part (0x458da8, 0x458e7b; seven guarded sites, one
  transaction): after the game's own parse, a line with `speakerLabel` whose text starts with a token the game
  resolved gets the label in place of the display name (text and textb, re-interned through 0x4566b0); the speaker
  key stays, so the portrait and the talk animation are still Magma's. Anything else (no attribute, an unresolved
  token, a token not at the start, a second token) is left as parsed. Absent, `0` or `off` make no writes; without
  the fix the attribute is never read and the line reads "Magma" as before. Display only: no gameplay-agreement rule,
  no save change.
- **Enemy AI fidelity: gameplay rule 16 revision 10** (`[Game] NpcPowerRule = xml1`, `[Game] NpcSemiAuto = xml1`;
  1.8.0-beta.6). One new rule-16 revision covers two restorations ([docs](docs/npc-power-rule.md),
  [weapon brain](docs/npc-semi-auto.md)). (1) With `NpcPowerRule = xml1`, a hit reaction now spends an enemy's combo
  step, power re-check and attack decision as in X-Men Legends (the reaction sites 0x435b6e, 0x518770, 0x518fd0);
  measured on the single-Brotherhood moving-hero bench, enemy offensive entries fall from 48.7 to 25.2 per minute
  (original 27.7, gate 23.6-31.9) with the still-hero control unchanged. (2) `NpcSemiAuto = xml1` with
  `[NpcSemiAuto] Count / W1..Wn` rows (`actor|recoil|range|burst|leader`, binary32 bits, written by Legends Classic from
  the owner's XML1 data) gives those NPCs the original weapon brain: the recoil deadline, the held fire, the
  grenade-press and input-lock bridges (0x51a359, 0x519fe7, 0x512f0d) - the HAARP officer fires one aimed shot per
  ~0.9 s, never bursts or melees at range, closes from 520 to ~340 and holds his grenade at full energy, within +/-15 %
  of the original's shot count in all six range/energy cells at 60 fps. It needs `NpcPowerRule = xml1` (otherwise it
  is refused and host/join close). The record: absent/`xml2` = revision 3 `{0,0,0,0}` (released OFF, nothing
  patched); `NpcPowerRule = xml1` = revision 10 `{1,1,0,0}`; with the weapon brain `{1,1,1,metadata fingerprint}`.
  A revision-10 PC refuses the released revisions 3 and 4 and the unreleased candidates 5-9 both ways, and a PC with
  the weapon brain refuses one without it, each naming `16 NpcPowerRule`.
- **The classic HUD no longer frames the small map** (`[Game] ClassicHud = xml1`; 1.8.0-beta.6): beta 5 drew a bevel
  around XMen2.exe's small map and moved the map's safe-area inset (two operands at 0x5a023f and 0x5a0717) to fit
  it; hand-testing found the frame oversized and the original game's small map had none. The adapter now leaves the
  map alone: the map draw callback (0x69db98) is not hooked, the two inset operands keep their stock bytes, and the
  twelve read-only guards that existed only for the frame are gone (the transaction is seven pointer writes and one
  call redirect). The map, its fog and markers sit where XML2 puts them, and every other classic HUD piece is
  unchanged. Off (`ClassicHud` absent) is byte-identical as before. A builder whose atlas still has the frame is
  harmless (sprite 38 is never drawn); Legends Classic 0.4.0-beta.6 (content 26 / LC35) stops writing it.
- **Online at 60 fps** (`[Online] TickRate = 60`; 1.8.0-beta.3): the stock game runs an online session at 30
  frames a second (`CClient::frame` writes a 1/30 s minimum frame while the online flag is set, 1/60 s otherwise).
  `60` replaces the online 1/30 with 1/60 (guarded, read back; `[Display] FrameRate`'s patch of the offline 1/60 in
  the same bytes accepts it). Lockstep runs one simulation step per frame with the slowest player's measured frame
  time as the step, so the game keeps its speed; upload per player roughly doubles (~11 KB/s). The rate is
  **gameplay-agreement rule 29 (TickRate)**, `{rate in effect, 0, 0, 0}`, registered whenever the key is there: a 60
  and a 30 PC refuse each other naming `29 TickRate`; a 60 request that cannot patch the game advertises 30. The
  lost-player records a survivor re-sends keep their length in seconds (64 at 30, 128 at 60). Absent = the game's
  30 and the released record; Legends Classic 0.3.0-beta.3 builds write `60` ([docs](docs/online-tick-rate.md)).
- **Online frames wait instead of spinning** (`[Online] TickWait`, 1.8.0-beta.4): beta 3 at `TickRate = 60` fell to
  ~15-20 fps on a busy PC (other games running). The game's online minimum-frame wait is a busy-spin, so every online
  game is a 100 % CPU thread; on a saturated PC the scheduler takes it off the CPU for whole quanta, and a 1/60 s
  frame has no slack to absorb that (a 1/30 s frame's spin did). With `TickRate` set the online minimum is now 0.0
  and the fix paces online frames at the tick rate from its Present hook: a high-resolution timer, a 0.6 ms spin,
  the wait at above-normal thread priority. Measured with the host on 4 CPUs shared with 12 busy processes:
  spin 20.9 fps, timer at normal priority 18.1, timer above normal 55.4 (p90 17.1 ms), the game using ~0.2 of a CPU
  instead of a whole one. `TickWait = spin` keeps the beta 3 behaviour; rule 29 is unchanged
  ([docs](docs/online-tick-rate.md#the-15-fps-cliff-tickwait)).
- **The classic look, opt-in** (`[Game] ClassicHud = xml1`, `[Game] ClassicCodex = 1`; 1.8.0-beta.5): two
  presentation-only adapters for Legends Classic 0.4.0-beta.5's builder-generated assets. `ClassicHud = xml1` draws
  the HUD from the build's composite atlas and panel/emblem models (one guarded transaction: eight four-byte pointer
  writes and one five-byte call redirect, each checked against the retail bytes; scoped hero fills and target frame,
  only the three HUD sprite batches remapped) and gives the small map a larger frame with a matching safe-area inset,
  so fog and edge markers stay inside it; it refuses (stock HUD, logged reason) without the build's `lc-hud.txt`
  marker `LC-HUD-4`, on an unexpected image base or on any retail-byte mismatch. `ClassicCodex = 1` binds the Codex's
  Details action to the list entry under the cursor and makes Back from a biography return to that list (List Back
  keeps the native Close); both hooks commit in one transaction with verified rollback. Absent, `0` or `off` make no
  writes (the retired `ClassicHudPilot` key is ignored). Display only: no gameplay-agreement rule, no save change
  ([docs](docs/classic-hud-pilot.md)). Refused-save dialogs now show the save's file name instead of its full path
  (a long absolute path ran past the dialog box); the log and the refusal record keep the full path.
- **Bigger model pools, opt-in** (`[Engine] ModelPools = x4`; SPEC-runtime-limits 2.4): every model the game shows
  takes a handle and components from fixed pools (handles 692, IGB geometry 400, shader effects 432, animated actors
  32, particle clouds 40, ...). When one is full the model is silently not made - an effect, a prop or a HUD head goes
  missing (the beta 1 crash was the full IGB pool). `x4` moves the whole model aggregate (0x7bf6d0) into a block of
  the fix's own and raises handles and transforms to 4096, attributes 1024, shader effects 2048, IGB 2048, actors
  128, procedural geometry 1024 and particle clouds 128 (`x2`: half, clouds 80). Each particle cloud takes one of
  the particle systems of a second static manager (0x7dad78, 40 retail; with more clouds than systems the game reads
  through a NULL system at 0x578df3), so that manager moves and grows with the clouds; 753 fields of the model code are patched and
  36 imm8 sites and five bitmap clears move into caves, and the procedural-geometry destructor gets its own copy of
  the `bitset<220>::findNext` it shares with the FX templates (beta 3: under beta 2 that destructor read the helper's
  end value 220 as a live slot, and the attributes and procedural-geometry destructors cleared only the retail part of
  their bitmaps, so a zone change or exit with those pools in use could destroy items twice), all or nothing (on any byte that isn't the retail build's the game keeps its own
  pools and the log says so). A full pool refuses spawns as well as models, so online a raise is **gameplay-agreement
  rule 28 (ModelPools)**, `{installed, handles, IGB, actors}`, registered only when requested: peers must share the
  pools, and a 1.8.0-beta.1 peer and a beta.2 peer with `x4` refuse each other at admission (the beta.2 PC names
  `28 ModelPools`). Off by default in the DLL; Legends Classic 0.3.0-beta.2 builds write `x4`. The test pipe's status
  shows the nine live counts.
- **Quit no longer races the controller-enumeration workers** (retail `0x618c90`, manager `0xA6A7E4`): several
  enumeration workers can be alive after returning to the main menu, and each one cleared the shared manager and
  published "stopped" (`0xA6A7E1`) on exit while a peer was still enumerating - the peer then reloaded a null manager
  for its count call and faulted at `0x627375` (read of `0xC`), intermittently on Quit. Entry/exit accounting now lets
  only the last active worker clear the manager and acknowledge shutdown. Eleven short retail signatures guard the two
  sites; any mismatch logs its address and keeps the stock behaviour. See `docs/input-enumeration-shutdown.md`.
- **The team-combo popup no longer crashes the game** (owner report on 1.8.0-beta.1, Maintenance Garage hosting alone;
  `XMen2.exe+0x1a6848`): when two heroes land a combo the game shows both heroes' heads, and makes each head model
  from fixed model pools. With a pool full (the dump: 400 of 400 `CModelIGB`), the second head is never made, but
  the game still showed the popup and its draw called through the missing head. The popup is now hidden in that
  case, the way its own timeout hides it; the combo's XP and count are unchanged. Display only (no gameplay-agreement
  rule of its own). `[Game] ComboPopupGuard=0` keeps the game's own behaviour.
- **A frozen or silent player no longer splits the session** (0.3.0e, rule 25 revision 4): with three or more
  players, a player whose game froze (or whose network went silent) was removed by the host's own 10 s timeout
  without the survivors' agreed cut frame; the other joiner then waited for the host's part of the agreement, its
  own timeout ran out and it left ("You have lost connection to the host"). Now every way a player is lost - its
  connection closing, the host's timeout or lag-report removal, another player reporting it lost - takes the same
  path: every remaining PC exchanges the lost player's last inputs and removes it at one agreed frame, and the
  session carries on. The frozen player, when its game resumes, gets the normal "lost connection" message.
  0.3.0d peers are refused by name (`differs: 25`).
- **Hero switches survive level changes online** (0.3.0d, rule 26 revision 6): the solo-segment hand-back ran at
  every level change and gave each player the hero they had at the previous zone's start, undoing d-pad switches
  (and handing a dead hero back, which the game then passed on again). It now runs only inside a solo story segment
  and in the zone where it ends, with the heroes the players held when the segment started. 0.3.0c peers are refused
  by name (`differs: 26`) instead of desynchronizing.
- **A slow player is no longer dropped online** (rule 26 revision 6): the game's slow-player guard dropped whoever
  reported frames over 50 ms for 1,700 frames in a session - a joiner in the owner's three-PC test, the host itself
  on a busy PC (which ended the session for everybody). The warnings stay; the drop is gone. Switching onto a hero
  another player controls stays refused, as in the original game; switching to an AI hero works online.
- **Four-player Start Game no longer crashes:** each participant keeps its own mesh peer; the start race no longer rebinds two players to the same peer.

- **The release is built with online play:** the release workflow now builds GameNetworkingSockets 1.6.1 (pinned commit, the
  project CA and the cert-enforcement patch in `packaging/gns/`; protobuf and abseil from a pinned vcpkg) with
  `scripts/build-gns.ps1`, links it statically into `dinput.dll` and fails unless the DLL it publishes has GNS, protobuf and
  the CA pin in it and imports Windows system DLLs only (`scripts/check_release_dll.py`, run on the built DLL and on the one in
  the finished zip). Before, CI built without GNS: the same version number, no online rooms or drop-in. Nothing ships next to
  `dinput.dll`; the release zip now also carries `THIRD-PARTY-NOTICES.txt` (the licences of what is linked in).

- **Public game browser and join codes match builds by game version:** a room now carries the build's
  `[Online] GameVersion` (for example `LC33`) as its version token, exactly what the launcher's Browse games list
  compares with its own build; before, the token also held part of the content fingerprint and every listed game
  of the same build showed "Different game version" with Join off. A joiner of another build is still refused
  before it connects (`version_mismatch`).
- Rule 27 OnlineConversations (1.7.0) is the only conversation fix online; Legends Classic 0.3.0 writes it.
- **Online hardening** for room startup, message handling and logging.
- `online-status.json` (contract v1.5): a room that exists but takes no players right now (the broker's
  `room_not_joinable`) is the new error id `room_closed`; `room_full` now means only that all four places are taken.

- Online rooms for the launcher (contract v1.5): `[Online] Host` / `JoinCode` are read once at start and removed from `xml2-fix.ini`; `online-status.json` next to the game reports connecting / room / joined / in_game / ended / error (ids broker_unreachable, broker_rejected, code_not_found, code_expired, room_full, room_closed, version_mismatch, test_mode_on, tls_pin_mismatch, internal), written atomically; a room carries a version token so another build's joiner is refused before connecting; with a join code Join Game lists the room's game only - master server entries are left out until the session runs (no Connect by IP needed). [details](docs/online-status-file.md)
- Online rooms keep the broker's credentials between starts: the 24 h certificate (reused with more than an hour left) and the 6 h TURN relay credentials (reused with more than 30 minutes left, for the same broker and TURN secret; sealed with DPAPI for the Windows user because they hold the relay password, never logged). A connection that fails while cached TURN credentials are in use gets new ones from the broker once. `xml2-fix.log` says whether each came from the broker or this install's cache. A member's keepalive that overtakes its hello at the start is ignored instead of counted as a refused tunnel message, and refused tunnel messages are logged with the reason.

- **Solo story segments online** (`[Online] JoinInProgress=1`, rule 26 revision 6): a player without a hero in a
  segment stays connected and watches, with the message "Solo story segment - you rejoin when it ends"; when the
  party widens each player gets back the hero they had before (docs/join-in-progress.md).
- **Guest loss and freezes:** with three or more players, survivors exchange the lost player's last inputs
  and remove it at one agreed frame (rule 25 revision 4, framing 3). This includes a closed connection,
  the native host timeout and lag-report removal. A guest can join again by code at the next level boundary.
  A host drop or freeze still ends the session for joiners with "lost connection to the host"; no host migration.

- **Host alone and drop-in** (`[Online] JoinInProgress=1`): the host can Start Game with nobody else in the room and
  play; players are admitted mid-level and brought in at the next level change (rule-26 flow). When the last player
  leaves or vanishes the host keeps hosting (the native rule ended a session of two), so a replacement can drop in.
  The native slow-player counters start again with every restored session (a host that had played alone dropped
  itself right after the first bring-in).

## 1.7.1 - 2026-10-20

XML2 Fix 1.7.1 is the engine half of Legends Classic 0.2.5. Both co-op PCs need
the same version (gameplay rule 18 is now revision 3).

- **Bedroom item pictures draw in the first game's window:** with
  `[Game] PersonalItemPicture=window`, the personal-item menu draws its picture as
  a 256x128 window over the menu's panels (picture frame, text panel, Back bar),
  as the first game does, instead of stretching it over the whole screen. The
  fullscreen picture could only show an enlarged copy of itself behind the real
  one. Only PERSONAL_MENU's two position/size getters change (guarded, one log
  line on refusal); absent or `stock` keeps the fullscreen picture. Legends
  Classic enables it after a rebuild.
- **Heroes are re-stunned per hit, as in the first game** (issue #101): with
  `[Game] StunRule=xml1` (gameplay rule 18, now revision 3), every stun hit on
  a party hero requests the stun reaction. The original does this; X-Men
  Legends II only stunned on the hit that started its two-second stun effect,
  so hits during a stun, or right after getting up, were ignored. Both co-op
  PCs need the same revision. ([details](docs/stun-rule.md))

## 1.7.0 - 2026-10-20

XML2 Fix 1.7.0 is the engine half of Legends Classic 0.2.4.

- **Enemy resistance text is back:** with `[Game] ResistanceDisplay=xml1`, the
  target health bar shows the first game's physical, energy and mental labels
  and colors. Legends Classic enables this after a rebuild.
- This changes the display only. Combat resistances, saves and co-op rules stay
  the same. With the setting off or absent, the 1.6.0 display behavior remains.
  Ordinary X-Men Legends II installs keep the setting off.
- **Co-op no longer drops during conversations:** with
  `[Game] OnlineConversations=lockstep`, a conversation line that goes on by
  itself does so after the same game time on every PC, instead of when this
  PC's voice ends. The voice ending a moment apart on two PCs had put the games
  out of step and ended the session (for example during the mansion tour).
  Online, a chosen reply's voice is cut as the game has it; single-player is
  unchanged. Both PCs need XML2 Fix 1.7.0 and matching settings
  (gameplay agreement rule 27, revision 1);
  Legends Classic enables it after a rebuild.

## 1.6.0 - 2026-10-18

The engine half of Legends Classic 0.2.3: the first game's hazards, enemy combat timing and Danger Room, the Sentinel
flashback's effects, and a co-op that loads a saved campaign in seconds. For X-Men Legends II as it ships only the
network-message check and the plugin-dialog fix apply; everything else is off until a mod's `xml2-fix.ini` asks for it
(the port's builder sets it all).

**For everyone**

- **Malformed network messages are checked and dropped** (`[Online] ReceiveValidation`, on by default): the game
  checks the size and shape of every message it receives, per message type, before its handler runs, and drops one that
  doesn't fit instead of acting on it (a line in `xml2-fix.log`, rate-limited). Nothing changes on the wire, so it
  works with players who don't have it. Ordinary messages that carry extra bytes, and the game's own empty movie
  completion message, are not dropped.
- **No "Library appverifUI.dll could not be loaded" dialogs** when the game runs from `C:\`: the engine's plugin
  helper scanned the root of the current drive for plugins when no plugin folder is set, and loaded Windows' own
  leftover DLLs there. That one empty search now finds nothing; the game's real plugin folders are untouched.
- `[Debug] LoadLibraryTrace=1` lists the DLLs the game and its engine libraries load and the plugin-folder searches,
  for a bug report (off by default).

**New for mods (the port's builder sets all of these)**

- **Co-op *Load Saved Campaign* works and takes seconds.** The host's saved-campaign transfer could stall or end the
  session when the joiner's shared network message pools filled up. The receiving side now keeps room in all four
  pools, defers a datagram it can't store instead of acknowledging it, and ends a session that stays blocked for 30
  seconds with one clear log line. With `[Online] FastTransfer=1` (and `[Limits] EntityCapacity=1`) the 2 MiB campaign
  image is sent as data and zero-run records with up to 32 messages in flight and a larger rate budget: two real saves
  took about 3 and 1 seconds of transfer (15 and 5 seconds from click to playing) on a LAN, against about 160 seconds
  before. Both PCs must use it (rule **24**). ([details](docs/online-campaign-transfer.md)).
- **Connect by IP and the LAN search work for a mod's builds:** `[Online] GameVersion` now also reaches the answer a
  host gives to LAN searches and *Connect by IP*, so two builds with the same version list and admit each other that
  way (only the OpenSpy list worked before).
- **Hazards that hurt again:** `[Game] BoxHarmOrigin=xml1` (rule **21**) gives a boxed hazard the first game's
  knockback direction, and `[Game] HarmSingleChain=single` (rule **23**) keeps one repeat chain per touch fire, so a fire
  hits about once per 0.4 seconds and stops when you step out (before, re-entering one stacked up repeat events).
  ([box origin](docs/box-harm-origin.md), [single chain](docs/harm-single-chain.md)).
- **Crates that land on the floor:** `[Game] ObjectWorldImpact=xml1` (rule **17**): a landing with no physical target
  does no damage, as in the first game. ([details](docs/object-world-impact.md)).
- **Enemy AI revision 4 and party stun** (issue #101, progress): `[Game] NpcPowerRule=xml1` restores each enemy
  button's combo delay and clamps stale deadlines after a clock rewind or AI-slot reuse; `[Game] StunRule=xml1` (rule
  **18**, revision 2) gives party heroes the original stun playback speed and input recovery and discards a refused
  or interrupted stun. ([enemy AI](docs/npc-power-rule.md), [stun](docs/stun-rule.md)).
- **Danger Room:** `[Game] DangerRoomParty=xml1` (rule **19**) is the first game's four-hero fallback for a course
  that starts without a full party, and `[Game] DangerRoomHealthBars=xml1` (rule **22**) shows the health bar of a
  protected training robot; the protection and the course limits stay. ([details](docs/danger-room-courses.md)).
- **Sentinel flashback:** `[Game] Xml1LockOnEffects` restores the eye-scan, surface-hit and acquired-target effects and
  `[Game] Xml1LockOnAim=1` (rule **20**) aims by the animated head, as in the first game.
  ([details](docs/lockon-effects.md)).
- **Saved zones keep what they had:** an LC zone record can keep a verified identical prefix of its residents across a
  map change that only adds to the end, and the new ones start fresh; a missing, stale or ambiguous record is
  discarded as before. The save format is unchanged. ([details](docs/native-fresh-tail.md)).
- **Optional `[TestMode]`:** Forge access, F8 enemy clear, F9 party protection and F10 return to the nearest safe start
  for testers; both switches are off by default and refuse online hosting and joining.
  ([details](docs/test-mode.md)).

**Online**

- Rules **17** to **24** register only when a mod requests them; matching installs play together, and a missing,
  failed or different rule refuses the join with its name on both PCs.
- Gameplay agreement protocol 4 (the same 108-byte record): **1.5 and older peers are refused at join as another
  build, so both players need 1.6.0.**

<details><summary>Technical details</summary>

- `[Online] ReceiveValidation`: a guarded hook on the game's common message dispatch checks each message (from the
  datagram parser, the ordered drain and fragment reassembly, including reliable wrappers and padded records) against a
  per-type table of minimum and maximum sizes. Fixed-layout types whose handler ignores extra bytes accept any body
  from their minimum up to the game's 1285-byte cap; strict maxima stay only where a handler copies or indexes by
  length, and two types stay refused. The empty dynamically registered movie-completion notification is accepted. A
  drop line names the inner type of a wrapped message, its size, the violated bound and the first 16 bytes. A hook
  that can't verify its bytes leaves the game's own dispatch and says so in the log.
- The plugin-scan guard answers the plugin helper's one empty-repository search (`\*.*`, the root of the current
  drive) with "file not found" and logs one line; a pattern with a directory part is left alone. It is installed after
  the mod loader. `XML2FIX_PLUGIN_SCAN_GUARD=0` (environment) turns it off for development only.
- `[Online] GameVersion` also rewrites the game-info constructor's own version store (19 guarded bytes, in the same
  transaction, with readback); with the key unset the stock bytes stay. Lobby metadata only: no gameplay rule.
- Saved-campaign transfer: the receiver reserves room in the headers, large bodies, small bodies and queue nodes before
  it stores a datagram; an outgoing reply or fragment it can't fit is refused before anything is copied. A transient
  native marker carries the fresh tail of a retained zone record through the transfer; the durable LC format is
  unchanged.
- The authored-startup adapter `HarmLoopStartup` stays a developer option and still blocks online play.
  Disabled optional rules 17 to 22 are named in local refusal messages although they are absent from the OFF record.

</details>

## 1.5.0 - 2026-10-13

The engine half of Legends Classic 0.2.2: more of X-Men Legends I's own rules, each behind its own key. For X-Men
Legends II as it ships only the display, pad and cutscene-mouse fixes apply; everything else is off until a mod's
`xml2-fix.ini` asks for it (the port's builder sets it all).

**For everyone**

- **Exclusive fullscreen** asks only for a mode the adapter can show (a resolution it doesn't list falls back to the
  desktop's mode and refresh rate), *8x* anti-aliasing really asks for 8 samples, every mode lowers a request to the
  highest level the adapter supports, and a failed start no longer wipes the game's display settings in the fix's own
  modes.
- **Pads:** *Defaults 1/2/3* in the controls menu keep the pad layout; they used to erase player 1's pad bindings
  (#9).
- **The mouse in cutscenes** (`[Input] CutsceneMouse`, on by default) no longer outlines characters or clicks to move.

**New for mods (the port's builder sets all of these)**

- `[Game] NpcPowerRule=xml1`: **Enemy AI** (issue #101): enemies pay energy for powers, decide every 2 seconds, need at least half their energy, and use the first game's melee combos. Visibility uses the first game's geometry and query scheduling. Heroes still spend more time stunned than in the original; that part of #101 remains open. ([details](docs/npc-power-rule.md)).

- `[Game] RankCosts`, `LevelSkillPoints=xml1`, `LevelStatPoints=xml1`: X-Men Legends I's 2-point skill ranks and its
  2 skill / 8 stat points on every fifth level ([details](docs/rank-costs.md)). A campaign saved before these keys
  were installed gets the fifth-level points its heroes missed, once, on its first load.
- `[Game] Xml1Multipart`: breakable parts - Juggernaut's helmet, the Sentinels' arms ([details](docs/multipart.md)).
- `[Game] X1Leader`: the first game's leaders (their drop rule; the pain rule for their non-cold projectiles).
- `[Game] SoloMode`: the first game's solo mode at its gaps and bridges ([details](docs/solo-mode.md)).
- `[Game] DeathRule=xml1`: an entity dies on the hit that leaves it below 1 health
  ([details](docs/death-threshold.md)).
- `[Game] PackAmount=xml1`: health and energy packs restore a quarter, as in X-Men Legends I
  ([details](docs/pack-amount.md)).
- `[Game] PlacementCrush=xml1` and `DoubleHeal=0`: no crushes (and their hit sounds) while objects settle at a zone's
  load, no double heals ([details](docs/placement-crush.md)).
- `[Game] DangerRoomGate`, `RevivalCost=xml1` and the Xtraction services: Load, Danger Room, Healer and Forge at the
  first game's points, the main menu's Danger Room locked until Phoenix has joined, the first game's revival price
  ([details](docs/xtraction-shop.md)).
- `[Game] Xml1Turrets`: the first game's scan turrets scan, track and fire again
  ([details](docs/xml1-controllers.md)).
- `[Game] DeathSwitchClear`: when the controlled hero dies, the hero handed its controller stands still instead of
  finishing the walk its AI had started ([details](docs/death-switch.md)).
- `[Limits] ScriptFunctions`: the game's 320 script function names raised to 1024
  ([details](docs/script-functions.md)).

**Online**

- Each of these keys is a rule of the shared gameplay agreement (IDs 5-16). The agreement record is now a fixed
  108-byte digest (protocol revision 3), so a join no longer crashes the host once enough rules are on, and a refusal
  names the setting that differs on both PCs.
- A 1.4.0 player and a 1.5.0 player refuse each other at join, with the reason on both PCs: both need 1.5.0.

**Going back to 1.4.0**

- A save written by 1.5.0 is protected from 1.4.0: before replacing it, the older fix keeps it as `.lcsave.newer`,
  and when a different `.newer` copy is already there (after going back and forth) it refuses to save over that file,
  the profile included. To go back, put back the copy of your save folder made before the update.

<details><summary>Technical details</summary>

- `[Game] LevelSkillPoints` / `LevelStatPoints`: the catch-up grants each hero 1 skill and 4 stat points per multiple
  of 5 in (1, its processed level] and is marked by optional-feature bits 1 / 2 of the LC header, so it never happens
  twice; no format change. When the build ships `Data\lcsave\autogrowth-legacy.txt` (its heroes' autospend classes
  changed), the saved class bonus is re-based once the same way (old classes' growth out, current classes' in, comic
  rewards kept; header bit 3).
- `[Game] PlacementCrush=xml1`: an object being settled on the floor no longer crushes another, as in X-Men Legends I,
  which never settles anything. XMen2.exe settles every object at a zone's load and each object spawned in play 0.5 s
  after it appears; furniture the first game authored overlapping (the X-Mansion dormitories' desk chair under its
  desk) no longer plays two hit sounds from an empty room on entry. Other touches in play keep the game's rule.
- `[Game] DoubleHeal=0`: health and energy packs never heal double, as in X-Men Legends I, so they no longer sometimes
  play XML2's darker "double" sound; the stats screen's Body and Focus helps drop the double health / energy chance
  lines while it is in.
- `[Game] DeathRule=xml1`: X-Men Legends I's death threshold - an entity dies on the hit that leaves it below 1
  health, instead of only at 0, so an enemy no longer fights on at a fraction of a point with its death script and
  drops waiting. Eight comparisons of the damage functions move to the first game's line, and a killing hit leaves
  health at 0 as the first game's whole numbers always did, so XP and death drops follow.
- `[Game] Xml1Turrets` / `[Xml1Turrets]`: X-Men Legends I's scan turrets (the HAARP exterior missile tanks, the
  Arbiter / Weapon X tripod guns and the Hive tanks' flame turrets) with the first game's timing, aim spread, damage
  and effects; online peers must agree on every placement (rule 4).
- `[Limits] ScriptFunctions`: the game's 320 script function names, full with the fix's own functions, raised to
  1024 by default while `[Game] ForcedTeams` registers them (321 to 4096; 320 keeps the game's limit); every stock
  name keeps its place. `[Test] ScriptFunctionProbes` for testing it.
- Display: the exclusive-fullscreen mode, format, refresh and multisample choice is checked against the adapter's
  list; the 8x boot-switch index reads a private table; the two registry-wipe sites run only outside the fix's own
  modes; the guards are read-only pre-checks.
- Pads (#9): the *Defaults 1/2/3* preset tables get the pad layout; the game's cross-player rule (one key never drives
  two players) is kept.
- `[Input] CutsceneMouse`: the mouse check also tests the HUD's cinematic flag, so cutscenes that start without
  locking controls (X-Men Legends I's mission briefings) stop the mouse too.
- Online: the agreement's protocol revision 3 sends a header, the SHA-256 of the canonical rule list and a 64-byte
  rule summary (up to 256 rules, the same 108 bytes at any count); the largest queued body is 188 bytes, under the
  game's 300-byte message limit. With the full rule list on the wire (protocol 2) the join messages passed that
  limit, the game split them, and the host's large-message path dereferenced null; a fragment for a stream that was
  never opened is now dropped instead (an eight-byte guard in the startup transaction). Protocol-2 peers (1.4.0 and
  1.5 pre-releases) are refused as another build, both ways ([details](docs/gameplay-agreement.md)).
- `[Test] MultipartTrace`, `TurretTrace`, `SoloTrace`: diagnostic log lines for those features.

</details>

## 1.4.0 - 2026-10-12

The engine half of Legends Classic 0.2.0. For X-Men Legends II as it ships only the mouse fix applies; everything
else is off until a mod's `xml2-fix.ini` asks for it (the port's builder sets it all).

- The mouse reaches every menu row in a window Windows resized for display scaling, and a windowed game keeps its
  size when it changes monitors.
- `[Save] Format=lc`: the port's own save format; old saves converted once into `SaveLC`, the old folder untouched,
  `.bak` and `.newer` copies, refused saves keep the file on disk.
- `[Balance]`: X-Men Legends I's combat numbers, including whole-number damage rolls and melee-weapon bonuses.
- `[Limits] EntityCapacity=1`: 768 generic / 1024 World entities instead of 224 / 256, so crowded rooms keep every
  object; one startup warning, with advice that fits the cause, when it falls back to stock.
- `[Limits] MoveTriggers`: long attacks keep every sound and muzzle flash.
- `[Game] Xml1MasterMold` / `Xml1MoldStage`: X-Men Legends I's Master Mold fight, its stage kept in saves.
- `[Game] ExplosiveFirstHit`, `ClassicMainBackdrop`, `ClassicMainPanelBounds`, `ConfirmationRelease`,
  `XtractionShop`, `PersonalItemText` (on by default); late-act trivia kept; no crash on Quit with the classic backdrop.
- Online: games with these keys exchange their installed gameplay rules at join and refuse a mismatch, with the reason
  on both PCs; saving in co-op no longer stalls the game long enough to drop the connection.

### Details

- Restore opt-in XML1 Master Mold shield/core combat, original attack deadlines,
  platform actions, companion behavior and core tethers through native services.
  The builder supplies saved-stage metadata; disabled/refused installation retains
  the native encounter fallback; see `docs/xml1-controllers.md`.
- Register Master Mold (rule 3: installed, saved-stage bit range and a hash of the flag
  name) and the absent turret controller (rule 4, off) in the shared gameplay-capability record. Online co-op is lockstep, so every peer runs the
  controller; sessions use a peer-invariant hero instead of the local `_ACTIVE_HERO_`.
  Peers with different controller state are refused. Startup refusal names an
  unverified XML1 hook rollback instead of the network exclusion hook.
- Master Mold works with `[Limits] EntityCapacity=1`: its action-entity queries accept the installed query
  owner's entries, test busy queries by the owner's null result and bound results by the installed World.
- Added `[Limits] EntityCapacity` (port default 1, needs `[Save] Format=lc`): the entity domain grows from
  32 actor / 224 generic / 256 World handles to 32 / 768 / 1024 with contiguous ten-bit indices, so props,
  projectiles and triggers past the 224th are no longer refused without a word (docs/entity-capacity.md,
  docs/entity-capacity-phase-a-plan.md). Prepared owners (allocator backing, World, grid, query manager,
  physics frames, native save manager), the reviewed ledger of operand edits and relocations, the native
  save layout E10A2, generation retirement, refusal and reset-time telemetry, and the test pipe's
  `entity status` / `entity retire <slot>`.
- A requested `[Limits] EntityCapacity=1` that falls back to stock (a capacity-only mismatch) now tells the player
  once at startup - what failed and what it means (stock limits, raised saves not loadable, online closed) -
  instead of only logging it; the game then starts as before. The advice follows the cause: a missing
  `[Save] Format=lc` (the ini line, or `EntityCapacity=0`), an executable not at 0x400000 or with changed entity code
  (the unmodified retail `XMen2.exe`), owners already built (other loaders), or an internal failure (report it).
- The optional host refusal receiver and lobby notice join the startup transaction only when their own guards read
  back as retail and every byte they write lies inside those guards; the agreement test covers a receiver guard
  mismatch as well as a presentation one.
- One startup transaction (`patch_transaction`) now owns every code write of the shared gameplay
  agreement, the LC save seam, `[Limits] MoveTriggers` and the entity-capacity install: expectations are
  unioned and checked once, pages acquired once, writes verified by readback and rolled back on any
  failure; a rollback that cannot be certified refuses startup. The agreement's prepare/finalize became
  contributions plus a verified commit; the GCAP protocol advances to revision 2 and the entity layout rule
  to revision 3 for raised peers, so release-1.4 peers are refused at admission.
- LC saves carry required-feature bit 0 under the expanded layout; a retail-layout build refuses them with
  the reason, and a legacy save's twelve registered typed entity-id zone variables are migrated at the
  LC-to-native boundary when the build's typed registry verifies against the installed scripts.
- Both lcsave implementations take an explicit native layout descriptor (retail or E10A2) for every stream,
  record and transfer bound; outputs for the retail layout are byte-identical to before.
- Fix a crash on Quit with `ClassicMainBackdrop=1`: the Cerebro room's asset directory is no longer
  retained past the end of the main loop. The pin kept the room's environment-map shaders alive through
  igReleaseAlchemy, so libIGSg's exit-time destructor touched freed memory (libIGSg +0x6955e).
- Fix the mouse in the borderless and windowed modes when the window's client area isn't the
  game's screen size: the engine maps a mouse point to its menus against its resolution
  (0x5f9eb0: `[0xa09ffc]` x `[0xa0a000]`) while Direct3D stretches the picture over the client
  area, so a window Windows shrank on a DPI change (1280x720 dragged from a 125% monitor to a 100%
  one became 1023x575) hovered rows ever further below the cursor and never reached the last ones
  (the main menu's Quit, in-game menus alike). Mouse messages and ScreenToClient now carry the
  screen pixel the point is shown at, and a windowed window keeps its client size across a DPI
  change (WM_GETDPISCALEDSIZE / WM_DPICHANGED): the size it has, kept apart from the render
  resolution, so a window resized from outside keeps that size too. Test pipe
  `mouse X Y [move|down|up|click]`.
- `[Game] ClassicMainPanelBounds`: a move or press inside the focused main-menu row stays that
  row's. MAIN_MENU falls through to the generic handler there, whose label test (0x5c6490) sits
  half the label's local y offset higher and refocused the row below: the focus flipped on every
  move in a row's lower third, and a press there could move it before the release.
- Tell the host why a co-op join was refused, on server-browser and Connect-by-IP joins alike. The lobby
  draws the reason (different gameplay settings, the fix not loaded on one PC, another build) in place of its
  advice text for a minute; in play the HUD shows it briefly. No popup, menu or input change: the host keeps
  its session and players. Refusals now carry their reason in the native incompatibility message (0x1d);
  the host receives it even from a player who has only queried its address. Corrects 0x18 to the querying
  PC's game-info message (live wire order): an incompatible host is not listed and the host is told.
  The notice clears only on a confirmed admission (the game's 0x1b follow-up), and the receiver installs
  independently of the presentation, so a joiner's refusal is logged even where it cannot be drawn.

- Keep the LC startup veto on instruction-cache flush failures and every patch protection
  restoration failure, including preflight cleanup. Stop further writes on a failed flush
  and attempt all remaining protection restores before refusing startup.

- Refuse executable startup when required LC save/trivia hooks cannot be installed. Guard,
  allocation and patch-protection failures now reach the DLL startup veto with an explicit
  notice and log; background XInput initialization starts only after the required hooks verify.

- Persist late-act trivia with the builder's TRIV bind rule 2. Guarded native getter/setter hooks,
  capture and restore share compact bitmap positions while LC question identities stay unchanged.
  Legacy native imports still read their historical sparse positions; rule-1 builds keep that layout.
  Move per-act question cursors out of the native array whose late-act entries overlap answer bits;
  cold loads resume at the first unanswered identity without changing the LC format.

- Added `[Limits] MoveTriggers` (20..127, port default 64): preserves full gun bursts and authored model events
  through guarded handle side storage; native move layout and save data remain unchanged.
  Scope timing packets to enclosing invocations, guard the fixed native packet producer, retire timing
  cache entries with the actor arena and fall back to native nineteen on cache exhaustion.
  Share fallback timers during same-actor reentry so a trigger-started replacement move retains its future
  deadlines instead of dispatching stale timers; enclosing packet ownership stays separately scoped.

- Prepare and cache verified conversation identity before play so LC saves do not
  scan the conversation tree on the game/network thread. Changes during play
  refuse saving until restart; the save format and digest bytes are unchanged.
- Compose co-op provenance caching with conversation era maps: pin effective per-zone
  packages through the native mod lookup, including later-mod ranking winners.
  Watch package/order dependencies and refuse unsupported topology or lookup contexts.
  Capture and restore no longer read/decode packages on a gameplay frame.

- Correct native agreement receiver roles: rejecting client host-info packets preserves the host session;
  follow-up/rejection packets clean the joining endpoint independently of pending-timer state.

- Generalize online agreement to a finalized record of explicit effective gameplay values and entity/token
  layout, with a registration API for capacity and controller installers. Native refusal now presents
  an actionable message, sends the remote incompatibility response and clears pending joins immediately.
  Shared code pages restore their original protections ([API](docs/gameplay-agreement.md)).

- Bind the effective installed explosive first-hit rule to native host-info/join agreement.
  Refuse differing or unverified rules before admission without changing the release identity.
  Legacy peers without the tagged agreement are refused; ordinary XML2 without the opt-in stays native.

- Add opt-in `[Game] ExplosiveFirstHit=1`: explosions use the original game's fresh attack ID for
  the first-contact victim and native area victims. A guarded two-byte branch patch leaves stock
  settings, activation, damage records and saves unchanged ([details](docs/explosive-first-hit.md)).

- Add opt-in `ClassicMainBackdrop` to restore the original Cerebro room's diffuse textures.
  Guard engine exports and pass layouts; scope to known opaque main-room shaders, preserving
  reflection and logo lighting. Other rooms and unsupported layouts keep retail rendering.
- Require actual ownership by the room asset's loaded directory before correcting a shader;
  matching labels elsewhere no longer qualify. Log bridge allocation/protection failures.
- Add opt-in `ClassicMainPanelBounds`: full-width main-menu hit regions with disjoint row
  seams, preserving centered labels and the separate Online/Quit entries.

- Fix legacy imports whose equipped items were written in a different era from the campaign's talents.
  Translate hero and restore-bank equipment with the detected item era, including semantic dumps.
- Key talent-era evidence by actual stats index so restore-bank records belong to the correct hero.
  These corrections preserve the LC format and wire revision and match the Python reference.
- Keep the corpus loader/saver gate strict while recognizing documented legacy placement promotions.
  Expected ordinals come independently from unique names in the build's numbered placements;
  loader metadata is checked against build ordinals, names and placement indices. Synthetic negative
  controls reject wrong associations even when returned metadata agrees with the wrong saved states.
  All other semantic fields and retained payloads must still match.
