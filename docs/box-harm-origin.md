# Original box-hazard impact origin

`[Game] BoxHarmOrigin=xml1` restores the original game's origin argument for box
hazard damage. Absent or `xml2` retains PC behavior. This is an opt-in candidate:
**it has no assigned gameplay-agreement rule ID and requesting it blocks online
admission**, including when installation fails. The builder does not enable it
by default yet. Protocol and save format are unchanged.

## Problem and original behavior

An idle hero in the opening fire can receive repeated upward impulses, remain in
the damaging volume and rapidly accumulate touch-triggered repeat activations.
The raw hit is still the original 3 HP. Reducing that number or inserting a
cooldown would change authored gameplay.

The original box service at 0x5a8f0 passes a zero origin to its target dispatcher
at 0x5a9b9-0x5a9d5. The zero skips the direction/distance calculation in 0x59d50.
The harm descriptor initially has zero direction, so actor hit-context creation
at 0x44fb0 reconstructs it from victim and attacker **entity origins**.

PC box service 0x451ba0 instead copies the box centre at 0x451c6e-0x451c79 into the
argument to 0x450d00. That dispatcher generates a direction using the target's
centre. The nonzero direction bypasses the otherwise equivalent fallback in
actor hit-context creation at 0x436870.

At the tested fire centre, the hero's feet are below the hazard's origin while
the hero's body centre is above the hazard's box centre. Consequently the
directions have opposite vertical signs. The native party reaction transforms
them into a vertical argument of +0.55 on PC and -0.45 on the original-compatible
path, both with strength 100 derived from authored knockback 200. Live traces
confirm upward motion on PC and a grounded hero with the correction.

Both engines permit touch activations to add repeating action chains; the
0.4-second repeat delay is not a minimum damage interval. The PC-only upward
motion therefore amplifies a scheduling behavior that already exists in XML1.
The correction does not impose a new damage cap, remove touch activation or make
fire safe to stand in.

## Installation and scope

The six-byte site at 0x451c6e originally selects the source pointer and stack
destination for the native vector-copy constructor. The bridge supplies an
static zero vector and retains the stack destination. It preserves
the other registers, flags and FPU stack. The spatial query has already run;
its bounds are unchanged. Damage, damage type, modifiers, knockback strength,
activation settings, attack IDs and the radius-damage branch are unchanged.

The change also restores the original zero-origin dispatch delay calculation;
it is not merely a visual adjustment. Guards cover the harm caller, native stack
layout, copy, dispatcher call and origin-length gate. Installation uses the
shared preflight/write/readback/rollback transaction. A normal refusal retains
stock behavior; an unverified rollback/protection restore refuses startup.

The current converted-map census contains 137 boxed harm definitions, 419
placements in 66 zones, with no combined radius-and-box definitions and no boxed
harm entity templates in the built template census. All 348 map payloads in the
comparison were byte-identical. This is potential scope, not runtime acceptance
of all those rooms. The native box service's identified direct caller is the
harm activation path; radial-only harm does not use the patched site.

## Evidence and limits

The level-five opening-room negative control uses native fire hits, with enemy
AI paused and position setup disclosed. Repeated standalone stationary runs
measured approximately 68-72 HP/s on PC versus 15-20 HP/s with the correction,
over roughly five-second no-input windows. These survival diagnostics raise
maximum/current health to keep the actor alive; the damage remains 3 HP per hit.
They are specific contact histories, not a universal damage-rate guarantee.

A separate normal-health level-five test kept native health and granted no
survival health: from 100 current HP, the 34th three-point hit killed the hero
0.752 game-seconds after the first hit on PC, versus 6.484 seconds with the
correction. Both runs used a disclosed floor-verified stationary setup and no
input during exposure. Fire remains dangerous; the correction is not a rebalance.

Short fixed-input edge contacts lost 33 HP before and 36 HP after, so there is no
claim that every walking route becomes less damaging. Once verified outside the
fire, neither path showed a damaging burn tail over five seconds. The original
and PC fire-effect helpers (0x5d5c0 / 0x454290) add effects and sound, not a damaging
burn powerup. Earlier 10.416667-HP after-contact losses were physical impacts,
not burning ticks.

The selected original and PC fire-hit effect files both specify a 0.1-second
effect loop time, 0.4-second flame-particle lives and 0.15-second flash lives.
Those are visual component timings, not health-damage ticks. Repeated hits can
overlap these effects; they do not create a stacking damaging burn status.

The original-console rate was not measured. XML1 executable/data establish the
intended dispatch/fallback behavior; the corrected PC run is an interoperability
test, not an Xbox gameplay capture. Other rooms, difficulties, co-op, and a full
normal-health traversal still require acceptance. Rule assignment, registration
of effective installed state, and builder-default integration remain outstanding.

The native test executes the production installer and bridge, checks every
dependency guard against a local retail executable, corrupts each dependency,
checks OFF/invalid/relocated behavior, verifies the exact write footprint and
exercises partial-write rollback plus ABI/FPU preservation:

```
cmake --build <native-build> --config Release --target xml2_box_harm_test
<native-build>/bin/Release/xml2_box_harm_test.exe <retail-executable>
```

No game binary, data dump or disassembly is included in the test.
