# Online menus: round 4 native lobby bridge

This is a guarded presentation bridge, pending Claude review and real-game
acceptance. Host fixtures do not establish fresh-boot stability, rendering or
input parity. Round 3 adds owned native name polling, Code input/clipboard
and local Rejoin storage. Resolve ends at an explicitly offline fixture response;
no broker operation or native hosting/admission is introduced. Round 4 adds
opt-in presentation adapters around the real native host/join classes; they
remain current and own their roster, callback, modes and actions.

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

Availability is checked BEFORE the capture gate. Missing mod-loader generation,
wrong/missing marker or incomplete resources forward to native `online` even
without a fresh capture. A successful asset check is cached only for that mod
index generation; exceptions fail closed to native entry. With resources ready
but no capture within 1000 ms, show the builder-owned localized main item
`lc_online_continue` ("Press Accept to continue"). If that text item is absent or
unusable, forward natively rather than swallow the request silently. A fresh
accept hides the hint. The queued Hub lease lasts 2500 ms and is tied to its
source main menu; only actual live Hub arrival, explicit navigation from that
source, refusal or expiry clears it. Popup/resource closure and unrelated custom
ticks cannot re-allow duplicate initialization. A failed Hub open can retry
after expiry. Host clocks test 1000/1001 and 2499/2500 ms, including wraparound.

An existing profile name is preserved. Empty names get `Player`; oversized or
nonprintable names refuse custom entry and retain the native screen. A second
request before the Hub appears cannot initialize again. Hub/child navigation
does not initialize again; Back to main followed by a new real accept does.
The stock native initializers retain their own idempotency and failure state.

Seven hooks install together: six bytes at `0x5c971b`, manager request slot
`0x6a23d8`, base CMenu tick slot `0x69e454`, close slot `0x69e464`, and
the native nonempty usecmd queue call at `0x5bc3d0`, plus scoped base Back
slot `0x69e440` and base deactivation slot `0x69e438` (31 bytes total). The main-menu tick/mouse hooks owned by ClassicMainPanelBounds remain
composable. Base menus never receive native ONLINE_MENU's extended callback
dispatcher. There is no dependence on a network pump, a room, `wanted` or
`key_loaded` for UI dispatch or initial entry.

## LC integration contract (rounds 2 and 3)

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
  Round-3 command selection additionally requires the exact `lc-online-input/1`
  DLL input API probe; round 2's generic marker/function pair alone is insufficient.
  This additive API marker is not a release identity or gameplay rule.
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
| Back | `runscript lcOnlineIntent(6)` | Hub -> main; Code deletes one symbol until empty, then Hub; other children -> Hub |
| Unavailable action | `runscript lcOnlineIntent(7)` | Visible local preview explanation |
| Dismiss | `runscript lcOnlineIntent(8)` | Hide the inline explanation |
| Change name | `runscript lcOnlineIntent(9)` | Hub; native `text_entry` |
| Paste | `runscript lcOnlineIntent(10)` | Code; whole validated clipboard value |
| Delete | `runscript lcOnlineIntent(11)` | Code; delete last symbol, stay when empty |
| Clear | `runscript lcOnlineIntent(12)` | Code; erase entire value |
| Done | `runscript lcOnlineIntent(13)` | Code; explicit complete-code fixture retry |
| Rejoin | `runscript lcOnlineIntent(14)` | Hub; load local remembered code, resolve afresh |
| Symbol | `runscript lcOnlineIntent(32)` through `(62)` | Code; alphabet index 0..30 |

Value 1 is reserved and rejected by the script dispatcher. There is one real
script registration, `lcOnlineIntent`, return `n`, arguments `i`. Add no direct
`setuphost`, `startgamedialog`, code resolution, admission or broker commands.
Party/Host settings/Open room/Refresh/Join remain future rounds; show their
preview state or map to 7 without implying success. Value 15 is an internal
invalid-key feedback action; values 16..31 and values outside 2..62 are rejected.
Remove synthetic Connected/recent-game/room-success claims from capability
output. Browse/Host/Code may retain clearly labelled offline preview fixtures.

* Wire native `menu_cancel` to intent 6 on every screen; the default CMenu Back
  closes instead. The scoped base Back bridge intercepts custom menus before default close or
  menu_cancel execution. The Code grid has 31 symbols and Delete in cell 32. Do not claim unimplemented keyboard/pad
  shortcuts merely because a prompt contains a device token. Verify one native
  Accept maps to one posted action for each actual input path.
* Retain Hub `local_name`, `service`, `last_name`, `last_code`, `browse_hint` and
  child `self_name`/`self_state` items. Add `lc_inline_error` to every menu with
  a corresponding scene anchor, initially hidden, sized for the preview error
  text. Dynamic data uses direct text bindings; attach no competing gamevar getter.
  Use text items with the normal interned-text storage, not the 31-byte buffer
  mode at item +0xa0. Runtime checks the native text setter/hide slots before
  binding. A menu-scoped value cache avoids repeated setters; normal deactivation
  and resource cleanup invalidate it, including reuse of the same address.
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
display data is allowlisted to ASCII letters/digits, space, hyphen, underscore,
period, apostrophe and comma (caret and backslash are excluded), and cdecl `0x5adcf0` always receives the literal
format `%s`. `0x5ade10(...,true)` hides; false shows.

## Native name request and Code model

Hub intent 9 owns tag 0 through one native keyboard request. It drains any
already-ready result before setting tag (`0x60b990`) and maximum 31
(`0x60b970`), then passes fully zero-initialized 256-byte seed and 64-byte title
buffers to `0x60c6e0` / `0x60c710`. The title is resolved text, "Screen name".
Opening `text_entry` retains its native layout, names, keyboard filters and
return navigation. No callback or caller-owned destination is invented.

The manager request is queued. A still-current Hub before its close is not a
cancelled keyboard: the normal deactivation hook (+0x1c / `0x5ace30`) records departure before return polling is
allowed. A request that never departs releases ownership after five seconds,
without waiting. Resource cleanup (+0x48 / `0x5af5a0`) is distinct and cannot
mark departure or erase a pending Rejoin seed while constructing a noncurrent
Code menu. Normal Code deactivation invalidates lookup even when resources
remain cached. Both hooks preserve the native originals. On Hub return, ready/tag are polled; owned tag-0 results are
consumed at `0x60c780` and all 256 bytes copied immediately. Missing, unchanged,
blank, unterminated or >31-byte results never change the name. Invalid accepted
results show a length/encoding explanation. A foreign tag is left for its owner.
Leaving to another route cancels ownership and drains only our tag-0 completion.
A no-result return resets our tag to sentinel 4. A changed valid name calls
session `0x610d80`, profile `0x48f3f0`, and the original fixed persistence command.

Code alphabet is exactly `ABCDEFGHJKMNPQRSTUVWXYZ23456789` (31 symbols).
Intent `32+i` appends alphabet symbol i. Delete is intent 11; the fourth row's
last cell is Delete, not a 32nd symbol. The model holds at most eight symbols.
Render eight named items `cell0`..`cell7` and a fixed dash after cell 3; empty
cells bind an empty string. `code_value` is an optional combined diagnostic
binding, not an extra visible field. Retain `code_status` for status text;
`code_remaining` is optional numeric data; `code_state` stays builder-owned. The DLL enables item `done` only when complete and
Hub item `rejoin` only when a valid local record is present. Preserve closed
four-direction focus links with no updownonly restriction, including all tools.

DirectInput keyboard input uses the existing device-read adapter, also when the
automation pipe is off. It does not change cooperative level or enable that
pipe. Letter/digit scancodes implement the alphabet; ambiguous 0/O/1/I/L show a
hint. Ctrl+V Pastes, Backspace/Delete delete one, Shift+Delete clears, and Enter
(including keypad Enter) is Done. Reserved tool keys are consumed in Code so
Enter cannot also accept the focused grid cell. Arrow navigation, pad Accept
and mouse cell/tool clicks retain the native item path. The edge sampler sees
out-of-scope reads without posting actions or changing their state: held entry
keys do not leak and the first fresh typed key is not discarded. Failed device
reads re-prime without generating an input edge.

The native controller mask is rising edges (`0x550b70`, current +0x2f8 against
previous +0x374). Scoped handlers implement `$MENU_NEXT` (Y) Paste and
`$MENU_OTHER` (X) Clear in Code, `$MENU_OK` (Start) Done; in Hub, MENU_OTHER
changes name and MENU_NEXT Rejoins. Direct keyboard text wins over coincident
native X/Y bindings. Every route uses the same one-slot, menu-bound mailbox.
Back (pad/keyboard/clickable prompt) bypasses default base close: nonempty Code
deletes one symbol; empty Code returns to Hub. One held press cannot delete
and close together. The native handler is forwarded for unrelated base menus.

Paste tries OpenClipboard once, prefers CF_UNICODETEXT, and never sleeps,
retries or reads user text into a command. It accepts at most a 4096-byte even
allocation, scans at most 257 UTF-16 units for NUL, and rejects non-ASCII,
malformed/unterminated text and text over 256 characters. Trim only outer ASCII
space/tab/CR/LF, accept an optional dash only after four symbols, normalize
lowercase, then validate the ENTIRE eight-symbol value. No prefix truncation.
Failure preserves current Code text. Every successful lock is unlocked and
every successful open is closed, including validation failure.

Every edit increments a generation. Eight symbols cause one automatic fixture
request for that generation; Done explicitly retries. A later UI tick receives
the invented reply, after applying that tick's edit/cancellation. Stale replies
cannot mutate another value or reopen a closed menu. Status explicitly says
"Offline fixture response. No room was joined." No broker/network call is made.

Rejoin stores only canonical code, fixture room name and UTC Unix seconds in
`%LOCALAPPDATA%/XML2 Fix/online/last-game.txt`. The bounded `lc-rejoin/1` record
contains no address, credential, save or participant identity. It is outside
all game/mod/builder packages, on this PC only; UNC/device paths and non-fixed
drives are rejected before file access. A malformed/oversized record disables
Rejoin. Round 3 remembers an honestly labelled offline fixture, never successful
admission. Rejoin always opens Code and resolves afresh. This record is not
shared party recovery state, and must never be copied into an LC output.

LC must wire the exact new commands above to Change/Rejoin, the 31 alphabet
cells, Delete/Paste/Clear/Done, and retain the named cell/status/tool items.
Keep `lc_inline_error` on each screen; use normal interned text with no gamevar
owner. Add optional `last_time` if showing the raw remembered timestamp; the
current `last_code` binding is canonical code, not a fabricated relative age.
Keep offline fixture wording in service/recent-game panels. Do not add Copy,
Refresh, lobby or Host shortcuts before their own handlers exist. Update the
frontend command inventory, validator and selftest for these exact integers,
and run the focused LC tests plus the full owned-input pipeline with this DLL.
No LC file was edited by the round-3 DLL task.

## Localized presentation and frame safety

DLL bindings contain data only: local/recent names, code/cells, remaining count
and optional timestamp. The DLL does not replace localized `service`,
`self_state`, `browse_hint`, `code_status` or generic error text with English.
Visibility/enabling are cached by value as well as text. All caches discard
menu identity on deactivation/resource cleanup; they never retain item pointers.

LC must retain its localized incomplete-code `code_status`, add localized
`lc_code_complete` (complete / Done-retry guidance) and `lc_recent_empty`
(no local recent fixture), and add initially hidden, normal interned text nodes:

| Node | Localized English meaning / scope |
|---|---|
| `lc_online_continue` | Press Accept to continue; main, same item name and scene anchor |
| `lc_notice_unavailable` | This action is unavailable in this preview. Use Other ways to play; custom screens |
| `lc_notice_resolving` | Resolving offline fixture; Code status card |
| `lc_notice_fixture` | Offline fixture response. No room was joined; Code status card |
| `lc_notice_paste_invalid` | Paste needs eight code symbols; no 0/O or 1/I/L; Code |
| `lc_notice_code_invalid` | Use code symbols only; no 0/O or 1/I/L; Code |
| `lc_notice_name_invalid` | Screen name must be 1 to 31 printable bytes; Hub |
| `lc_notice_profile_save` | Name changed; profile save was refused; Hub |
| `lc_notice_name_open` | Name editor did not open. Try again; Hub |

Localize these in the builder's paired resources and use the appropriate native
Accept glyph for the main hint. Put mutually exclusive notice text at existing
inline/status anchors, preserving the approved layout rather than adding a
popup. Missing specific error text uses builder-owned `lc_inline_error`; no
English string overwrites it. The `lc_code_complete` and `code_status` nodes are
mutually visible according to completion/notice state. `last_name`, `last_code`
and `last_time` are hidden without a valid record; `lc_recent_empty` is shown.
No relative age or successful admission is invented.

Request, asset validation and tick boundaries contain C++ allocation/filesystem
exceptions. Request failure forwards natively; tick failure preserves valid
Code text, invalidates pending replies and prevents automatic retry spinning
until an edit or explicit Done. Native tick runs at most once per frame.
The queued-command matcher uses a fixed stack buffer, no allocating formatting.

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

`scripts/run_native.py` and `scripts/test_online_menus.py` share one complete
mode inventory: OFF/zero, every guard mutation, rollback/protect/flush/retain,
failures, review and round-3 cases. The native runner gives each mode a distinct
log tag. `python scripts/test_run_native.py` checks its side-effect-free list and
inventory. `scripts/test_online_menus_startup.py` additionally executes the real
DLL startup against the repo's `xml2_union_host` target, testing the actual
ScriptFunctions/exports gates with no ForcedTeams or LC assets:

```powershell
python scripts/test_online_menus_startup.py --dll <dinput.dll> --host <union_host.exe> --retail <owned-XMen2.exe> --engine <owned-engine-dir> --out <new-workspace-output>
```

It runs on a private never displayed desktop, maps the PE without calling its
game entry, compares registry exports, and checks hook writes plus rule-14 logs.

## Ten fresh-boot live acceptance (coordinator scheduled)

This procedure remains pending; no game launch was performed during rounds 2 or 3.

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

## Additional round-3 live traversal (coordinator scheduled)

After the complete round-2 ten-boot gate and LC/font integration, run a fresh
normal boot through the changed Hub/Code route under the same launch, slot,
registry and NoActivate discipline above. Record final DLL/assets/INI hashes.
Change the native name at 31 bytes, accept and verify the profile after restart;
verify the keyboard cannot silently submit 32 bytes. Cancel with Esc/pad Back
and close without a result: the original name and profile remain unchanged.
Return repeatedly to the Hub and verify no stale name/tag is adopted.

Type Code directly; select all 31 cells and Delete with pad/mouse, test arrows
and tool navigation, Enter Done, Ctrl+V, Delete/Backspace, Shift+Delete, pad Y
Paste and X Clear. A pad/mouse grid accept appends one symbol. Test valid full
and dashed/lowercase/outer-whitespace Paste, invalid suffixes, ambiguous
characters, very long/malformed text and clipboard-unavailable feedback. Use
invented text and a coordinator-authorized clipboard harness that restores the
previous clipboard; this host task never reads or writes the system clipboard.

Hold Back with one symbol: exactly one deletion, no return; release and press
again to return when empty. Exercise Code edits/closure during a pending fixture
lookup; no stale reply may change text or reopen a menu. Full code auto-resolves
once; Done retries. Quit/restart normally, verify local Rejoin fields and fresh
fixture resolve; check the local file contains only code/name/time and is absent
from every builder/game package. A malformed record disables the action. Check
keyboard -> pad -> keyboard prompts, mouse hit rectangles, focus and native
keyboard return. Capture approved 01/03 at 4:3 and stretched 16:9, 720p
readability and 1080p comparisons. No fixture or source check establishes G/V.

## Additional review live observations (no behavior change)

Record that `persist()` already writes the profile on Play Online entry; an
initially blank profile becomes `Player`. Test a copied blank profile and verify
exactly which files/timestamps change, then restore the test copy. Record that
`runscript saveloadProcess(2)` runs re-entrantly inside `openmenu`: trace ordering,
current menu/controller and one initialization sequence, and verify no nested
menu/script failure. This review does not change either stock-preservation side
effect. Test console/delayed `openmenu online`: missing assets use native entry;
available assets show localized Press Accept; failed Hub open retries after
2500 ms; unrelated popup closure never causes a second initialization. Include
localized text retention and no repeated setter/intern churn in the real matrix.


## Round 4: native lobby contract (fixture level)

Require the additive DLL probe `lc-online-lobby/1` before authoring this skin.
Retain native menu names/types `host` / `join`, vtables 0x69f274 / 0x69f33c.
The invisible marker item `lc_lobby_v1` opts each parsed native skin into the
adapter. Without it, native activation/tick/Back/deactivation forward unchanged.
Both parsed `text_playername1` and `text_readycheck1` must be real LISTBOXes
with backing tables. A marked skin missing either returns to native online
before the retail activator can dereference it (guarded named-close fallback
if requesting online faults). It never leaves an inactive lobby waiting for
an inline dismissal. Unmarked/cached menus forward without resetting the
active marked lobby state. Missing lists remain a builder defect.

Actions stay on their native items. Start uses `text_startgame` through retail
0x5bacd0, including role/readiness, the existing rule-26 minimum, image transfer
0x6094f0 and new-game opcode 0x26/broadcast/start. Ready uses `text_readygame`
through 0x5c8840 after admission; ready Leave/Back unreadies before any departure.
Kick uses `text_kickplayer`, native state 1, selected row value/native lookup and
0x6133a0. Host Leave/Back, including a state-2 request arriving before the tick, first
opens an inline confirmation; Yes authorizes only this activation and sets native
state 2 for the next native tick's 0x607c40(false). Join departure keeps its
0x196e flag/deactivation/0x6097f0 lifecycle. The DLL never opens a base Party or
Hub to implement a pregame confirmation, Copy feedback or error.

Host X focuses the native Kick bar; guest X focuses Ready or, if ready, the
native Unready bar. A fresh edge supplies native Accept only at that class
tick's guarded mask read. Y is Copy; Ctrl+C is keyboard Copy. Copy enumerates only the calling thread's
existing windows and requires this process's igWin32WindowClass owner; no window
is created, activated or focused. Missing/foreign/wrong-class owners fail inline.
A real non-NULL window owner is required by the
[Win32 clipboard contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-emptyclipboard). Neither Copy path
manufactures native Accept. A coincident Copy/Accept suppresses the native
class action until Copy and Accept have both been released, including a held
keyboard/pad chord. Base UI input still handles authored inline buttons. The
manager mask hook filters only returns 0x5bad33 / 0x5c88d2 while this same menu
and activation generation are ticking. Native input/wait/transfer flags remain untouched; only the documented Host
confirmation phase is deferred through native state 0/2. Native ticks run once. Back dismisses inline state before
native Back can act when the presentation is valid; otherwise it exits the
Kick picker, unreadies a ready guest or leaves according to native state. The
actual native Join tick and mouse/help Leave branches dispatching the Back
slot always retain their native behavior through visible notices. A failed
DLL view never suppresses native actions/Back/Leave or creates a sticky error.

Roster counts match the native participant count, bounded at four. HOST alone
owns native row values: use its distinct row IDs and native session lookup.
JOIN's actual 0x5c8150 refresh never sets row values (they remain constructor
zero), so map its rows through exactly the participant order at menu+0x1968,
from 0x610ca0. Read head/+8-next/+0x10-record without changing the native cursor.
Validate counts/live pool slots/ID agreement where appropriate. Empty/pending
rosters accept native's disabled name placeholder plus zero ready rows; no
placeholder becomes an admitted member. Local identity comes from 0x6111f0
and its actual record ID; NULL is distinct from a valid index-zero participant.

Validation repeats after base input for a known valid Host picker selection;
self/out-of-range Kick is refused without blocking Back. An unavailable view
is presentation failure and defers to native policy. It does not veto any native
action or latch an error every tick. Transient target errors clear on recovery.
Native Start independently repeats role/readiness/count checks; an invalid view
also restores only a previous dim applied by this adapter, within native mode
and transfer gates. No DLL reads of readiness replace native authority.

The adapter hooks native activation, update and both roster-event/refresh slots.
It hides backing lists before refresh and rewrites HOST picker text before the
refresh returns. The two exact guarded name-formatting reads at 0x5ba66e and
0x5c829b receive <=31-byte allowlisted text during marked UI refresh only. This
also protects native formatting/measurement from user renderer/printf controls.
All other name getter consumers, including nested consumers in the same UI
scope, receive their exact native field pointers. Participant names never change.

HOST's completed picker row payload is allowlisted without format controls;
its original object/value/index/selection/pool ownership remains unchanged.
Only native-enabled, state-1, successfully sanitized picker rows become visible.
JOIN has no picker: its native colour codes/Host suffix and zero row values are
retained, and both lists stay hidden; only its card mirrors are display-filtered.
The real refresh/add/clear/constructor/value-write paths are host-fixture tested,
with immediate render-boundary observations before any later UI tick. Font and
actual rendered geometry still need G/V; no raster acceptance is inferred here.

Native mask/focus/authority/getter/row-text/state access and native adapter calls
are contained in small POD SEH boundaries, including raw phase/gate/wait reads
and writes. C++ allocation exceptions cannot bypass that /EHsc protection. A
native fault skips extra presentation and never causes a second native tick.
Host leave consent is one-use: it clears when native state-2 dispatch begins,
even if closure is deferred. Another Back must confirm again. Dismissal returns
a Host to Start (or its still-active picker) and a Join guest to Ready, never
implicitly to an unconfirmed guest Leave action.

The optional `install_hooks::lobby_fields` provider exists only for offline host
fixtures. Production passes no hooks, so room code is a placeholder, Copy is
disabled, RTT is neutral/Measuring, build is Checking and hero is the localized
fallback. Invented members are created by the host test's native service stubs,
never by the production DLL. Extras match native IDs, not row order; broker-only
members cannot become native rows or kick targets. Round 5 must supply a reviewed,
generation-bound real room snapshot before claiming real code/build/RTT data.

### LC authoring handoff: exact names, anchors and commands

LC changes are owned by the font/menu agent, not this DLL round. Preserve every
native action item, BOTH LISTBOXes and all original mode/wait/transfer items,
`data_name`, `desctext1..5`, callbacks and focus/mouse geometry dependencies. Keep
native action `usecmd` behavior: do not add a script command or close/open command
to Start/Ready/Kick/Leave. Map their actual hit rectangles to 05-lobby's left,
middle and right bars. Host middle = Kick player; guest middle = Ready. Guest
left is a dim Host-only placeholder, with no native action. Native guest Leave
text can be visually retired behind the localized overlay in the same action
rectangle; the original item stays enabled/focused and owns the click. Authored
`lc_guest_unready` / `lc_guest_leave` select UNREADY / LEAVE and the matching
B hint, including each overlay's model/glyph as part of that node's scene.

| Anchor in approved 05-lobby | Exact items / binding |
|---|---|
| Nonvisual capability | `lc_lobby_v1`, retained parsed marker item; no input, no visible text |
| Top-right self header | DATA text `lc_self_name`, `lc_self_initial`; localized scene variants `lc_self_hosting`, `lc_self_joined`, `lc_self_ready` |
| Room code region | DATA `lc_room_code`; button `lc_copy_code` with exact `runscript lcOnlineIntent(16)`; localized `lc_code_copied` in the existing status region |
| Four card panels N=1..4 | DATA `lc_pN_name`, `lc_pN_slot`, `lc_pN_ping`; named scenes/items `lc_pN_card`, `lc_pN_ready`, `lc_pN_not_ready`, `lc_pN_host`, `lc_pN_hero_fallback`, `lc_pN_you`, `lc_pN_ms`, `lc_pN_measuring`, `lc_pN_same_build`, `lc_pN_checking_build`, `lc_pN_selected` |
| Empty card N | Separate named items/scenes `lc_pN_empty`, `lc_pN_empty_title`, `lc_pN_empty_help`, `lc_pN_empty_plus`; all are hidden on occupancy |
| Summary beneath cards | DATA `lc_ready_count`, `lc_member_count`, `lc_waiting_name`; localized scene variants `lc_ready_waiting`, `lc_ready_all`, `lc_ready_solo` |
| Native Kick picker | `text_playername1` inside the existing card panel region; visible and focusable only in native Kick mode. `text_readycheck1` remains a real native backing roster, visually hidden. Four-card selected decoration mirrors native selected row |
| Inline confirmation/error region | Localized scenes/items `lc_leave_confirm`, `lc_lobby_error`; `lc_leave_yes` invokes exact `runscript lcOnlineIntent(17)`; `lc_leave_cancel` and `lc_lobby_dismiss` invoke exact `runscript lcOnlineIntent(8)` |
| Guest native right bar + B hint | Localized scenes/items `lc_guest_unready` and `lc_guest_leave` with coincident native action rectangle; no competing input command |

All listed localized scene variants are one individually hideable menu item
whose scene contains its static text/model/glyph elements; do not assume that
hiding a frame item hides unrelated sibling menu items. Ordinary card text
fields are hidden individually on vacancy. DATA text must be normal interned
text with no competing gamevar. Fields are bounded (names 31 bytes; code nine
characters; counts one digit; fixture ping at most five digits); clip/ellipsis
inside the approved region without changing the round-3 allowlist.

Compose the localized summary from the numeric/name DATA nodes at the approved
summary anchor: "2 of 3 ready. Waiting for Alex to get ready." Keep static words
and suffix within the selected scene; `lc_ready_summary` may be a static scene
container but is not overwritten by the DLL. The waiting scene shows only for
an admitted unready member; all-ready and solo scenes are mutually exclusive.
Hero fallback is "Hero assigned at start". RTT unknown = Measuring with neutral
bars; local = You. SAME BUILD means fixture/token preview, not certified wire
compatibility. No actual hero, room title, campaign, visibility or route is
invented: retain localized pending/unknown content for `lc_room_name`,
`lc_room_visibility`, `lc_campaign_summary` and route ornaments until later
reviewed data exists. Static drop-in copy says next area. Native device modes/description glyphs must
select keyboard-only Ctrl+C versus pad Y; do not display an unbound shortcut.
The guest state variants also select X Ready/X Unready and B Leave/B Unready.

Copy and confirmation use the existing card/status region; inline state must
not use `openmenu`, `closemenu` or a base popup. Author each inline button as a separately named menu item, including safe mouse
rectangles inside that region. The DLL hides/disables all three buttons outside
their own panel, focuses Cancel on opening and restores native picker/Start/Ready focus
on dismissal. Keep their links in the directional graph while hidden/disabled
items are skipped; no overlapping invisible ordinary actions. Back ownership is
in the DLL/native handlers, so do not add a second Back close command. Initially
hide all optional/notice scenes; the adapter selects visible variants. Keep
native input-enabled, wait, transfer, title/help and description dependencies
intact. LC must include closure/localization, one/four/empty card focus graphs,
visible picker, disabled Start, keyboard/pad/mouse, safe rectangles and resource
budget checks in its builder/selftests.

### Host and live verification

`test_online_menus.py` includes every prior mode plus round-4 Host, Join,
missing/corrupt-list negative, actual shared-pool and Copy modes, plus a negative
mode for every new retail guard. The tests execute actual mapped class activators,
Host/Join refresh bodies 0x5ba4e0/0x5c8150, native add/clear/row constructor
and value writes, class ticks, Accept predicate, ready setter/getter, Ready-aware
Back, actual Host mouse hit/release and Join mouse-help paths, and Join
deactivation. Only presentation/font/network/participant/cleanup services are
invented or stubbed. They never execute the game entry point. The pool case exercises real
retail base add/clear through twelve 128-row allocation cycles, including 83-byte
rows and shared exhaustion. It does not claim font measurement/render coverage.

After coordinator integration and Claude review, run a separate authorized real
boot/peer procedure: owned candidate copy, hash inputs, private/never displayed
desktop, slot.py --wait, HOLD-GAMES/external XMen2 checks before every launch,
registry export and byte-identical restore. Use verified `[Test] NoActivate=1`
in every test-copy INI once supported. No desktop focus/input. Capture real
host/join setup and both skins at one/four/pending members, both aspects and all
devices. Verify native callbacks keep refreshing while Copy/inline confirmation
or errors are open; Back closes inline state, then picker, then Unready, then
intentional Leave in the appropriate role. Verify native roster indices, pool
counts and role-X prompts; actual Start blocked by every admitted unready peer,
solo only with rule 26, image-transfer success/failure and new-game broadcast.
Check Kick refuses self and invalid/reselected rows, removes the chosen admitted
peer once, and keeps the lobby open. Copy success/failure requires separately
authorized clipboard harness/restore. Check Ctrl+C/Y/click plus coincident held
Accept creates one Copy and no Start/Ready/Kick; Unready/B labels and Start alpha
must match the native state. Finish the prior ten direct boots and three-peer
hotspot/relay gates before any player-ready claim. No live gate was executed by
this round; existing entry-time profile save and reentrant saveloadProcess(2)
observations from round 3 remain pending real-game review.


## Coordinator round-3 review follow-up, folded into round 4

The coordinator reports Claude's review of 168547f..006471b approved round 3 as
opt-in. S1..S4 and the nits below are included in this round-4 diff and tests;
no history, rule identity, LC source or game installation was changed.

- **S1:** Rejoin stages a separate `rejoin_seed`. Additional Hub ticks never open
  or close a seeded Code model. Only Code's first tick consumes the seed;
  another explicit Hub action or a different native menu request invalidates it.
  The regression holds the Hub current for two extra ticks after Rejoin.
- **S2:** DirectInput delivery reads the already-published manager cell 0x8aff18,
  with SEH validation. It never invokes lazy getter 0x5d8920 or `live()` from that
  path. Before the first game tick, keyboard delivery does no frontend/model
  work. Once the game-thread ID is known, foreign-thread UI edits are refused.
  One-time logs print keyboard/tick IDs and their matched/FAILED assertion.
- **S3:** Custom destination requests call `abandon_name()` before the original
  manager request. This drains an owned completed result or changes its pending
  tag to 4. Host/Back immediately after queued Change name cannot later commit
  that old result. An abandonment fault keeps the current custom menu available
  with the name-open notice; a subsequent explicit navigation can retry.
- **S4:** All native name operations (getter, stale drain, setters, polling,
  immediate result copy and native profile/save side effects) go through one
  POD-only SEH helper. The singleton must equal 0xa3e168 and carry native vtable
  0x6a4730; apply validates session/profile/console too. A native access violation
  invalidates local ownership, attempts one bounded tag-0 drain and selects
  `lc_notice_name_open`. No retry loop or exception crossing DirectInput is
  introduced. Calls completed before a native fault cannot be rolled back; the
  adapter reports failure and makes no successful-name/profile claim.

Keyboard text uses a fixed 64-entry game-thread inbox so multiple poll edges
arrive in ascending DirectInput scan-code order rather than losing all but one.
Valid symbols take precedence over simultaneous ambiguous-key feedback. Numpad
2..9 are supported; numpad 0/1 give the existing invalid-symbol notice. Alt and
AltGr combinations are left untouched. Enter follows any same-poll text in the
inbox and is removed from the returned native key state, so it cannot also accept
the focused grid cell. Close/navigation invalidates the inbox, including address
reuse. Existing Back/delete and pad-tool precedence stay intact.

Recent-game persistence uses a heap environment buffer, a same-directory
CREATE_NEW temporary file, checked complete WriteFile and CloseHandle, then
MoveFileExW(REPLACE_EXISTING). No FlushFileBuffers or write-through flag runs
on the game thread; this is an atomic best-effort recent-code cache, not a
power-loss-durable campaign/recovery record. Only successful
replacement publishes the new in-memory recent record. Failure removes only its
own temporary file and preserves the previous disk/memory record. Directory,
read-only and partial-write failures are fixtures; the retained flush test
asserts that no UI-thread durability flush occurs; bounded 128/129-byte
malformed records and CRLF records are refused. The existing on-disk LF-only
`lc-rejoin/1` schema is unchanged. Rejoin never implies admission.

Clipboard Paste additionally rejects nonzero data after the Unicode terminator
(embedded-NUL/hidden-suffix input); zero allocation padding is permitted. Logical
text remains bounded at 256 characters, including outer whitespace. NULL handle,
GlobalSize zero, lock failure and all success/refusal paths balance only acquired
resources. No system clipboard is used by the fixtures.

Intent 15 remains internal invalid-key feedback and is neither captured as an
authored command nor accepted by the public handler. Exact strings with no
supported live custom source return to the native console queue, preserving its
result rather than reporting them consumed. The continuation hint is dismissed
on another main-menu request or real main Accept. Public LC commands remain the
round-3/4 constants already listed above; do not author intent 15.

**Additional LC node:** author initially hidden localized text/scene
`lc_notice_recent_save` at the existing inline/status notice anchor, meaning
"Could not remember this game on this PC. Your previous recent game is unchanged."
Keep no-admission/offline-fixture context in the screen's static copy. The generic
`lc_inline_error` remains the fallback if the node is absent. No new menu, bar,
identity, gameplay rule or command is needed.

`test/online_menus_followup.inl` executes the new regressions against the real
production online_menus.cpp. `xml2_online_input_wiring_test` includes the actual
production test_input.cpp translation unit and runs its COM CreateDevice,
GetDeviceState and SetCooperativeLevel hook chain. It tests Menus-only opt-in,
pipe OFF/ON delivery, background filtering, failed-read reset and unrelated
size/device/null pass-through. Only OS foreground answers and the UI consumer
are faked; `install()`/pipe service/capture dependencies are unreachable and
assert if invoked. The enabled branch uses an owned event and synthetic state,
with no named pipe, worker, actual keyboard/device or desktop input.

Add these cases to the future authorized live procedure: delayed native Code
opening after Rejoin; null/prefrontend manager DirectInput reads; queued Change
name then Host/Back; native editor failure without ownership/result leakage;
keyboard/tick thread IDs; rapid two-key and text+Enter input, Alt/numpad and device
transitions; unavailable/read-only recent storage without false Rejoin; and real
clipboard NULL/zero/256-character/embedded-NUL behavior under a separately
authorized clipboard-restoring harness. The clipboard strict-tail check may
refuse nonzero spare allocation data; that refusal must preserve existing Code
text and show the localized Paste notice. None of these live checks ran here.


## Round-4 review correction on 097f219

B1 is covered by the real retail refreshes: Host writes native participant IDs,
Join leaves every row value zero; the fixture no longer stubs either refresh
or preloads Join values. It also covers reordered participants, four/empty/
pending rows, a real index-zero participant versus missing local record,
invalid views with working native Ready/Back/Leave, and malformed activation
returning to online. Native help/tick dispatch to Join Back passes through
notices; literal user Back can dismiss inline UI. The separate base-input
boundary distinguishes those paths even when the retail Join tick tail-jumps
its Back slot. No fictional callback or hidden native row ID is relied upon.

The review adds guarded native update/refresh/mouse/base-input boundaries and
the two exact roster-name call contexts to the same opt-in transaction. Every
new guard has a negative fixture. Native-name getter output is unchanged for
all other call sites. Real Host mouse hit/release -> input -> Start/Kick/Leave,
Join mouse-help Leave through a notice, renderer-visible Host row safety,
hidden unchanged Join formatted rows, one-use consent, safe focus restoration
and fault-contained native access are explicit regression cases. No game or
desktop input was used. Full LC integration and real peer/render gates remain.
