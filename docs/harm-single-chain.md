# Optional single repeat chain for boxed fire

`[Game] HarmSingleChain=single` retains the native pending-activation bit after
a qualifying fire schedules its next repeat. Absent or `off` keeps stock
behavior. This is an explicit alternative policy, **not proven XBE parity**.
It is gameplay-agreement **rule 23, revision 1**, registered only when requested
(values `{installed ? 1 : 0,0,0,0}`). Absent, empty, `off` or `xml2` omits the rule and
keeps the released OFF digest. A failed or invalid request stays unverified and
blocks admission; a mismatched peer is refused naming `HarmSingleChain`. The
builder writes `single` by default for XML1 builds. Rule 21 (BoxHarmOrigin) is a
different mechanism with its own key and is unchanged.

This policy is independent of `[Game] HarmLoopStartup=xml1`, which preserves the
authored creation-relative startup delay. Both keys can be enabled together.
Neither introduces a one-second grace period each time a player enters a fire.
The first activation belongs to hazard creation; later entry meets the existing
repeat phase. Damage stops outside through the unchanged native area query.

## Mechanism and guards

The native delayed callback clears pending before invoking an action. The base
action then schedules the next repeat and sets pending. The harm tail at
0x439c00 clears it again; the immediate-request wrapper at 0x41a8da also clears
it after returning. Repeated contacts can consequently add persistent chains.

Two guarded bridges retain the bit for the ordinary affectable-harm class when
it has pure box damage, touch activation, a currently enabled visual loop,
no loop toggling, zero on/off timers and a finite positive repeat interval.
Other classes, smartfire, radial/mixed harm, authored-off loops, timer-driven
hazards, and nonrepeating hazards execute the original clears. The bridges do
not set a pending bit, so a failed/no-repeat request cannot become stuck pending.
No independent timer, owner cache, new entity field or save-format field is added.

The delayed callback still clears pending before the next activation. Therefore
the native scheduler can continue one chain while extra touches are rejected by
its existing pending gate. Damage amounts, modifiers, bounds, targeting,
reaction strength and random/fixed repeat intervals remain authored.

Both writes join one preflight/readback/rollback transaction. Guards include
the class vtable, enabled/pending gate, base repeat paths, delayed callback,
on/off timer layout and both clear sites. Unverified rollback refuses startup.

The current output census finds 103 qualifying definitions in 54 files with
331 placements, all fire, all with a 0.4-second repeat. Five definitions have
empty firstact and start through contact/script activation; the wrapper bridge
is necessary for those. The startup adapter has a different scope, including
one radial energy template. Dynamic script changes to loop state can affect
eligibility; the census describes authored initial configurations.

## Validation and limits

The native suite tests the production installer, each retail guard and corrupt
guard refusal, exact two-site footprint, genuine partial second-write rollback,
qualifying and excluded configurations, no invented pending state, and register/
FPU behavior. It uses synthetic entities and a user-supplied retail executable:

```
cmake --build <native-build> --config Release --target xml2_harm_single_chain_test
<native-build>/bin/Release/xml2_harm_single_chain_test.exe <retail-executable>
```

Live results and acceptance limits are recorded in the private task report.
In the opening level-five walking/re-entry trial, 528 contacts from one hero
still left one pending repeat. Native hits remained three HP, approximately
0.4 seconds apart during contact, and both verified outside tails had zero
damage. The original and PC queue insertion paths allocate a new event record
per accepted request; the XBE path at 0xaa020 does not supply a single-chain
guarantee. Original-console damage rates remain unmeasured.
Rule 23 is assigned and the builder default is on (candidate 0.2.3e). This deliberate
departure from the reviewed XBE scheduling behavior are integration decisions.
