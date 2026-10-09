# The stats screen's help text follows [Balance]

The character stats screen's help panel states the engine's per-point numbers: the Body help's
"Health 25 + 5 per Level + **3** per Body" names the health-per-body-point float (0x6d9770), and the
Strike help's "+**2**% per Striking" the damage bonus per strike point. `[Balance]` replaces both for
the scaled heroes (`HeroHealthPerBody`, `StrengthBonus` over `HeroStatScale`; details in
[balance.md](balance.md)), which left the help naming numbers the game no longer applied.

The fix rewrites the two help texts at runtime, from the same parsed ini values the combat patches
apply, and only when the corresponding patch is actually in (a key unset or a guard refused leaves
that text the game's own). The other two stat helps need no rewrite:

- **Focus.** The static "30 + 4 per Level + 2 per Focus" is the energy formula no `[Balance]` key
  touches (the port keeps the engine's own), and its dynamic "Damage Bonus" line already reads the
  patched mind multiplier — the balance patch redirects the stats screen's own call.
- **Speed.** `HitChance=xml1` changes which roll decides a hit, not the Attack/Defense Rating the
  screen displays, so "5 per Speed - 35" / "3 per Speed" stay what the screen shows.

## Why the DLL, not the builder

The texts live in `Data/strings.engb`, which the build ships; but the numbers come from
`xml2-fix.ini`, a player-editable file the DLL re-reads every start (every `[Balance]` key is
documented for tuning). A build-time rewrite could state a number the player's ini no longer
configures — the game would say one thing and do another. Rewriting the fetched string keeps the
text and the behaviour on the same source for any ini.

## How

`XMen2.exe`'s string manager (global `0x76d878`, vtable `0x68e990`) answers
`get_string(this, id)` at vt+8 (`0x4bd720`). The stats screen's own code (0x5e0b32..0x5e0c27) is
the only fetcher of the four stat helps (ids 0x442..0x445, one push each) and snprintf's the result
into a 512-byte buffer, so the fix writes one vtable slot: its guard is the slot's dword
(`20 d7 4b 00`), and every id other than the two helps passes to the game's getter untouched. The
replacement is built once from the fetched retail text (so a localization whose wording differs is
left alone — the rewrite needs the exact " per Body" / "%% per Striking" literal) and cached;
`%%` stays the snprintf escape the fetched text carries.

Offline tests cover the guard, the number formatting and both rewrites on made-up texts
(`xml2_stat_help_test`). The `balance` line of the test pipe's `status` is unchanged.

The whole feature is on `[Game] StatHelp=1` (the default); `0` keeps the game's own texts.
