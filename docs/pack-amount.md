# Pack amount

`[Game] PackAmount=xml1` makes a health or energy pack restore a quarter of the hero's maximum, as in X-Men
Legends I, instead of the third X-Men Legends II gives. No key, or `xml2`, leaves the game's own amount. The
companion builder sets `xml1` for its first-game builds and owns the key during INI merges.

## The two games

Both games' item tables give the packs the same scripts: `HEALTH_ITEM` runs `restoreHealth('_ACTIVATOR_',-1)` and
`ENERGY_ITEM` `restoreEnergy('_ACTIVATOR_',-1)`, and both describe them as restoring 25%. The amount -1 means "a
fraction of the maximum", and the fraction is code, not data:

| | X-Men Legends I (`default.xbe`) | X-Men Legends II (`XMen2.exe`) |
|---|---|---|
| script functions | 0x9bd50 / 0x9bcb0 | 0x4a2dd0 / 0x4a2c20 |
| fraction | 0.25 (float at 0x3c83b0) | 0.33 (`mov [esp+0xc], 0x3ea8f5c3` at 0x4a2e4e / 0x4a2c96) |
| amount | `(int)(max * 0.25)`, 16-bit maximum | health `0.33 * max` (float), energy `(int)(0.33 * max)` |
| double heal | none | a random draw against the hero's double-heal chance: 0.66 and the double sound |
| use | 0x3a7c0 / 0x3a870 | 0x42ca80 / 0x42cb70 |

Both use functions add the amount to the hero's pending heal: `min(max, max(current, pending) + amount)`. So a pack
is a fraction of the maximum in both games, whatever the hero's level or stats (the port's x4 hero stat scale moves
the maximum, and the pack with it).

## The patch

The installer guards the target test and the single-amount stores of both functions (0x4a2e4c-0x4a2e5d and
0x4a2c94-0x4a2ca5) and the two script table entries (0x68aa68, 0x68aa58), then writes 0.25 (`0x3e800000`) over the
two 0.33 immediates at 0x4a2e52 and 0x4a2c9a through the hook transaction (readback, rollback). The double-heal
stores (0.66 at 0x4a2e9a / 0x4a2ce2) are not touched: X-Men Legends I has none, `[Game] DoubleHeal=0` removes them,
and their bytes are inside that patch's guards. The random draw is unchanged, so the shared random stream is too.

Explicit amounts (`restoreHealth(name, 10000)` in scripts) are unchanged: only the -1 form reads the fraction.
Health keeps XMen2.exe's float amount where X-Men Legends I truncates to a whole point; the difference is below one
point.

A refused install leaves the game's third and writes one line to `xml2-fix.log`; a rollback that cannot be verified
refuses startup. The test pipe's `status` reports `pack amount xml1`, `game's own` or `unavailable`.

## Online

Every peer computes its own heals, so the effective value is gameplay rule 13 of the [gameplay
agreement](gameplay-agreement.md): peers with different values refuse each other at join.

## Verification

`xml2_test.exe --pack-amount-rules` checks the value rules, the numbers, that each guard covers its fraction and
stays clear of the DoubleHeal guards, and, with an `XMen2.exe` at hand, every guard and both fractions.
