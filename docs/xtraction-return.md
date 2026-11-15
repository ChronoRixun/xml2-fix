# Returning from Team Management

With XtractionShop=1, XML2 Fix1.11.0 preserves a supported return position when NAV chooses another floor or projects it horizontally into a certified floor gap. No map/save edits or LC rebuild are needed.

The snapshot has the correct point anchor. Native actor placement calls floor helper0x423cf0, which accepts a NAV projection before tracing only24 units above/below it. Interior Quarters projected all four heroes hundreds of units down. The sewer Healer point instead moved Iceman40 units sideways from supported floor into a gap. Both failures predate the stable preparation.

Two atomic guarded writes retain the restore scope at0x5f4504 and NAV slot0x689c38. Nonfinite output or vertical displacement beyond24 is refused as before. New horizontal refusal additionally requires exact caller0x423d48, validated actor bounds, a finite upward world-floor contact at the original point, unobstructed body clearance, and an equivalent projected-position query with NO HIT. Dynamic contacts, unavailable/exhausted queries, negative lower-Z bounds, malformed data or any uncertainty preserve native behavior. Other NAV callers and disabled XtractionShop are unchanged.

Every probe acquires a fresh actor-excluded query, copies contacts through native accessors, and releases before the next query and native tail. API guards accept retail bytes or the exact EntityCapacity-owned jump target. The existing failure branch retains inputXY; the unchanged collider tail selects contactZ+2. No global AI/pathing rule or map/hero identifier is added.

xml2_save_limits_test now executes the full retail acceptance/fallback/collider tail. Old code fails the horizontal-gap regression; the repaired fixture passes205 checks. Separate cases cover guard refusal, ownership mismatch, supported/dynamic/uncertain floors, body obstruction, negative bounds, query exhaustion, balanced releases, other callers, and the original height rule. TestMode integration passes525 checks. Full native matrix1055/1055 passes. Live final-image proof passes an actual hero replacement plus repeat at the sewer point, and replacement at Interior Quarters, all four alive.

Supervisor-assigned rule10 revision5 applies only with XtractionShop. Revision4 height-only peers and revision5 peers refuse each other in both directions; absent-shop revisions2/3 remain unchanged. Release-round reports retain exact binary hashes and test provenance. Broader campaign/online limits remain in KNOWN-ISSUES.
