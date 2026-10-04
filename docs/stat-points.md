# Spendable attribute point grants

Planned for XML2 Fix 1.3.2. `addStatPoints(actor, count)` grants 1..20 unspent attribute points through the native saved counter. Existing script function indices remain unchanged. No XP, level, skill points or fixed attribute is changed. Negative counters and signed overflow are refused.

Offline tests cover arguments, collector routing, registry capacity and native accessor behavior on a synthetic saved block. In-game pickup/spending/save-reload proof is required for this draft.
