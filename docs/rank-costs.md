# Skill rank costs (`[Game] RankCosts`)

`[Game] RankCosts=1` makes a talent rank cost what its talent data says: the `cost` attribute of the rank's
`<level>`, 1 when there is none. X-Men Legends I prices the ranks that open a new form of a power (an Xtreme power's
rank, the boost's ranks 1/4/7, the attacks' ranks 6 and 11, the second power's rank 1) at 2 points; 119 such ranks
across its playable heroes. X-Men Legends II charges 1 point for every rank and never reads the attribute. The default
is off; the Legends Classic builder writes the key for every build (legends-classic issue #35, builder SPEC 88).

## What changes

| Where (XMen2.exe) | Stock | With RankCosts |
|---|---|---|
| Skills screen, + (0x5e5d86) | -1 point | - the added rank's cost |
| Skills screen, - (0x5e5dcf) | +1 point | + the removed rank's cost |
| Buyable test (0x5f0590, call at 0x5f05ea): allows +, and the screen draws the talent buyable or greyed | points > 0 | points >= the next rank's cost |
| The screen's requirement sweep (0x5de570) | +1 per rank taken back | + its cost |
| Automatic spending (0x43c760) | -1 per rank | - its cost; a pick the hero can't pay for ends the spending at the loop's own end |
| Training respec (0x5d2810) | committed - base | the costs of the ranks above base |
| Losing levels (0x4bbae0) | one rank per missing point | ranks taken back until their costs cover the missing points (JNZ -> JG at 0x4bbd21); no refund of an overshoot |

These are every site that adds or subtracts a skill point for a rank (the callers of the points setter 0x43a5b0 and
getter 0x544a30). Not changed, because no rank price is involved: the per-level point, the Training's bonus point,
the item point, the "points to spend" marker (points > 0, as in the first game), the online state copy (0x60bd40 sets
the host's numbers as they are) and saves.

The cost is read at the moment it is needed: the rank's `<level>` through the game's own walk (0x4bdea0, which counts
the levels' `count` attributes), then its `cost` through the game's attribute lookup (0x5649c0). Values 1..7 are
honoured (the first game keeps the cost in 3 bits); anything else is 1.

The first game's rules (default.xbe): the talent parser 0xb4250 keeps up to three (rank, cost) pairs per talent, the
getter is CTalent vt+0x34 (0xb3ad0, 1 by default); the skills menu 0x16c800 charges and refunds the cost, its draw
0x169200 greys a talent whose next rank costs more than the hero has, the automatic spending 0xae8a0 tries a rank only
when the hero has its cost, and losing levels 0xb1ca0 takes ranks back while points are missing. Its requirement sweep
(0x167e2d) refunds 1 per rank; the fix refunds the cost there too, so no point is lost.

## Saves

Nothing about the save changes. A hero's unspent skill points and talent ranks are stored as they are and nothing
recomputes them, so a save made without the key (every rank bought for 1) loads with the same ranks and points; the
difference simply stays spent. The first game had no respec; a respec with the key on returns the costs of the ranks,
so such a hero would get back more than it paid.

## Install and online

Nineteen guards (the callees and every site's surroundings) and the nine writes go through one code transaction
(`xml1_hook_transaction`): a mismatch or a failed write leaves the game's own 1-point rule with one log line. Co-op
peers must charge alike: gameplay agreement rule 5, revision 1, `{installed, 7, 0, 0}` (verified `{0,0,0,0}` without
the key; requested but not installed is unverified and closes online play). The costs themselves come from the
build's talent files, as every other talent number does.

Tests: `xml2_rank_costs_test [XMen2.exe]` runs the cost rules, then the production install and every stub at the
retail addresses against invented talents (image fixture); with a legally owned executable it also checks every guard
and site against the retail bytes.
