# The first game's death threshold

`[Game] DeathRule=xml1` makes an entity die on the hit that leaves it below 1 health, as in X-Men Legends I. An absent
key or `xml2` leaves the game untouched (death at 0); any other value is logged, patches nothing and counts as a
requested rule that did not install (online play closed, as for `[Game] PlacementCrush`). The X-Men Legends I port
sets the key.

## The two games' rules

| | X-Men Legends I (`default.xbe`) | X-Men Legends II (`XMen2.exe`) |
| --- | --- | --- |
| Damage taken from health | a whole number (`0x92220`) | a float; a whole hit below 1 is rounded up to 1 (`0x49834e`) |
| Death test after the hit | health below 1.0 (`0x92675`, mask C0\|C2) | health at most 0.0 (`0x4983f6`, mask C0\|C3) |
| A character's last-stand window | health after the hit at most 1.0 (`0x463bf`) | at most 0.0 (`0x438b51`) |
| A hit marked as killing (type 8) | health below 1.0 (`0x4648f`) | at most 0.0 (`0x438c2f`) |
| Harm entity's AI hazard announcement | while health is at least 1.0 (`0x46bae`) | above 0.0 (`0x43935e`) |
| Affectable harm: early return, killer record | health below 1.0 (`0x46e12`, `0x46ed0`) | at most 0.0 (`0x439542`, `0x439600`) |
| Power-gated bridge objects react | while health is at least 1.0 (`0x944ea`) | above 0.0 (`0x49a64c`) |

Every class that can be hurt shares the physical damage function on both engines (characters through their own
damage function, which delegates to it), so the line is the same for enemies, heroes, bosses and breakable objects.
Every other health test of both games (is it alive, may it be hit, remove the corpse: about 175 in the first game) is
"above 0" in both and stays as it is.

With XMen2.exe's float damage, an enemy could be left alive on a fraction of a point (0.2 or 0.9999993 health was
seen in play), still fighting, its death script and death drops waiting for a further hit. The first game's whole
numbers left no fraction to sit on, and its line at 1 would have ended it anyway.

## What the key does

Eight comparisons and one jump, in `XMen2.exe` only, after every byte they rely on is checked against the retail build (image
base `0x400000`): each one's float operand moves from the game's 0.0 (`0x680030`) to the game's own 1.0 (`0x68012c`),
and where the first game tests "below" rather than "at most", the status mask of the following `test ah` goes from
`0x41` to the first game's `0x05`, so a health of exactly 1 lives as it did there.

| Address | Comparison |
| --- | --- |
| `0x4983ce` | `bossundying`: back to 1.0 before the death test (no first-game counterpart, see below) |
| `0x4983f0` | the death test |
| `0x438b47` | the character's last-stand window (mask unchanged: "at most 1" in both) |
| `0x438c28` | the character's killing-hit mark |
| `0x439357` | the harm entity's AI hazard announcement |
| `0x43953c`, `0x4395fa` | the affectable harm's early return and killer record |
| `0x49a646` | the power-gated bridge objects (magma / ice / telekinesis bridges) react only while health is at least 1 |

**The killing hit's health.** The first game's line at 1 lives in a game of whole numbers (the health formula rounds
up, damage is a 16-bit integer, regeneration whole points), so there a hit that takes health below 1 always takes it
to 0 or below - and everything after the death test relies on it: the kill handler (`0x45e20` there, `0x436c50` here)
gives the kill's XP only at health 0 or below, and every "is it alive" test asks "above 0". With XMen2.exe's float
damage the moved death test alone left a dead entity at 0.2: in play the HAARP officer died (state 11) and his
corpse was removed, but his keycard never dropped, no kill was credited, and every "above 0" test still called him
alive. So the death branch's first instruction (`0x498407`) jumps to a 42-byte bridge of the fix's own: when the
health is above 0 it calls the entity's own health setter (vtable slot `0x1a4`, the call the hit itself made at
`0x4983bf`) with 0.0, then runs the two displaced instructions and returns to `0x49840d`. That is the state an XML1
hit below 1 always left. A health at or below 0 (and NaN) is left alone.

The eight comparisons and the jump are written in one transaction (all or none, read back, the old bytes restored
on any failure); the bridge is written and made executable before it. No save field, no number a script reads and no
network message changes.

**Undying.** The first game's `undying` flag (+0x2b8 bit 0x10 there, +0x30c bit 0x10 here) puts the health back to
1.0 inside the death branch on both engines and is untouched: an undying boss still stops at 1. XMen2.exe's own
`bossundying` / `setBossUndying` (+0x30d bit 0x40) restores 1.0 before the death test when health is at most 0; it
moves to "below 1" with the death test, so it still holds a boss at 1 instead of letting it die from 0.5.

**Not changed.** The damage itself stays XMen2.exe's float (the first game's whole-number damage is a separate
difference); a pain cap, invulnerability and the 1,000,000 / direct-damage exceptions are decided before the death test
and are untouched.

## Multiplayer agreement

Online play is lockstep, so when an entity dies is shared gameplay: the rule is entry ID 9, revision 1, of the
[gameplay capability record](gameplay-agreement.md), `{installed ? 1 : 0,0,0,0}`. A requested rule that did not
install is unverified and closes online play; a rule not requested registers as verified off in an LC installation.

## Checking it

`xml2-fix.log` says whether the rule is on and, when a check fails, which site. `xml2_test --death-rule-rules` checks
the value's rules, the x87 decisions before and after against the first game's (every 1/64 step and its neighbour
from -6.25 to 6.25, exactly 1, NaN), the eight sites on their own and, with a copy of `XMen2.exe` at hand, every
guarded byte, both constants and the writes on that copy (29 bytes, all inside the sites); the bridge itself runs
against a fake entity (health above 0 to 0 through slot 0x1a4, at most 0 and NaN untouched, the x87 stack intact).
