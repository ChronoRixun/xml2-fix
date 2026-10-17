# HAARP exterior: weapon priority restoration and remaining limits

The builder added priority 5 to gun moves whose XML1 priority was omitted. That made
firing enemies reject ordinary pain reactions that the original accepts. The content-only
correction preserves source priority and overrides while retaining existing AI metadata.
No new engine rule is needed; party StunRule remains rule 18 revision 2. Engine code,
damage, spawn data, save format and protocol are unchanged.

## Mechanism and scope

XML1's comparison at `0xeabf0` reads current move priority at `+0xb0`; PC's `0x500650`
reads `+0xc0`. Both accept when requested override is at least current priority. Execution
of the supplied retail fragments with synthetic moves passed 16 checks: pain override 4
and stun override 0 pass against priority 0 and fail against 5.

The original weapon handler is registered at `0xe8307` with vtable `0x3d7bb4`. Entry
`0xe14d0` initializes actor weapon timer `+0x498`; it does not raise move priority.
The original move constructor defaults omitted priority to zero.

Live negative controls captured nonlethal hero hits during gunfire with pain refused at
PC `0x435a20` and the firing move retained. Restored priority passes that check and native
dispatch at `0x4320de`; the actor enters pain. A selected linked-evidence set contains
12 refusals before and 14 acceptances after. This proves the reaction discrepancy,
independently of encounter survival variation.

The change affects 27 moves in ten generated styles used by 21 NPC entries: HAARP gun
users, GRSO variants, nuclear and Weapon X guards, police and a generated Danger Room
entry. Rifle-like styles use `power_attack`, `crouch_pow_atk` and `power_boost`; the
flamethrower uses `power_attack`/`flame_sweep`, and the pistol uses `power_attack`.
Only the intended priority removals differ in 18 files, counting format mirrors.
Explicit priorities, grenade auxiliary priority, attack events, damage, reuse times and
AI types remain unchanged. No Brotherhood style changes.

V45 checks generated priorities against source. Restored output passed 51 source-move
checks; substituting the old MP5 tree in memory produces three expected errors.
Synthetic conversion tests cover missing/explicit values and priority overrides.

## Encounters and measurements

The original exterior maps `haarp_ext01` through `haarp_ext04` contain HAARP soldiers,
security and flamethrowers. Source and port retain 18/28/18/33 authored spawn instances
and world levels 3/5/6/7. Those totals include deferred spawns, not simultaneous attackers.
The Brotherhood ice tunnels are separate level-7 maps with original 27/20 authored spawns.
Source/output counts and character types match in the audited maps.

The measured exterior initial group was two level-3 soldiers and two level-3 security
enemies in `haarp_ext04`. The `icetunnel1` group was four level-10 Revolutionaries and
one level-7 Marauder. World level is not spawned NPC level.

Fresh original and port runs used level-5 Cyclops, Iceman, Storm and Wolverine. XP, map
entry and positions were staged. The controlled hero received real movement/light-attack
inputs; followers used native AI. No invulnerability, scripted damage or script kills
were used in measured fights. Initial enemies were tracked by generation-checked identity;
additional native engagements were retained. Runs ended at leader health exhaustion,
initial-group defeat or 90 game seconds. These are not human playthroughs or party-wipe tests.

The primary wave windows observed no other running games. The original RAM reader was
validated against GDB, then sampled without repeatedly pausing the emulator.

| Exterior group | Original | Before, two trials | Restored, two trials |
|---|---:|---:|---:|
| Observation/clear seconds | 30.59, leader exhausted | 20.48 / 29.66, cleared | 15.87 / 13.65, cleared |
| Leader HP lost | 83 including overkill | 55 / 59 | 19 / 45 |
| Leader free of sampled reactions | 100% | 100% / 100% | 100% / 100% |
| HP loss per observed living minute | 162.78 | 161.50 / 119.49 | 72.05 / 198.52 |

The scoped repeats cleared faster with less total HP lost. The rate per minute did not
improve in both pairs because exposure ended sooner. Broader pursuit trials varied,
including early deaths with restored priority. Small unseeded samples cannot establish
a general difficulty effect or complete original parity.

The unchanged interior negative control exhausted leader health at 13.41 seconds in the
original, 14.93 before and 4.44 after, with free shares 41.93%, 74.98% and 92.75%.
These differences cannot establish an effect of a content change touching none of those
enemy styles. No StunRule revision 3 change is justified by this experiment.

## Remaining work

The original weapon-input routine at `0x42f60` has a dedicated path selecting
`power_attack`; PC lacks its `ch_weapon_semi_auto` handler. Current original traces did
not show the port's `power_boost` usage, but move transitions do not count every repeated
shot. This remains a cadence/routing lead, not proof that the original never uses bursts.
A future native weapon restoration needs separate design and agreement coordination;
no rule ID is allocated here.

The maintainer's exact death location/loadout, complete weapon cadence, party-AI parity,
other gun encounters and live online/save-load coverage remain open. The retained change
restores a proven original reaction rule; it does not close the broader stun issue.
