# XML1 shared handler rules

These opt-in `[Game]` keys restore the listed shared callback behavior for an
XML1 content installation. Each accepts `xml1` or `xml2`. Absent, empty and
`xml2` keys leave the retail callbacks byte for byte unchanged. Selection applies
to all uses of that handler in the installation; these keys do not infer an
actor's content origin. Enable them only for an XML1 campaign/profile.

| Key; mask bit | Handler and PC sites | XML1 behavior restored | Live acceptance check |
|---|---|---|---|
| `SkatingRule`; 1 | `ch_skating`, `4ec870`, `4ecf80` | Forward 400; vertical correction +/-48; lower thresholds 36/48, ceiling 120. Bypass PC's low-speed/time exit and its elapsed-time accumulation. Remove the PC entry/exit writes to coefficient `+434`. Constants are private operands, never edits to shared retail float data. | Iceman ice slide and Magma traversal: sustained low-speed traversal, speed and height correction near ledges/uneven ground; enter/exit repeatedly. |
| `TeleportLandingRule`; 2 | `ch_move_tele_land`, `4f0010` | Always choose among four animations, regardless of actor name. Match XML1 animation **names** `ea_power2/3/4/6`: XBE IDs `87/89/8a/8d` map to PC `75/78/79/7e`. PC's old `7c/7e/7f` choices have different names. | Nightcrawler teleport-jump landings, all four variants, including copied/shared styles on another actor. Confirm converted skeletons have the original named animations. |
| `PopupRule`; 4 | `ch_popup` and `ch_air_spin`, shared decision `4f0f00` | No-op decision, preserving PC callback's two stack arguments. Remove PC's rising-above-72 vertical velocity correction. | Shared popup/air-spin launches and falling arcs, especially a victim rising above 72 units. |
| `PopupLandRule`; 8 | `ch_popup_land`, `4f0fb5` | Unconditionally reset actor `+278` to 1. No stats pointer or bit-80 gate. Preserve native landing flags. | Land after popup/air-spin with a hero and NPC; verify movement returns to normal, including actors whose stats have `+2ae` bit 80. |
| `ChargeRule`; 16 | `ch_charge`, `4ebe20/4ebe50/4ebe70`, update slot `6937b4`, event insertion `4ec02f` | A positive duration expires strictly after its deadline through action **0x18** (24 decimal), both input/pending and update/immediate dispatch. Restore event 100's optional positional `CCESound`, validity/position/stop lifecycle in initialized actor `+73c`, and per-update event 200 after contact processing and before motion. Retain PC's contact body and one-second cadence. | Advanced Sentinel charge: finite-duration expiry into authored action 0x18, looping sound follows movement and stops on exit/cancel; event-200 authored effects update once per frame. Test interruption, repeated entry, and duration <=0. |
| `NightcrawlerRule`; 32 | `ch_nightcrawlerdecide`, `4f04f3`, `4f0640` | Remove PC game-state exclusion. Eligible ally target is same team regardless of controller, or neutral with AI eligibility virtual `+78` true. XML1 team 1a maps to PC 1c; XML1 target AI token `+5b4` maps to PC `+7dc`. Retain surrounding target/query and actor-state gates. | Nightcrawler buddy teleport with a controllerless ally and an eligible neutral NPC; reject an ineligible neutral or enemy. Check a competitive/Danger Room state separately. |
| `GambitRule`; 64 | `ch_gambitdecide`, `4ed5d7` | Add the forward 60-unit box trace after held-object selection and before fallback. Exclude player, invalid token, actor, nonphysical, weight byte >=10, and the mapped exclusion affecter (XML1 39 -> PC 5c). Resolve action 0x19 for an eligible object; retain the native held-object switch flag. Do not substitute `ch_gambitdecide2`, which tests different eligibility. | Gambit and Jubilee object charging: objects in front at 60 units, weights below/at 10, blocked/protected objects, actors, and no object; also enter with an already held object. |

All selected parts install as one `xml1_hook_transaction`, with guards, page
protection acquisition, readback and rollback. A bad selected key or any selected
guard refusal changes no callback and is unverified for online play. An unsafe
rollback refuses startup. Trace entry guards accept retail bytes or only the
exact target published by the entity-capacity installer, so raised query owners
remain usable and foreign hooks refuse.

Online agreement rule **42**, revision **1**, records `{installed_mask,0,0,0}`.
It registers only if one of these keys requests XML1 or is invalid. Absence/native
therefore preserves the released OFF digest. Both peers must select the same
mask and have a verified installation. ID 42 is coordinator-assigned; IDs 31..41
are assigned or reserved for other work.

## Native verification

Build with MSVC Win32 Release, below normal priority, then run:

```powershell
build/bin/Release/xml2_shared_handlers_test.exe <retail-XMen2.exe> <original-default.xbe>
```

The test requires these SHA-256 identities:

* PC: `146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b`
* XBE: `2ea531f11e0b5b7ca651485012b871ef99a5099c566a6d7cb8ec327ee73d62ac`

It checks every retail guard, each guard refusal, invalid/base/OFF paths, exact
write footprint, effective agreement mask and peer mismatch. An optional third
argument, an integer mask 1..127, isolates installation of any combination.
Without it, the test also executes owner-loaded original callback slices next
to the production bridges with synthetic dependencies: popup reset/no-op,
all four landing choices and animation-name registry mapping, skating boundary
velocities and low-speed retention, charge duration/sound/event lifecycle,
Nightcrawler team/AI eligibility, and Gambit probe geometry and eligibility.
Original code is read at runtime, uniformly relocated into the test's owned
allocation, with only the known absolute data references relocated and external
calls replaced by synthetic dependencies. No game instructions or assets are
embedded or distributed as a runnable copy.

The Gambit probe also tests a fractional direction that distinguishes XML1's
x87 intermediate precision from an ordinary SSE float expression. Its X product
remains extended until the origin addition; Y/Z products are stored first.

These are static/native proofs for the tested callback slices. Downstream
physics, animation availability, audio resources, event consumers and live
campaign integration still require the acceptance checks above. No game or
emulator is launched by this test.

## Deferred design notes (no patches)

These are the original rule42 audit notes. Subsequent rule46 work implements
telekinesis and guard through the installed rule45 lifecycle; see
[the coupled contracts and remaining live checks](tk-guard.md).

* `ch_move`: identify stats `+2b0`, its initialization, parser and all effective
  controller-assigned values before removing PC's animation-rate division at
  `4f01c5..4f01da` (XML1 `de7f2..de7f9`). A default-only guess is insufficient.
* `ch_idle`/`ch_bored_loop`: reconcile stun ownership with rule 18, controllerless
  team categories 1e/1f, the additional stats-bit gate, and game-state selection.
* Grab lifecycle (`ch_grab_victim`, `ch_grab_throw_ally`, `ch_pickup_grab`): trace
  paired ownership flags, grabber move-prefix checks, early refusal and release,
  including exit/cancel. Do not remove only the held-object refusal.
* `ch_lockon`/`ch_lockedon`: identify consumers of XML1 notifications 13/14/15
  before restoring them. This is separate from existing visual lockon effects.
* `ch_telekinesis`: verify consumers of lift offset 48/120 and callback-phase
  ownership (XML1 input processing versus PC decision processing) together.
* `ch_fly_move`/`ch_flying`: reconcile global-state/actor-flag idle cancellation
  with PC's separate flight-entry game-state gate.
* `ch_guard_decide`: trace the XML1 output flag consumer and moved interaction
  dispatch. Its short entry is not evidence that PC interaction selection can
  safely be removed.

Evidence: parity audit section 1; PC ranges listed above, XML1 `daae0/dac50`,
`de3c0`, `15fdf0`, `defa0`, `d91e0..d94e0`, `dbc05..dbc3c`, and
`db70a..db89c`. Native sound mapping is independently visible in PC
`4eec97..4eed4f`; actor initialization sets `+73c=-1` at `4234b3`.
