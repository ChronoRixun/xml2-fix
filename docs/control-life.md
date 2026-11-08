# XML1 control lifetime ranges

Legends Classic's two confuse chains switch from confused to team_switch at rank 6. The builder previously
split the kinds with life=0 on the inactive branch. Zero is an attachment without an expiry in XML2:
0x5383e0 returns an end time only for life > 0, and 0x5376b1 checks only positive end times. The builder now
gates application, preserving each XML1 duration range. Inactive application chances are negative, since
0x4e7b50 rejects chance < random and a zero draw must also reject an inactive branch.

XML1's 0x2a7b0 samples life + random * (life_max - life). XML2's powerup template and attachment initializer
0x537ba0 can express that sampling. Its inline combat-event override cannot: 0x4e7d44–0x4e7d74 samples
random * (max - min), omitting min, and attachment initialization can sample again against the template's
upper bound. Merely joining the two XML1 bounds into an inline life string changes the duration distribution.

With `[Game] MindControlLife=xml1`, control_life.cpp replaces only the CCEPowerup attribute-parser vtable slot
at 0x6912d4, guarded against the retail pointer 0x4e8240 and that setter's entry bytes. Its new attribute,
`xml1_life_range`, invokes the native life setter, retaining the template's range, then zeros the 12-byte
event override at CCEPowerup+0x24. Attachment initialization therefore samples the template exactly once.
The builder sorts attributes, so this marker follows ordinary life. Native mind-control duration defenses
still apply afterward in 0x5370b0; the listed bounds are the unresisted durations.
Every ordinary attribute delegates unchanged, and absence of the key installs no patch. A requested adapter
that fails a guard or transaction refuses startup. No executable file is modified.

The builder emits life plus xml1_life_range with the same value, writes the required INI key, and probes for
`xml1 template life range (parser revision 1)` before admitting a companion. The ordinary life is a finite
fallback; the parser extension is necessary to preserve XML1's randomized timing. This covers Frost Confuse,
Jubilee Photo Flash and the common life_max translation in Frost Fear.

Frost Confuse and Photo Flash both use 1–3/4/5/6/7 seconds at ranks 1–5 (confused), 2–10/11/12/13/14 seconds
at ranks 6–10 (team_switch), and 5–20 seconds at rank 11. Frost Fear uses 1–3/4/5/6/7, 2–8/9/10/11/12, 5–15.
These are ranges sampled per attachment, not fixed maximum durations.

The native fixture executes the installed bridge with an invented parser, verifies forwarding, rejection,
the exact override field and its neighbors, and checks both guards against an optional owned retail image.
With that image it also executes the original CCEPowerup setter and native team reconciliation/getter/setter
with synthetic actors. XML2's 0x474c80 compares the derived control flag against the current inversion and
calls 0x41a290 in both directions; XML1 does the equivalent at 0x74c30 for attribute 0x34. Removal at expiry
therefore restores the original team; the ordinary same-team attack filter (XML2 0x4eb280) then admits attacks
again. Retail XML2 power9 effects add damage/removal callbacks at expiry; those callbacks are not needed for
the first game's temporary team switch and must not be copied into the converted powers.
Run `xml2_control_life_test.exe <retail executable>` offline. Live expiry, hostility, attackability and a true
map transition still need coordinator checks. Integration must assign the new content/agreement identity;
this branch changes no release version or identity and must ship with the matching rebuilt content.
