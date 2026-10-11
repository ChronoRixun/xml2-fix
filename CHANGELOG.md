# Changelog

## Unreleased

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
