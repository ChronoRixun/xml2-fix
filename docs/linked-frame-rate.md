# Host-owned session frame rate

Capture `0x558ec0` records elapsed time, gate `0x606a50` waits for every
participant, and consumer `0x559190` runs one shared simulation step with the
slowest recorded duration. Simulation does not run independently underneath
arbitrary rendering. Every peer needs the same pacing target; actual
throughput may still fall when a PC or connection stalls. The original
timing evidence is in [online-tick-rate.md](online-tick-rate.md).

The host freezes a numeric target at room creation/native advertisement:
the lower of its effective display pacing and its 30/60 TickRate ceiling.
Stock/absent FrameRate means 60; refresh resolves on the host; unlimited
uses the tick ceiling. FrameRate=144 with TickRate=60 hosts at 60;
FrameRate=30 hosts at 30. Numeric display caps 10..60 are retained exactly.

Guests adopt the target in memory. It overrides local FrameRate, menu pacing
and windowed VSync's refresh clamp, including in the room lobby and during
late admission. Synchronization never invokes display setters or writes an
ini. Leave, cancellation, failed admission and native teardown remove the
override. Deliberate option edits remain local; changing the session target
requires leaving and rehosting. Fullscreen VSync may physically block Present
below the target; that limitation is measured rather than resetting the device.

## Settings and admission

Rule 29 (TickRate), revision **2**, values `{1,1,10,60}`, agrees on this
host-owned contract. It registers for TickRate, GNS, a room preset or our
co-op menus. The zero-online-minimum/timer patch must verify; otherwise
admission stays closed. TickWait=spin cannot leave a hidden 30 fps guest
limiter underneath a 60 fps session. Revision 1 peers, including beta.1,
refuse in both directions before native admission. No release identity or
new gameplay-rule ID is assigned here.

The settings trailer is eight bytes: `F P S C`, wire `1`, fps u16 LE,
reserved zero. Only exact lengths and numeric rates 10..60 are accepted.

* Rooms reuse reliable **kind 12**, wire **2**: the existing generation,
  count and rows, then the trailer. Only the pinned room host can supply it.
  Map and settings validate before either is adopted. Rate adoption precedes
  map confirmation, discovery and admission. A conflicting rate refuses,
  even in a newer generation. A legacy roster, or no compatible roster
  within five connected seconds, refuses with our co-op settings/build
  notice and launcher status error. That generation stays refused even if
  a late roster arrives; Leave/retry opens a fresh one. The provisional
  broker-order fallback is removed.
* Direct-IP/LAN reuses the existing `0x18`, `0x1f`, `0x1a`, `0x1b` admission
  bodies: native body, unchanged 108-byte GCAP record, settings trailer.
  The largest queued body is 196 bytes (safety ceiling 236). Browsing never
  adopts a host's rate. Matching replies/follow-ups apply it **before**
  invoking native admission; hosts ignore a joiner's proposed value.
  Conflicting mesh replies refuse. Old parsers reject the extended length
  as well as the changed rule revision.

Room ownership retains the target across native cohort resets/loading and
clears explicitly on room teardown. Direct sessions also clear when their
native online flag falls. An override awaiting its first shared frame is
retained until native online state has actually been observed.

## Cards and measured capacity

Party report/table wire is **2**. The report appends local fps u16 (0
unlimited, 0xffff unknown), measured fps in tenths u16 (0 unknown, <=10000),
and capacity u8 (0 unknown, 1 holding, 2 slow) after the reserved hero word.
Reports are 28..124 bytes within the existing 128-byte cap. The table layout
is unchanged; each authenticated, sequenced member report supplies its own
timing claim. Expired reports clear rates and warnings.

Lobby cards show local/session rates and an override/capacity notice. Party
cards show rates and state, with a named slow-peer warning preferred over
the mismatch notice. These are authored co-op text fields, not native
dialogs. LC requires DLL capability `lc-online-linked-rate/1` so an old DLL
cannot silently leave them blank. The status-file schema is unchanged.

The work meter starts after the shared input gate (`0x606b98`, through the
existing join-in-progress bridge) and ends after successful Present,
**before** our deliberate limiter wait. Remote-input waits therefore do not
blame every PC. It samples only known gameplay screens. Pause/Party keeps
the last gameplay result readable while discarding partial windows; UI time
never counts as work. Loading/movies, a new target and stale gameplay samples
invalidate evidence. Each window spans at least
three seconds and thirty shared frames. Mean local work above 110% of the
frame budget, or over 20% of samples above that threshold, reports slow.
Two healthy windows clear a slow warning. Five seconds without gameplay
samples or continuing UI presentation expires the evidence.

This conservative measurement excludes pre-gate input work and cannot
diagnose network stalls. Without the join-in-progress bridge capacity stays
unknown/measuring. It never lowers the host's target automatically.

## Offline checks

The normal `xml2_test.exe` suite includes policy, late-join, both-direction
override/restore, old-peer and sustained-slow/recovery checks.
`xml2_linked_frame_rate_test` exercises the production pacer on invented
patch storage. `xml2_online_broker_test --roster-offline` drives production
room/admission ordering with fake transport. Agreement and menu fixtures
also map an owned retail executable read-only; they never launch the game.

Default CI DNS checks are unchanged. `XML2_FIX_TEST_OFFLINE=1` explicitly
skips and identifies two external OpenSpy DNS assertions, their log assertion,
and the pipe child's external `.invalid` DNS assertion. Report that gap for an offline run.
