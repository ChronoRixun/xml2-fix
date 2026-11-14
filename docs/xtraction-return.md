# Returning from Team Management

With `XtractionShop=1`, XML2 Fix 1.11.0 keeps a side-mission return's requested
position when the navigation grid suggests a different floor. It targets the
party dying below Interior Quarters after leaving Team Management, including
leaving without changing heroes. Offline live proof passed twice at Interior Quarters, including a hero replacement, and in Command Center and Lounge Deck A. The wider story-dependent census remains incomplete.
The map and saves need no edits or LC rebuild.

The snapshot has the correct Xtraction anchor. Native snapshot actor placement
then calls the actor floor helper (`0x423cf0`). It accepts a NAV projection and
traces only 24 units above and below that projection. A navigation height
hundreds of units below the requested floor can therefore move the whole party
below the level. Skipping that final helper in an owned diagnostic process kept
the party at the expected formation, alive.

The fix wraps the side-pop's actor restore call at `0x5f4504` with a thread-local
scope, and the navigation manager's query slot at `0x689c38`. During that scope,
a successful query with nonfinite output or a vertical displacement beyond the
native trace span returns failure. The floor helper already handles failure by
using its requested position before its normal floor trace. Same-floor results,
native failures, other navigation callers, and disabled `XtractionShop` retain
their existing behavior. The XML1 floor helper has the same input fallback and
24-unit floor trace.

Both writes are one guarded transaction. The caller, query ABI, return cleanup,
fallback branch, slot target and trace-span constant are checked. A clean refusal
leaves native placement and marks the requested feature unverified; an
uncertified rollback refuses startup.

`xml2_save_limits_test` executes the retail NAV call, acceptance branch and
selected-position copy with synthetic data. The separate collider tail is
replaced in the fixture. The unsafe-height case fails without the return filter;
disabled, guard-refusal, native-failure, other-caller and boundary cases are
checked as well. `XML2_FIX_TEST_RETURN_BASELINE=1` runs the native regression
without installing the filter and is expected to fail its safety assertion.

Stable integration assigns rule 10 revision 4 whenever XtractionShop is requested. All peers
need the update; beta.9's placement rule is refused in both directions. Native agreement
regressions cover matching-new acceptance, old/new refusal and unchanged absent-shop behavior.
The wider runtime point census and the stable release smoke are tracked by the release round.
