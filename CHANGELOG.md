# Changelog

## Unreleased

- `[Game] DeathRule=xml1`: X-Men Legends I's death threshold - an entity dies on the hit that leaves it below 1
  health, instead of only at 0, so an enemy no longer fights on at a fraction of a point with its death script and
  drops waiting. Eight comparisons of the damage functions move to the first game's line, and a killing hit leaves
  health at 0 as the first game's whole numbers always did, so XP and death drops follow (`docs/death-threshold.md`);
  online, gameplay rule 9.

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
