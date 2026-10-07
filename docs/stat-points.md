# Spendable attribute point grants

New in XML2 Fix 1.3.2. `addStatPoints(actor, count)` grants 1..20 unspent attribute points through the native saved counter. Existing script function indices remain unchanged. No XP, level, skill points or fixed attribute is changed. Negative counters and signed overflow are refused.

Offline tests cover arguments, collector routing, registry capacity and native accessor behavior on a synthetic saved block. A controlled in-game comparison collected the original STAT pickup through native
touch handling with a two-hero party. The old activation raised Body with no unspent
point. The candidate gave the collector one remaining point, preserving Body, XP,
level, skill points and all other heroes' saved stat blocks.

A normal save was loaded through the game UI in a fresh process; the point and compared
blocks/counters persisted. Spending it on Focus in the native stats menu increased
Focus by one, reduced the remaining point to zero and left Body and the other hero
unchanged. The test never directly invoked addStatPoints or wrote character memory.

The Release DLL, stat-point rules and existing script-registration tests pass.
Independent review found no actionable issue. Raw screenshots, game logs and saves
remain local. Multiplayer and automatic spending were not tested; this draft is not
a release or full campaign acceptance.
