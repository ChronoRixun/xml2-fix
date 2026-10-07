# Character ladder paths

New in XML2 Fix 1.3.2.

Legends Classic issue #32 converts the source animation's Motion keys into relative
native paths named `x1_ladders/sewers/mp_cabinet` and `x1_ladders/arbiter/mp_cabinet`.
Stock characters assign these paths but do not schedule the generic path timer; their
ordinary movement-goal predicate can also bypass the path evaluator.

With `[Game] CharacterLadderPaths=1`, two guarded character virtual callbacks retain
the original behavior and add native scheduling for these exact loaded path objects.
Only the generic evaluator's call site can override the ordinary goal predicate.
Resource lookup never precaches missing names and follows the existing name table,
including the ResourceNames expansion. A bounded record keyed by actor address,
entity identity, path pointer and start time distinguishes active and completed
callbacks. The final native callback can apply its remaining displacement without
snapping to an unrelated movement goal; it then stops the override.

The builder temporarily disables world and entity collision during the authored slide,
restoring both after the original animation signal. The engine adds no teleport,
animation, script function, save field or package entry. Unknown executable bytes
leave both hooks disabled. Ordinary characters and unrelated paths keep native behavior.
The developer status command reports enablement and matching/scheduling/evaluation
counters, which can distinguish a merely assigned path from actual callbacks.

## Validation

The pure `xml2_ladder_paths_test` target checks exact resource identity, original call
ordering and return values, ordinary actors, inactive paths, final callbacks and
entity-generation changes. A Release DLL was exercised in separate owned-game builds.

Controlled original/converted script comparisons activated the authored spawners in
Sewers and Arbiter. Original soldiers stayed near z=157; converted soldiers descended
to the floor near z=0.04 through native callbacks. Two overlapping repeat Sewers
spawns also landed, restored collision and cleared their native timers. Screenshots
and raw samples remain local, outside source control and PR attachments.

These are isolated spawner tests, not full encounter traversal. Mid-descent save and
fresh-process reload, multiplayer, and alternate frame rates remain unverified.
