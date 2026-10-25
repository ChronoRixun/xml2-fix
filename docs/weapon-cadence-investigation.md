# XML1 NPC weapon cadence

The missing semi-auto handler is real, but it is not an event-level limiter. In
XML1, `ch_weapon_semi_auto`'s entry callback (`0xe14d0`, vtable `0x3d7bb4` slot
`+4`) resolves the actor's weapon, reads its prototype's recoil getter (`0xb9c90`,
field `+0x1b4`), and writes current game time plus recoil to actor `+0x498`.
It does not filter individual `weapon_fire` events inside a move.

The actor's input update (`0x43650`, call at `0x436b1`) invokes the separate
weapon gate (`0x42f60`). Held power/fire bits reach its clock comparison at
`0x43030`. The gate resolves `power_attack` (`0x430cf`) and requests it at
`0x43131`, selecting the standing/crouched animation and flags using the weapon
prototype. Warm-up uses a separate deadline at actor `+0x49c`. Thus adding an
entry callback alone cannot restore this path on PC.

The original ranged brain (`0xef390`) resolves the weapon prototype near
`0xef3cd` and drives held power/fire bits at `0xeff50..0xeff63`. Its weapon burst
window is distinct from the authored seven-event `power_boost` animation. The
PC ranged brain (`0x519e00`) lacks that native weapon path. The builder currently
replaces weapon events with beam/projectile events and adds generic power-AI
metadata, including to `power_boost`. Its retained unknown handler name falls
back to `%default%` in `0x4fd860`; no recoil deadline is installed.

An owned-copy source inventory found eight weapons used by 19 NPC definitions
whose styles name this handler. These include rapid, ordinary semi-auto and
continuous weapons; their distinct flags and warm-up/burst windows must not be
collapsed into one global fire interval. The freeze weapon's recoil is 0.9 s.
The pistol's separate style and melee weapons are outside this handler cohort.

Retained observations from the separate sound investigation show one original
freeze-weapon shot gap of 0.954632 game seconds. Seven complete port bursts each
contain seven requests over 0.684–0.734 game seconds. These are evidence of a
ranged conversion difference, not matched attacks-per-enemy-minute trials and
not evidence that the melee discrepancy has the same cause.

`aireusetime` alone is not an equivalent shared weapon gate: it belongs to each
AI power slot, is armed by selection, and does not represent accepted handler
entry, cross-move recoil, warm-up or the weapon burst window. A compatibility
implementation needs source-derived weapon metadata and native admission/entry
semantics, or must explicitly limit its claim to a narrower data correction.

Status: static findings, not a measured gameplay fix. No new gameplay rule ID
is allocated by this investigation. Any runtime candidate must participate in
the existing agreement and refuse incompatible peers.

The retained stationary #101 trials used only Brotherhood Energy and Smash
attackers. Their source stats have no weapon, their powerstyles have neither
`weapon_fire` nor the semi-auto handler, and their ice-tunnel spawners have no
weapon override. Therefore this missing weapon path cannot explain the measured
20.44-versus-32.26 offensive-entry gap in that cohort. It is a separate defect in
the gun-armed cohort. The opt-in [recoil-admission candidate](npc-semi-auto.md)
must not be described as resolving that original rate discrepancy.

## Follow-up live observations

An isolated original officer observation recorded 24 shots in 45.024 game
seconds, with 23 consecutive shot intervals of 0.9180–0.9201 seconds (median
0.9185). Its 24 handler-entry deadlines were bit-identical to game time plus
the weapon's binary32 recoil, and all 24 firing requests passed the deadline.
This agrees with a 0.9-second gate quantized by the original's approximately
49-Hz default ceiling; 0.95 seconds is not a hardcoded firing interval.

The original officer had AI attack range 550 and weapon-brain vtable `0x3d8f94`
(entry `0xef390`). The PC officer had range 90 and the generic melee brain.
Both carried the same source-derived NPC health/energy maxima. Restoring the
handler does not restore the weapon-specific brain selected during original
AI initialization (`0xfd362..0xfd42f`). Changing the generic melee range alone
would not establish equivalent decisions.

A release-profile PC diagnostic reproduced seven shots over 0.7000 game
seconds in one automatic burst. Its candidate follow-up produced no shots and
15 melee move starts over 30.045 game seconds. Both had other games running,
so neither is an accepted throughput result or evidence of attack-rate parity.
The outcome does not establish restored shooting behavior: the candidate stays
off in normal builds and is not an accepted fix for #101.

The earlier sound investigation's successful capture used a minimal harness
configuration without `NpcPowerRule` or the release Balance settings. Its style
bytes match the current build, and its observed burst is real, but its repeated
burst frequency is not a matched release-profile baseline. Shot events and
offensive move starts must also remain separate: a seven-shot burst is one move.
