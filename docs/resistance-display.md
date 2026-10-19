# Original target resistance subtitles

`[Game] ResistanceDisplay=xml1` restores the original game's colored resistance
subtitles beneath the selected enemy's health gauge. Missing or `xml2` keeps the
PC behavior. Legends Classic requests the original display. No game art is added:
the native text renderer, localized strings and matching type colors already exist.

The original target update is at `default.xbe` `0x15ff60`. It checks active
`def_damage` affecters (18) in physical, energy, mental order, with a default attack
context for each type. `0x94a70` formats immunity at a nonpositive damage factor,
resistance below one, and nothing at one or above. Three matching scopes use
localized string 103. The `~NN` prefixes are text colors, not image identifiers.

The PC target update at `0x5a8920` queries `resist_*` (13..19) instead. The port's
converted talents deliberately use `def_damage_scope` (56) to preserve both damage
and reaction resistance. Adding native resistance affecters alongside them would
apply the reduction twice. The display therefore needs a local engine adapter.

Only the resistance-query loop at `0x5a8dec..0x5a8e14` changes. It reads current
scoped (56), unscoped (39), and converted gear resistance affecters. The existing
target validation, gauge, title, boost prefix, position and draw calls remain.
There is no actor cache, powerup application, damage hook or save change.

The adapter asks the native bounds aggregator `0x54ff80`, rather than `0x550410`:
the latter can draw a random value for a ranged affecter. Bounds are classified
only when the entire interval has the same display meaning; the additional queries
draw no random value. Every existing native formatter query still runs in its
original order with the same arguments, preserving its cache/RNG behavior. Native
extra damage types remain visible; with no original modifier, the entire native
subtitle is retained. A temporary affecter that disappears is absent on the next
HUD sample. The PC's energy and mental color/string indices are swapped relative
to the original; their matching colors are used without editing the global table.

Installation checks the retail image base, dependencies and every displaced byte.
The existing transaction verifies writes and rolls back failures; an uncertifiable
rollback refuses startup. Default-off, invalid-option and changed-image paths keep
the native HUD. This is display-only and adds no gameplay-agreement rule.

`[Test] ResistanceDisplayTrace=1` logs target changes and sampled factors for local
verification. Synthetic rules and production-installer tests cover scope order,
immunity, multiple scopes, removal, ranged values, the emitted native bridge and
guard refusal. Runtime comparisons must use matching original enemies and actual
movement/targeting input; a successful installer is not live display proof.
