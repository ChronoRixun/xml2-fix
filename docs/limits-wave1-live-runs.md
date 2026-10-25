# Limits wave 1: live acceptance runs (for a later game agent)

Written 2026-10-25 with branch `limits/wave1`. None of this has run: the wave was code-only. Follow the common
rules of the day (HOLD-GAMES / slot.py `--wait`, registry export/restore, a copy of the current candidate package
with this DLL swapped in, no focus, delete the copy and its Documents\Activision folder afterwards).

## Setup

Two ini variants on the same package copy:

- **W1 (raised):** the candidate's `xml2-fix.ini` plus
  `[Limits] IGBCache=1024`, `ResourceNames=2048`; `[Engine] ModelHandles=8192` (ModelPools stays `x4`).
- **OFF:** the same ini with the three keys removed (ResourceNames back to the candidate's 1024).

Read in `xml2-fix.log` at start: `limits: IGB cache raised from 200 to 1024 records ...`, `limits: resource name
table raised from 450 to 2048 names`, `model pools: raised to x4 ... handles 8192, transforms 8192 ...`,
`limits: address space - LAA on|off ...; highest reservable address 0x...`. The test pipe's `status` gives
`igb n/1024`, `names n/2048` and `model pools x4: models n/8192`.

## R1 - IGB cache: cross the old cap without a bounce

1. OFF, negative control: build one zone with `--tiles folder` (igb-cache.md section 10 B: hive2_2_4, 179 records +
   about 51 resident > 200) and walk into it from its neighbour. Expect the retail bounce back to the neighbour
   (SPEC 13) and `igb 200/200`.
2. W1, same zone, same route: the zone loads (no bounce), `igb` reads > 200, `names` < 1024 of 2048.
3. Pass: no bounce; `igb` peak over the old 200; **peak <= 512** (50 % of 1024); names peak <= 1024; model handles
   peak <= 4096 (50 % of 8192).

## R2 - long session: peaks stay under half

W1: the 60-zone sequence of tour v2 followed by mansion / man4 / mansion4_1 (igb-cache.md 10 C), 4-hero party, with
a fight in each of the five heaviest census zones. Sample `status` at every zone change (actors, names, igb, model
pools, private bytes). Pass: no bounce, no missing model counted by the status probe, `igb` <= 512, `names` <= 1024,
handles and transforms <= 4096, private bytes logged (feeds E12's 75 % gate). Then exit the game normally: no crash
on exit (the patched pool destructor runs on the relocated block; a crash points at a missed displacement).

## R3 - old save loads unchanged

1. With the OFF ini, load the tester's newest real save and Owen's (copies), note party, zone, inventory count and
   `status`.
2. With W1, load the same copies: same party, zone, inventory and objectives; one zone change; save to a new slot.
3. With the OFF ini, load that new save: it loads (wave 1 changes no save byte; nothing in the save knows the cache
   size). Pass: all three loads identical in party/zone/inventory; no `.lcsave.newer` refusal.

## R4 - co-op admission (two instances on this PC, or this PC + Shelley's)

1. W1 host + W1 joiner: admitted.
2. W1 host + OFF joiner: refused at admission; the host's notice names `31 IGBCache (not on the other PC)` and
   `28 ModelPools`; the joiner's names `31 IGBCache (disabled on this PC)`. Resource-name changes also name
   `32 ResourceNames`; other non-stock resource limits have their own rules (33..37).
3. W1 with `ModelHandles` removed on one PC only: refused naming `28 ModelPools`.
4. With `IGBCache` absent or `200` on both PCs, `ResourceNames=450` versus `2048`: refused naming
   `32 ResourceNames` in both directions. Equal capacities: admitted.

## R5 - OFF proof

The release gates with the OFF ini: `prove_off_wire.py`, `prove_combined_install.py`, `check_release_switches.py`
must report the released bytes and agreement record (no rule 31..37 entries when those limits are absent/stock, rule 28 values `{1,4096,2048,128}`).
