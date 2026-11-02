# Shared handlers C (rule 46)

All keys are under `[Game]`, accept `xml1` or `xml2`, and default to native. Absent, empty, and `xml2` perform no image accesses or writes and register no agreement entry. Invalid selections or mismatched executable bytes remain unverified. A selected mask installs in a single guarded transaction with readback and rollback. Requested-only agreement ID 46 revision 1 records `{installed_mask,0,0,0}`. Rule 42 remains independent. Callback registration guards cover only the slots each part owns; fly-charge collision and pickup cancellation may already belong to the grab/carry installer. The fixture installs external replacements in those slots before C and verifies they remain unchanged. Full combined startup and lifecycle validation still belongs to integration. Capacity-owned contact query APIs accept only the exact replacement target published by the verified capacity installer; arbitrary jumps are refused.

| Bit | Key | Source behavior and PC sites |
|---:|---|---|
| 1 | JumpAttackDamageRule | L3 minimum from the native archive-aware values reader; signed 16-bit stages after truncation of the source value, 0.75 multiplication and timing multiplication. Producer bridge 4ee99b only. |
| 2 | FlyChargeDamageRule | Same L3 lookup and signed narrowing; producer bridge 4eeb88 only. Collision belongs to Xml1GrabCarry work. |
| 4 | ColossusChargeRule | Coupled input 4eced0 and contact 4ed220. Event-100 affordability, idle plus exit cleanup, explicit input mask 40, caster talent-evaluation context for PC tagged punch, no repeated-target suppression/weight admission, source horizontal crash vector and -0.71 threshold, special action and 450 recoil. Native 320 speed, 1.3 deadline and 450 exit cap remain. |
| 8 | BeastBounceRule | Contact 4ed1a0 always emits event 1, stops velocity and records the time, including null contact records. Other movement/timing remains native. |
| 16 | FastballRule | Event bridge 4f4adf emits for actors, non-actors and null contacts. Native angle, response velocity and action tail remain. |
| 32 | IdleBoredRule | Source map predicate slot 3c; remove controller/team exclusions and nonhumanoidskeleton selection gate; restore bored entry policy. The native stun redirection bytes remain untouched and rule 18 retains stun ownership. |
| 64 | LocomotionScaleRule | Remove the local animation-rate division by scale_factor at 4f01d4. |
| 128 | AnimationPhaseRule | Remove additional input phase-preservation bit write at 4f026a, shared by move and pickup idle/walk. Preserve all incoming flags and default input result. |
| 256 | TeleportVisibilityRule | Temporary suppression cleanup at 4efc7e and 4f44a4 follows the source even with an invisibility affecter. This key affects visuals and participates in rule 46 for exact selection agreement. |
| 512 | TelekinesisRule | Coupled suspend parser/entry/control/release/cancel, signed tklevel and damagelevel, token sidecar and rule45 lifecycle. |
| 1024 | GuardDecideRule | Source short entry and input-scheduled interaction policy, pickup claim and rank-3 walllunge acquisition. |

The values reader selects `value[name=L3].min` from `data/values.xmlb`; the supplied source profile has minimum 15. PC's native value manager only retains DMG2/DMG3/DMG4/K2/K3, so asking that manager for L3 would return zero. The local reader avoids mutating DMG3 and does not draw random numbers. Content must retain the L3 row; missing/unreadable content does not fabricate a fallback damage value.

`TelekinesisRule` and `GuardDecideRule` require installed rule 45, whose pickup entry admits the source guard claim and whose victim lifecycle retains telekinesis. Rule 46 installs after that dependency. Rule 39 supplies the separate rank-2 cling/backflip path; guard restores rank-3 `walllunge`. See [the coupled contracts, native fixtures and remaining live checks](tk-guard.md).

## Verification

Build below normal priority with MSBuild `/nr:false`:

```powershell
cmake -S . -B build/native -A Win32
cmake --build build/native --config Release -- /m:1 /nr:false
build/native/bin/Release/xml2_shared_handlers_c_test.exe <owner-XMen2.exe> <owner-default.xbe>
python -B scripts/shared_handlers_c_matrix.py <owner-XMen2.exe> <owner-default.xbe>
```

The fixture hash-binds both owner binaries, compares every selected guard, checks OFF/invalid/foreign-image/failure states and exact production write footprints, negotiates rule-46 masks, and executes original callback slices with synthetic services. It covers signed stages, energy/input exits, dispatch recipients, crash boundaries, idle policy, local locomotion rate/phase and temporary visibility cleanup. Live physics, resource loading in the complete engine, animation, authored triggers and combined grab/flight integration remain live checks. No game or emulator may be launched by this task.

The selection matrix now covers all 2,047 nonempty masks. `xml2_tk_guard_test` executes owner XML1 entry/control/filter/cancel/guard instructions and PC suspend parser/evaluators. The separate `--off-proof` mode makes the four-MB fixture image inaccessible while OFF, then compares image and serialized agreement bytes.

Colossus's horizontal contact normalization refuses zero/non-finite lengths
before its source-ordered x87 reciprocal. The fixture executes XML1 `DA900` and
PC `4ECDC0` frame instructions and both cleanup paths: facing*320, retained jump
velocity, upward-Z100 and total-speed450 cleanup caps agree. Those motion values
remain unchanged. PC `4DC3E0` treats the second actor as talent-evaluation owner:
its stats supply damage at `4DC353`, knockback at `4DC37E`, and MaxRange at
`4DC44D`. XML1 `CF250` uses the caster and literal data and ignores that second
actor. Supplying the collision victim therefore evaluates converted `%xco_*`
against soldier stats and can produce zero. Colossus now supplies the caster
twice, matching the original PC contract, while the struck actor still owns
health/crash decisions. Other contact handlers retain their own contracts.

The fixture executes actual PC tag lookup, event dispatch, punch builder and
native encoded-talent evaluators with distinct caster/soldier stats. Soldier
context produces damage/range/knockback 0/0/0; caster and fixed collision produce
9/40/190. Its registry, modifier and terminal volume/HP services are synthetic.
Arc180 takes XML1 `5BA90` / PC `450580`; live query membership/defenses remain
validation work. The fixed-box difference belongs to another arc branch.
