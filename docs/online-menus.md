# Online menus: round 2 DLL bridge

This is a guarded presentation bridge, pending Claude review and real-game
acceptance. Host fixtures do not establish fresh-boot stability, rendering or
input parity. No broker operation, native hosting/admission, clipboard, name
editor or lobby replacement is introduced in this round.

## Runtime contract

The exact opt-in is `[Online] Menus=1`. Missing, zero or other values retain the
old behavior. Play Online keeps `openmenu online`; it must not become a call to
an optionally unregistered script function. A refused guard/transaction/capacity
leaves that native command available. An uncertifiable transaction rollback
refuses startup, following the existing transaction policy.

The manager request for `online` is redirected only from `main`, after a real
native Accept supplied controller 0..3. The adapter at `0x5c971b` forwards the
native controller setter and captures EBX's Accept bit (0x10). Navigation alone
does not arm entry. The captured controller must still match the manager's
signed selected controller and be no older than one second. A request without
that selection leaves main available for real Play Online acceptance. Launcher
presets cannot supply or guess a controller through this API.

Entry first checks the builder capability/assets and native singleton classes,
then calls, in order:

1. Seats `0x48d5e0(p)`.
2. Network `0x615dc0()`; require success.
3. Session screen name `0x610d80(name)`; at most 31 printable game bytes.
4. Session `0x613ba0()`; require success.
5. Campaign networking `0x60a4f0()`; require success.
6. Network `0x60a770(1)`.
7. Profile name `0x48f3f0(name)` (profile vtable +0xb4), then the constant
   `runscript saveloadProcess(2)` through console run-now +0x18 (`0x55beb0`), as
   stock Ready does. Finally request `lc_online` through the original manager.

An existing profile name is preserved. Empty names get `Player`; oversized or
nonprintable names refuse custom entry and retain the native screen. A second
request before the Hub appears cannot initialize again. Hub/child navigation
does not initialize again; Back to main followed by a new real accept does.
The stock native initializers retain their own idempotency and failure state.

Five hooks install together: six bytes at `0x5c971b`, manager request slot
`0x6a23d8`, base CMenu tick slot `0x69e454`, close slot `0x69e464`, and
the native nonempty usecmd queue call at `0x5bc3d0` (23 bytes total). The main-menu tick/mouse hooks owned by ClassicMainPanelBounds remain
composable. Base menus never receive native ONLINE_MENU's extended callback
dispatcher. There is no dependence on a network pump, a room, `wanted` or
`key_loaded` for UI dispatch or initial entry.

## LC integration after round 1 lands

Do not enable this from the round-1 fixture manifest alone. Add the following
command/capability selection to the builder's frontend, frontend_selftest and
validator, with focused synthetic tests and its owned-input build gates:

* Retain the production Play Online command `openmenu online` and its native
  main-menu role/mouse slot. Require a verified matching DLL capability and
  complete resources before emitting `[Online] Menus=1`. Extend fix_ini.py's
  exact key/capability checks and script_functions.py's signature/capacity
  inventory for the additional real `lcOnlineIntent n(i)` registration. The
  DLL contains the API marker and function name; validate those together with
  the exact Online/Menus key support, rather than using a release number alone.
* Emit `Data/lc_online_menus.cap` with exactly `lc-online-menus/1` plus one LF
  and no BOM. This is runtime asset metadata, not a release identity. Require
  paired `UI/menus/{lc_online,lc_host,lc_code,lc_browse}.{XMLB,engb}`, their
  `.IGB` scenes and
  `Packages/generated/maps/package/menus/<name>.PKGB`. Preserve the round-1
  package/texture/focus closure and approved composition.
* Select only constant, authored commands from the following mapping. Do not
  interpolate names, room codes, clipboard data or display text into commands.

| Intent | Command | Scope |
|---|---|---|
| Host Setup | `runscript lcOnlineIntent(2)` | Hub |
| Code | `runscript lcOnlineIntent(3)` | Hub |
| Browse | `runscript lcOnlineIntent(4)` | Hub |
| Other ways | `runscript lcOnlineIntent(5)` | Hub; opens full native `online` chain |
| Back | `runscript lcOnlineIntent(6)` | Hub -> main; child -> Hub |
| Unavailable action | `runscript lcOnlineIntent(7)` | Visible local preview explanation |
| Dismiss | `runscript lcOnlineIntent(8)` | Hide the inline explanation |

Value 1 is reserved and rejected by the script dispatcher. There is one real
script registration, `lcOnlineIntent`, return `n`, arguments `i`. Add no direct
`setuphost`, `startgamedialog`, code resolution, admission or broker commands.
Name/Rejoin/Party/Host settings/Open room/Paste/code edits/Refresh/Join are
future rounds; show their preview state or map to 7 without implying success.
Remove synthetic Connected/recent-game/room-success claims from capability
output. Browse/Host/Code may retain clearly labelled offline preview fixtures.

* Wire native `menu_cancel` to intent 6 on every screen; the default CMenu Back
  closes instead. Round 2 Back from Code returns to Hub. Code editing and
  delete-first Back belong to round 3. Do not claim unimplemented keyboard/pad
  shortcuts merely because a prompt contains a device token. Verify one native
  Accept maps to one posted action for each actual input path.
* Retain Hub `local_name`, `service`, `last_name`, `last_code`, `browse_hint` and
  child `self_name`/`self_state` items. Add `lc_inline_error` to every menu with
  a corresponding scene anchor, initially hidden, sized for the preview error
  text. These are direct text bindings; attach no competing gamevar getter.
  Use text items with the normal interned-text storage, not the 31-byte buffer
  mode at item +0xa0. Runtime checks the native text setter/hide slots before
  binding and reacquires items each tick.
* Update frontend_selftest's PLAY_ONLINE_CMD expectation deliberately while
  retaining native role checks, unrelated native text/modes and the native
  fallback packages. Run the validator, frontend_selftest and owned-input
  pipeline with the final DLL. Fixture commands alone cannot emit the marker.

The exact authored commands above are consumed at the native item usecmd queue
boundary, before a delayed script could outlive its accepting menu. Other
commands retain the original native queue. The console vtable stays retail,
so existing postgame and lazy test-input guard checks remain valid. Do not add whitespace, terminators
or extra statements to those command constants. The registered script handler
also remains available for scoped direct calls.

The UI mailbox accepts one action per tick, bound to the live source menu and
screen. Transition/close clears it, including address reuse. Binding occurs only
after the native tick and a second current/active-menu check. Text is bounded,
renderer controls are replaced, and cdecl `0x5adcf0` always receives the literal
format `%s`. `0x5ade10(...,true)` hides; false shows.

## Capacity and agreement

Requesting menus selects the existing automatic ScriptFunctions capacity
(1024) when there is no explicit limit, even without ForcedTeams. The dispatcher
occupies one actual slot in the existing overflow registry after its lookup
hooks and the complete UI hook transaction have installed. Explicit capacity
320 or unavailable/full overflow storage refuses the UI installation. Rule 14
still records effective installed capacity, revision 1; menus request its
registration before the agreement freezes. No ID, rule revision or release
identity is allocated here. A new peer-visible semantic entry requirement
**needs a rule id** from the coordinator.

## Host verification

Configure with Win32 MSVC and the existing GNS static package, build
`xml2_online_menus_test` and `xml2_fix`, then run:

```powershell
python scripts/check_online_menus_guards.py <retail-XMen2.exe>
python scripts/test_online_menus.py <bin>/xml2_online_menus_test.exe <retail-XMen2.exe> <workspace-output>
```

The host maps the owned exact PE read-only from disk into its reserved process
image and supplies invented services. It exercises the production installer,
actual callback adapters, registration lookup, UI tick and close, failure
ordering, stale binding and constant-format escaping. It is not a game boot.
Every guard has an independent negative mode; the production contribution is
also used for protection/flush/readback/rollback fault injection. Run the full
native suite, full-INI union and an OFF comparison against exact b603cc8 before
integration. Keep all logs, mapped bytes and owned assets outside the repository.

## Ten fresh-boot live acceptance (coordinator scheduled)

This procedure is pending and was not executed during round 2.

Prepare the final reviewed DLL and capability-selected builder output in owned
copies of `_candidate_040f`; merge the separately reviewed NoActivate support
and W2 crash fix as applicable. Check the actual final DLL supports
`[Test] NoActivate=1`, then set it in every test-copy INI. Preserve identities;
record the final DLL/EXE/assets/INI hashes. Re-run the host gates after merging.

Before **each** boot:

1. Check `game-slots/HOLD-GAMES` and every XMen2.exe outside agent-work. If either
   exists, wait/poll every two minutes and do static work. Use
   `game-slots/slot.py --wait` for the actual launch, following its current CLI.
2. Export the Activision registry key into the external evidence folder and
   ensure no previous test game/helper is alive. Use a fresh game process and
   normal EXE entry; never a skipped-main-menu or injected-menu fixture. Start
   on a private/never displayed desktop, or use explicitly authorized background
   input/capture only. Never focus/activate/restore or drive the owner's desktop.
3. Use `Menus=1`, the correct marker/assets, and no launcher Host/JoinCode/broker
   preset or one-shot key. Broker configuration alone must create no room.

Use this fixed boot matrix (4:3 and stretched 16:9 use the same approved UI
content; captures include a 720p readability pass and 1080p comparison):

| Boot | Initial accepting device | Aspect | Variant |
|---|---|---|---|
| 1 | Keyboard | 4:3 | Enabled |
| 2 | Pad, controller 0 | 16:9 | Enabled |
| 3 | Mouse, Play Online hit rectangle | 4:3 | Enabled |
| 4 | Keyboard | 16:9 | Enabled |
| 5 | Pad, controller 1 | 4:3 | Enabled |
| 6 | Mouse | 16:9 | Enabled |
| 7 | Keyboard | 4:3 | Enabled |
| 8 | Pad, controller 2 or 3 | 16:9 | Enabled |
| 9 | Mouse | 4:3 | Enabled |
| 10 | Keyboard | 16:9 | Enabled |

For **all ten** boots: wait for normal main initialization, accept Play Online
with the listed real device, and require a usable Hub with the captured actual
controller logged. Confirm exactly one initialization sequence before Hub and
no premature room/broker work or allocator fallback fault at `0x5517db`.
Traverse Hub -> Host/Code/Browse -> Back, then Hub Back -> main -> real Play
Online accept -> Hub; this second entry must perform exactly one new sequence.
Try keyboard -> pad -> keyboard focus/prompt transitions, mouse row edges and
Back. Confirm live names/error text and safe bounds. Compare Hub captures with
approved `01-hub.png` at both aspects; annotate only approved rev-2 exceptions.

Before Other ways, capture the native input manager's selected-controller
field +0x34 after the Hub appears; require it matches the real accepted p.
This checks the bridge prerequisite before stock ONLINE_MENU can rebind it.
In every boot, take Other ways and verify the native online name/profile/save
flow reaches campaign_lobby and its native Host/Join service menus. Through
native Game Options, choose the native campaign/difficulty/name/player-count
options and Post Game, then require the actual native Host lobby without the
historical allocator fault. This creates no broker room; leave through native
Leave and return through the native Back chain without starting a campaign.
Record that Other ways reinitializes through stock ONLINE_MENU: it does not
certify a future direct custom Host Setup route (round 6). After exiting,
stop every helper/process, restore/export
the registry and verify byte-identical equality with that boot's snapshot.

Add **separate** fresh native-fallback boots (they do not replace any of the ten):
key absent; key zero; marker absent; deliberately wrong capability marker;
and one safely injected local guard mismatch before DLL installation through
a coordinator-authorized harness. Confirm native Play Online still works and
no partial dispatcher is advertised. Never patch the owned original EXE for
this test. The host's guard mutation does not count as the real-game fallback
observation. Also test a launcher preset before selection: it must wait for a
real accept and never manufacture controller 0.

Record one row per boot: hashes, launch route, accepting controller/device,
aspect/resolution, guard/registration/capacity logs, first and second init
counts, Hub/Back/Other-ways result, no-broker/no-crash result, capture paths and
registry equality. Any failure leaves the ten-boot gate pending; repeat the
complete matrix after changing entry code. Delete only the created test game
and matching profile folders, by checked absolute Python paths. No live gate
is waived by a fixture pass; no 0.6-ready claim follows from this round alone.
