# Danger Room training target deaths

Some original courses protect their targets from ordinary damage until the
player connects a specified combo. A target reaching minimum health during
other attacks does not establish a death-handling defect.

The original Xbox course spawner at `0xc6770` sets physical flag `0x10` when
the course has a nonzero `connectlimit`. The connected-move handler at
`0xc7c10` checks the selected combat-node identity, increments the connection
counter, clears that target's protection, and makes the current hit lethal.
The target dies and the spawner can supply the next training opponent.
Completion at `0xc5e00` checks the authored connection/execution/kill limits.
These are separate requirements; they must not be replaced with a kill quota.

The PC retains this mechanism. The spawner at `0x4d07f0` protects a target
when `connectlimit` is nonzero and `killlimit` is zero. All original courses
give the same protection outcome under these two predicates. The selected
move starts through `0x4d3b70`; its connected hit reaches `0x4d3c40`, which
increments the connection counter, calls the target's undying setter
(`0x41c560`) with false, and supplies a lethal damage value. The execution
handler at `0x4d3c00` counts move execution separately. Completion at
`0x4d2c30` checks those counters and records success.

Course examples, expressed as interoperability facts:

| Course | Authored condition | Spawn protection |
|---|---|---|
| FR101 | Three `attacklight3` connections | Until the required combo hit |
| FR102 | Three `attackheavy2` connections | Until the required combo hit |
| FR103 | Four `popupattack` connections | Until the required combo hit |
| FR104 | Five `attacktrip2` connections | Until the required combo hit |
| FR105 | Six `attackstun3` connections | Until the required combo hit |
| FR112 | Seven `grabstart` executions | No connect-limit protection |
| CHMAG | Twenty kills | No connect-limit protection |

The five combo courses contain seven robot spawner records. FR112 adds two
unprotected robot records. Calling all six exercises permanently undying
targets is incorrect: the grab exercise has a different condition, and the
combo exercises deliberately kill a target on the required connection.

The builder's course-only actor variants omit the queued spawn cloak and
change the stats identity. They do not add undying/invulnerable flags or
change combat styles or objectives. `[Game] DangerRoomHealthBars=xml1`
preserves this death path and shows gauges on protected training targets.
It remains gameplay agreement rule22 because the newly reached native HUD
path can refresh dirty stats and schedule clamping; equivalent timing has
not been proved.

For a live regression check, use the authored combo rather than isolated
normal hits. Observe target protection before and after the connecting hit,
the target death and respawn, the connection counter, and the course success
result. Separately check normal kills in a kill-based course and ordinary
campaign enemies. Test-only scripts that kill actors or edit protection or
counters cannot establish that these player actions work.

The Xbox comparison above is static. On the unchanged candidate0.2.3d PC
build, real-input FR101, FR102 and FR103 runs killed three, three and four
successive robots respectively and reached the native success result. These
runs staged course availability and menu access, and made no combat-stat,
protection or counter edits. A wrong-combo FR102 control took real damage
without connection progress, then completed after the required move was used.
Ordinary campaign enemies also died through real attacks. No gameplay patch
is required by those observations; isolated punches leaving protected targets
alive are consistent with the original mechanism.
