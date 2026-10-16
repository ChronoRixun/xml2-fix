# Changelog

## 1.6.0 - unreleased

- **Online saved campaigns:** under raised entity capacity, reserve receive space across all four shared transport pools and defer a full datagram before acknowledging it. Preflight outgoing replies/fragments before allocation; persistent pool pressure ends the connection safely instead of deadlocking or copying through null. This fixes the reproduced receive-pool stall without changing saves, protocol 4, or the stock send rate. Controlled burst reproduction and validation limits: [campaign transfer](docs/online-campaign-transfer.md).
- **Opening fire (#104 follow-up):** `BoxHarmOrigin=xml1` restores the XBE zero-origin argument for boxed harm. The native victim/attacker fallback replaces the PC box-centre direction and uses the native 0.01 dispatch delay. Damage amounts, bounds and authored repeat intervals remain unchanged. This addresses the opening fire's upward repeated-contact impulse; other boxed hazards need campaign testing. Requested-only gameplay rule **21**, revision 1.
- **Danger Room robot health bars (tester report):** `DangerRoomHealthBars=xml1` bypasses the protected training-target HUD suppression gate so ordinary attacks can show a decreasing gauge. Protection and course limits remain intact. The newly reachable native HUD path may refresh dirty stats and schedule health clamping; timing parity is unproved, so requested-only gameplay rule **22**, revision 1, covers this behavior.
- The builder enables both keys for XML1 builds and refuses an existing companion missing either key. Matching verified installs can host/join; absent keys omit their rules, invalid/failed requests block admission, and mismatches name the setting. Ordinary XML2 defaults stay off. Builder 0.2.3 / XML2 Fix 1.6.0, content 20 / catalogue 7 / LC29 and the immutable bind fingerprint are unchanged: this candidate adds runtime switches and documentation, no game-data changes.


**Online saved-campaign transfer fixed.** Transient native metadata carries fresh resident tails through co-op saved-campaign transfer; the durable LC save format is unchanged. Gameplay agreement protocol is now 4 (108-byte framing kept): 1.5 and older peers are refused at admission, as before.

Companion engine for Legends Classic 0.2.3. Optional gameplay changes remain off in an ordinary XML2 install.

- `DangerRoomParty=xml1`: original four-hero fallback for an incomplete non-solo course party, avoiding the absent PC hero/default actor. Gameplay rule **19**, revision 1.
- `Xml1LockOnEffects=beam,surface,target`: restores the three omitted lock-on visual paths; `Xml1LockOnAim=1` uses the animated head for natural acquisition. Aiming is gameplay rule **20**, revision 1; valid effects and the complete verified transaction are required.

- `ObjectWorldImpact=xml1`: zero ordinary landing-impact damage for every no-physical-target collision, as in XML1's coefficient-table entry zero. Physical-target coefficients and authored throw hits retain their native paths. Gameplay rule **17**, revision 1.
- `NpcPowerRule=xml1`: revision **4** restores per-button combo delays and clamps stale deadlines after clock rewind or AI-slot reuse. OFF keeps released revision 3 in the digest.
- `StunRule=xml1`: party heroes use original stun playback speed and input recovery, discard refused flags and clear interrupted reactions. Enemy reactions remain native. Gameplay rule **18**, revision 2. This is progress on #101, not complete combat parity.
- Disabled optional rules 17 through 22 are named in local refusal messages even though they are absent from the OFF record.
- Rules 17 through 22 register only when requested. Matching verified installs can play online; absent/failed/mismatching rules refuse with their names. The agreement protocol is 4 / 108 bytes (3 and older refused).
- LC zone records can retain a verified identical resident prefix across append-only map changes, with the new tail fresh. Runtime readers check opened-map hashes and prefix digests. Missing/stale or ambiguous attestations discard safely. The durable LC format is unchanged; a transient native marker carries the fresh tail through online transfer, see `docs/native-fresh-tail.md`.
- Optional `[TestMode]`: Forge access and deferred F8 enemy clear, F9 party protection and F10 nearest safe authored-start return. Both switches default off. Either switch refuses online hosting/joining. Tools work without the automation pipe. F8 conservatively protects actors already referenced by scripts; future references and every scripted boss are not certified.

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
