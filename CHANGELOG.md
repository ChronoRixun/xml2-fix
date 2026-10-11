# Changelog

## Unreleased

- Preserve default-off XML1 combat experiments with an atomic install guard,
  native-AI Master Mold updates and spawn/load reconstruction. Experimental
  controller requests refuse online play pending shared gameplay agreement.
  Full encounter/turret acceptance remains open; see `docs/xml1-controllers.md`.
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
