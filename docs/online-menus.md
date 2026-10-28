# Online menus: round 5 broker lifecycle

This is a guarded presentation bridge, pending Claude review and real-game
acceptance. Host fixtures do not establish fresh-boot stability, rendering or
input parity. Round 3 adds owned native name polling, Code input/clipboard
and local Rejoin storage. Round 5 connects Host/Code/Browse to the existing
authenticated broker client; native auto-admission remains a separate round.
The old offline resolve path is available only through host-fixture hooks. Round 4 adds
opt-in presentation adapters around the real native host/join classes; they
remain current and own their roster, callback, modes and actions.

## Round 5 LC handoff and broker verification

Require the additional DLL probe `lc-online-broker/2` (round 5b; `/1` is no longer present) before authoring these
commands. The marker is an additive UI API capability, not a gameplay rule or
release identity. Keep the existing resource marker and all native lobby types,
items, modes and actions. No LC sources are changed by this DLL round.

| Intent | Exact constant command | Allowed screen |
|---|---|---|
| Refresh | `runscript lcOnlineIntent(18)` | Browse |
| Open room | `runscript lcOnlineIntent(19)` | Host Setup; opens native Game Options before broker posting |
| Retry | `runscript lcOnlineIntent(20)` | Custom Hub/Host/Code/Browse |
| Toggle private/public | `runscript lcOnlineIntent(21)` | Host Setup |
| Previous / next page | `runscript lcOnlineIntent(22)` / `(23)` | Browse |
| Select visible row 0..6 | `runscript lcOnlineIntent(24)` through `(30)` | Browse, any cell in that row |
| Join selected room | `runscript lcOnlineIntent(31)` | Browse; resolve, then automatic room-only native admission |

These are script intent values, not agreement rule IDs. The native bridge
(`lc-online-native/1` DLL probe) now completes Game Options/save/difficulty
selection before posting and joins resolved rooms through actual native
discovery. The broker API keeps printable ASCII room names <=32, without `;`
or quote; native game names and screen names are each <=31 game bytes.
Host Setup seeds Game Options from its valid native-length preset/default.
An incompatible/overlong native seed becomes the visible "Online game" choice
for confirmation/editing there; it is never silently cut to 31 bytes. The
accepted native name, rather than an earlier card value, names the broker room.

Preserve DATA items `service`, `self_state`, `browse_hint`, `room_name`,
`preview_name`, `room_code`, `details_name`, `details`, and the seven by seven
Browse matrix `room_R_C`, R/C=0..6. Add `broker_error` for a bounded allowlisted
error identifier, and `retry` with intent 20. Host's Open item must be named
`open` or `prompt_open`; Browse prompts retain `prompt_join`, `prompt_refresh`,
`scroll` (23), `scroll_up` (22). Rows must exist even for an initially empty
list, with dynamic text storage and row-specific constant commands. Retain
closed directional focus and mouse rectangles when rows are disabled/rebound.
F5 and the pad's MENU_NEXT refresh Browse; PageUp/PageDown change its bounded
page. The Host pad's MENU_OK opens native Game Options. These shortcuts have real handlers.

Add named, initially hidden localized variants at the approved status/error
anchors. The DLL selects them; no remote text becomes a command:

| Node | Copy / meaning |
|---|---|
| `lc_broker_auth` | Connecting to Online service... |
| `lc_broker_list` | Finding public games... |
| `lc_broker_host` | Opening your room... |
| `lc_broker_resolve` | Looking up room code... |
| `lc_broker_empty` | No public games right now. Host a game or go Back. |
| `lc_broker_stale` | These games may be out of date. Refresh to try again. |
| `lc_broker_err_<cause>` | Round 5b: one scene per failure cause, see "Round 5b presentation selectors" |
| `lc_broker_room` | Code: Room found. Editing the code leaves this room. Host/Browse: Room found/open; native lobby setup is the next step. |
| `lc_broker_cancel` | Canceling... / Leaving... |
| `lc_broker_offline_room` | Online service unavailable. Existing connections stay open; new players cannot join. |
| `lc_host_private` / `lc_host_public` | Mutually exclusive localized visibility labels |
| `lc_rowR_selected`, R=0..6 | Selected-row decoration at its approved geometry |

Plain existing DATA fields receive short truthful service/count fallbacks; the
new variants provide localized pending/empty/stale/error copy in screens 01/02/
04/09. Replace the old resolving-fixture wording in production assets. Add
specific localized explanations for `code_not_found`/`no_such_room`,
`room_closed`, `room_full`, `version_mismatch`, `rate_limited`, timeout and TLS
refusal using `broker_error` as a diagnostic field, not executable text. Retain
inline layout; errors never tear down a native lobby. Numeric RTT is never
invented; neutral `--` and Measuring remain. SAME means only known equal token.

Rejoin is written only after a current successful Code/Browse resolve. Back,
edits, close and screen departure invalidate pending tickets before inbox drain,
including the frame where a reply arrives. Back on a full/partial Code first
deletes one symbol; on empty Code it returns to Hub. Back from committed custom
Host/Browse intentionally leaves that broker room. Deactivation of a committed
room during a future native-lobby transition does not implicitly leave it.
Native in-session departure/admission cleanup stays in the next round.

For fixture verification, build and run `xml2_online_room_test`,
`xml2_online_browser_test`, `xml2_online_security_test`, `xml2_broker_tls_test`,
`xml2_online_broker_test` (GNS builds) and the complete `test_online_menus.py`
matrix, including `r5-broker`. The broker fixture uses the production worker
and lifecycle with invented socket/TLS responses; the separate TLS suite tests
real certificate trust and protocol selection; its optional `--live` handshake/
record path is not exercised by these offline gates. No real service/P2P success
is inferred. Preserve the full
native suite, PE guard scan, full-INI union and OFF proof against b603cc8 with
Menus/Host/JoinCode absent, and the content guard.

The coordinator's separately authorized G/V procedure is:

1. Apply launch discipline before every boot: HOLD-GAMES/external XMen2 checks,
   slot.py `--wait`, owned candidate copy, matching DLL/assets, verified
   `[Test] NoActivate=1`, private/background capture, registry export and
   byte-identical comparison. Never focus or drive the desktop.
2. With a controlled protocol-2 TLS broker and its exact pin, fresh boot with
   no launcher keys. Enter Hub through a real accepted controller and verify
   the broker has **no new room**. Traverse Browse -> pending -> empty and
   populated; refresh repeatedly, leave/re-enter, wait 10 seconds idle, and
   compare request timestamps against the shared 2-second limiter/backoff.
3. Host -> Open -> code -> Back, then Code/Browse resolve -> Back -> Host in
   the same process. Delay each reply until after Back/edit/Leave/re-entry.
   Verify zero duplicate rooms, stale members, stale codes or native admission
   claims. Test launcher Host/Public/RoomName and JoinCode presets only after
   real controller acceptance.
4. Inject wrong pin, timeout, malformed/oversized JSON, error/rate-limit/full/
   expired/mismatch replies and missing optional metadata. Check Retry and
   cancellation during authentication, list, host and resolve; verify stale
   rows remain labeled and cannot Join. Restore service and retry after backoff.
5. After Browse -> Back/Cancel and after Host/Code -> Leave, take Other ways
   and verify a fresh native LAN/IP GNS session still works under the restored
   native identity, with no room certificate retained. Test a busy native link
   during the request/transition: listener and identity must not reset.
6. With separately established real peers through the existing native route,
   interrupt only the broker. Verify healthy P2P gameplay continues, new
   admission stops, no automatic replacement room opens, and explicit Leave
   still works. This is a live gate; a retained fixture tunnel is not proof of
   healthy traffic or gameplay.
7. Compare actual screens 01 Hub, 02 Code/Host and 04 Browse plus 09 inline error
   variants to the approved rev-2 frames at 4:3 and stretched 16:9, 720p/1080p,
   keyboard -> pad -> keyboard and mouse. Check long allowlisted names, seven
   columns/rows, neutral ping, pending/empty/stale/error copy, focus, safe
   bounds and input prompts. Stop all owned helpers and clean only the created
   copies/profile paths by checked absolute Python paths afterward.

No game launches, desktop input, LC build, live broker or visual acceptance are
implied by host-fixture success.

## Round 5b presentation selectors (probe `lc-online-broker/2`)

Everything below is derived from fields the broker already sends (`list_rooms_ok` rows, the client phase and error id,
the stored Rejoin record). No protocol, wire or release identity changed. The DLL shows/hides named nodes and binds DATA
text; a node a screen does not author is skipped. LC authors all copy.

**Per-cause error scenes.** While the client is in `retry_wait`/`error`/`refused` exactly one of
`lc_broker_err_{code_not_found,room_full,version_mismatch,rate_limited,timeout,offline,refused,generic}` is shown (the
one-size `lc_broker_error` is gone). The id is classified by `online_menus_rules::classify_error`: `no_such_room`,
`code_not_found`, `room_closed`, `code_expired` -> code_not_found; `room_full`, `room_not_joinable` -> room_full;
`version_mismatch`; `rate_limited`; `timeout`; `broker_unreachable`, `worker_unavailable`, `send_queue_full`,
`inbox_overflow` -> offline; `tls_pin_mismatch`, `broker_rejected`, `invalid_certificate`, `bad_auth`, `auth_failed`,
`unsupported_version` -> refused; anything else (including hostile ids) -> generic. `broker_error` stays a bounded
diagnostic. Item `retry` is **hidden** (not merely disabled) unless the phase is `retry_wait`/`error` and the cause is
retryable (rate_limited, timeout, offline, generic); a `refused` client phase never shows it. The version_mismatch error
carries no host/own token (the reply path does not keep them), so its scene cannot name the builds; the Browse detail
line does for the selected row.

**Browse rows** (`online_menus_rules::browse_rows` = 5 rows per page, R=0..4, shared relative art; the approved seven rows need 241 scene anchors and XMen2.exe holds 175 per menu scene - playfield build `0x5882e0`, see below): `lc_rowR_st_{lobby,progress,closed,full}` (status kind),
`lc_rowR_pips_{0..4}` (players, unknown = 0), `lc_rowR_build_{same,other,check}`, `lc_rowR_dim` (shown for full,
closed, other-build and password rows) and the existing `lc_rowR_selected`. Status: players >= max -> full; started ->
progress; not joinable -> closed; else lobby. **There is no `loading` kind**: the broker has no such field, so none is
invented. Build: SAME only for a known equal token, OTHER only when both tokens are known and differ, otherwise CHECK.
Cells `room_R_4`/`room_R_5` no longer exist as DATA: the status word, the ping `--` (no measurement exists and no
connection is opened per row) and SAME/CHECK are drawn by the row scenes; `room_R_6` holds only the other build's
token (<= 8 characters). The hit/focus cells are columns 0,1,2,3,6; the five cells of a row share one focus anchor `room_R_focus`.

**Scene anchor capacity.** The playfield build `0x5882e0` walks the menu scene (`0x587f90`) and pushes every `ItemName` anchor's transform into a 175-entry stack vector and its user info into the playfield's own 175-entry vector (`0x587e40` -> `0x587620`, no bound check; count at +0x2d4, bit mask at +0x2bc); the name table (+0x3c) add `0x588120` refuses past 0xaf. Playfields live in a static 5-slot pool at `0x7ec72c` (stride 0x8b0). Anchors 176-181 overwrite the bit masks, the 182nd overwrites the count with a pointer and the 183rd push writes to a wild address: the 060p Browse crash `0xc0000005 at 0x00587654` (241 anchors). LC's builder refuses a scene over 175 (`online_menus.ENGINE_SCENE_ANCHORS`, `anchor_count`). Raising it means relocating that pool with larger inline tables (or replacing the build/lookup routines `0x587990`..`0x588950`). Area shows `--` when the
broker row has no zone. The detail line is `Friends join at the next area - Same build as you (TOKEN)` /
`- Other build (THEIRS vs your TOKEN)` / `- Build not checked`; it is empty for an empty list so `lc_broker_empty` is the
only empty-state message (it still says `Refresh to update games` for a stale list without selection).

**List header**: DATA `list_count` (`- N`, empty before the first list), DATA `list_age` (`Updated N s ago`, then minutes,
then `Updated long ago`; ticked from the *completion* of the last good list, 32-bit wrap safe, kept while a refresh
fails) and scenes `lc_age_fill1..5` (one per two seconds of age, the 10 s idle refresh interval). `lc_broker_stale` is
not shown beside a committed room (the offline-room line takes that place).

**Service**: `lc_service_ok` (diamond + localized word) is shown when the client is authenticated, idle and not in a
room; `service`/`self_state` are then empty so the word is never drawn twice. Hub authenticates nothing by itself, so the
Hub shows it only while a previous operation's authentication is still live. With no live broker the DLL hides the
service scene, every cause scene and `retry`, and sets the Host `lc_host_private`/`lc_host_public` pair from the
visibility choice (private by default) so it is consistent from the first frame.

**Rejoin**: DATA `last_code` is now `Code ABCD-EFGH, 2 hours ago` and `last_time` the age alone (`just now`, minutes,
hours, days, `a long time ago`), computed from the stored epoch seconds (host fixtures inject the wall clock).

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
| Done | `runscript lcOnlineIntent(13)` | Code; explicit complete-code retry |
| Rejoin | `runscript lcOnlineIntent(14)` | Hub; load local remembered code, resolve afresh |
| Symbol | `runscript lcOnlineIntent(32)` through `(62)` | Code; alphabet index 0..30 |

Value 1 is reserved and rejected by the script dispatcher. There is one real
script registration, `lcOnlineIntent`, return `n`, arguments `i`. Add no direct
`setuphost`, `startgamedialog`, code resolution, admission or broker commands.
Party/native Host settings/admission remain future rounds. Open room/Refresh/
Browse Join now use intents 18..31 below. Value 15 remains internal invalid-key
feedback; 16/17 are the reviewed lobby Copy/confirm actions. Values outside
2..62 are rejected. Production emits no synthetic Connected or fixture-success
claim. Offline responses require the explicit host-test seam.

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
previous +0x374). Scoped handlers implement `$MENU_NEXT` Paste and
`$MENU_OTHER` Clear in Code, `$MENU_OK` (Start) Done; in Hub, MENU_OTHER
changes name and MENU_NEXT Rejoins. Direct keyboard text wins over coincident
native X/Y bindings.

**Pad buttons (060p live-round fix).** The mask has one bit per UI code and the
engine raises a code when its action's binding is pressed (`0x619c40`: code ->
action). On the fix's console pad layout that makes pad X = Guard = MENU_SUBTRACT
(bit 8), pad Y = Jump = MENU_OTHER (bit 0x18) and RT = Power = MENU_NEXT (bit
0x16), so rounds 2-8, which read MENU_OTHER as "X", did the wrong job on a real pad
(X nothing, Y Change name / Clear, prompts `Y Change name`, `[RT] Rejoin`; their
host fixtures fed the bits directly). On the Hub, Host, Code, Browse and Party
screens, while the player's prompts show the pad (`pad_prompts::pad_shown`, the
same device tracker that draws the labels), `pad_menu_mask` reads X as the
screen's "other" button, Y and RT as its "next" button, and the prompt hook draws
`$MENU_OTHER` / `$MENU_NEXT` as the pad's X / Y (`pad_prompt_code`). The keyboard
keeps the engine's keys (Del = other, KP5 = next); the native Host/Join lobbies
are unchanged (they read the raw bits and draw the engine's own labels). Without
the prompts patch (`Prompts=off`) nothing is translated. Every route uses the same one-slot, menu-bound mailbox.
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
python scripts/test_online_menus_startup.py --dll <dinput.dll> --host <union_host.exe> --retail <owned-XMen2.exe> --engine <owned-engine-dir> --out <new-workspace-output> --fonts <owned-build-with-the-private-font-family>
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
native Back can act, independently of roster-read validity; otherwise it exits the
Kick picker, unreadies a ready guest or leaves according to native state. The
actual native Join tick and mouse/help Leave branches dispatching the Back
slot always retain their native behavior through visible notices. A failed
DLL view never suppresses native actions/Back/Leave or creates a sticky error.

Roster counts match the native participant count, bounded at four. HOST alone
owns native row values: use its distinct row IDs and native session lookup.
JOIN's actual 0x5c8150 refresh never sets row values (they remain constructor
zero), so map its rows through exactly the participant order at menu+0x1968,
from 0x610ca0. Read real head/+8-next/+0x0c-previous/+0x10-record nodes: head.previous and
tail.next are NULL, there is no sentinel, every linked node has a participant,
and walked-node count must equal +0xc. Restore the shared iterator cursor
after native lookups (including faults), since those helpers also use it.
Validate counts/live pool slots/ID agreement where appropriate. Empty/pending
rosters accept native's disabled name placeholder plus zero ready rows; no
placeholder becomes an admitted member. Local identity comes from 0x6111f0
and its actual record ID; NULL is distinct from a valid index-zero participant.

Validation repeats after base input for a known valid Host picker selection;
self/out-of-range Kick is refused without blocking Back. An unavailable view
is presentation failure and defers to native policy for normal actions and
exit. Picker acceptance alone fails safe: self or unknown targets/local identity
are refused even when the full view is unavailable. Back and Leave stay usable;
no read failure latches an error every tick. Transient target errors clear on recovery.
Native Start independently repeats role/readiness/count checks; an invalid view
also restores only a previous dim applied by this adapter, within native mode
and transfer gates. No DLL reads of readiness replace native authority.

The adapter hooks native activation, update and all three roster-event/refresh
slots (+0xa0, +0xac and +0xb4), including Host 0x69f314 and Join 0x69f3dc.
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
fixtures. Production now supplies a generation-bound room code/title/visibility
from the broker snapshot, enabling Copy only for a committed valid code. RTT
stays neutral/Measuring, build stays Checking and hero stays the localized fallback. Invented members are created by the host test's native service stubs,
never by the production DLL. Extras match native IDs, not row order; broker-only
members cannot become native rows or kick targets. No broker membership is mapped into a native roster row; native participant
identity/admission and actual peer telemetry remain separately guarded.

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

Clipboard Paste reads the text up to its first Unicode terminator, as every
clipboard reader does; the spare allocation bytes after it are unspecified
(`GlobalAlloc` without `GMEM_ZEROINIT`) and are ignored (the strict-tail check of
rounds 3-4 refused a valid `abcd-efgh` on a real clipboard in the 060p live round).
No terminator in the first 257 characters is refused. Logical
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
authorized clipboard-restoring harness. (The strict-tail check named here refused
real clipboards and was removed; see the Clipboard Paste paragraph.) None of these
live checks ran here.


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


## Re-review correction on 2454786: real participant containers

The prior fixture was wrong: it appended a null-payload sentinel to a circular
chain, while the retail container is NULL-terminated and has no sentinel.
`read_lobby` now validates exactly the declared number of non-NULL payload
nodes, the head/previous/tail/next links, and then a NULL next pointer before
calling any lookup. It never requires or dereferences an invented terminal
node. A bounded bad-shape check avoids hanging native lookups on a malformed
cycle. Native lookup cursor changes are restored around UI reads.

The fixture builds the actual list at session+0x26c using retail 0x611e00
insertion, which calls retail 0x611da0 allocation and 0x605740 pool allocation.
Only the pool backing-store singleton is supplied by the fixture. Reversed
inserts exercise native sorting; no next/previous/payload links are fabricated.
The real 0x610ca0, 0x6110c0, 0x6111f0 and 0x611250 run without replacement.
Local flag/cache fields model actual native state, including absent/zero and
pending cases. Real LISTBOX +0xc4 selection/scroll clamping also runs; its parsed
visible-row count is initialized instead of replacing the method with a no-op.

`r4-retail-list` tests both roles at 1..4 members and real successful/missing/local
lookups; `r4-refresh-paths` covers +0xa0/+0xac/+0xb4 immediate renderer-boundary
observations. `r4-back-combos` verifies Back+Accept cannot bypass pending Copy,
modal/tool, Copy-latch or picker target checks. Only the Back bit is forwarded
when suppression applies. `r4-unknown-kick` verifies invalid views/selections
cannot alias a real remote index zero, unknown local cannot authorize Kick, and
Back still exits. `r4-dismiss-invalid` verifies notice dismissal does not require
a roster read: first Back dismisses, a later intentional Back uses native Leave.

Missing-list activation still queues return to native online rather than calling
the unsafe activator. Its real manager processing/one-native-tick interval is a
live residual to check, not proven by the queued-request fixture. No live game,
raster capture or desktop input was executed for this correction.


## Round 7: lobby card facts (party reports, measurements)

The four 05 cards now get observed facts in production (`broker_api::party` -> `online_room::party`; host fixtures
still use `lobby_fields`). New D-bindings LC must author on each card `lc_pN_` (N = 1..4), all optional (an absent item
is a no-op): `phase` (text: Connecting / Checking build / In lobby / Ready / Waiting for next area / Receiving campaign /
Loading / Playing / Watching story; empty = unknown), `route` (text `direct` / `relay`, empty unknown), `to_host`
(hidden unless the shown RTT is the host's measurement, label "to host"), `other_build` (OTHER BUILD badge, beside the
existing `same_build` / `checking_build`), `pending` (marker scene for a broker-only member's card: Connecting /
Checking build, no ready diamond, never focusable). Browse: optional text `room_<row>_5` per row (measured RTT of the
room this PC is in, else empty -> keep the `--` scene underneath). Unchanged: `hero_fallback` ("Hero assigned at
start"), native READY/NOT READY, `lc_ready_count` / `lc_member_count` (native rows only), Kick picker (native rows
only). Host fixtures: `r4-r7-cards`, `r4-r7-cards-join`, `r4-r7-bounds`. Contract: [online-party.md](online-party.md).

## Native Host setup and automatic admission (round 6)

The bridge is `src/online_native_flow.inl`, included by the existing dispatcher;
it adds no thread, transport, broker operation or script function. Its guarded
sites are Game Options activation `0x69f66c`, tick `0x69f694`, Back
`0x69f688`, accepted Post queue
`0x5b9609`, the native Host open queue at `0x60a095`, and console `openmenu` dispatch at
`0x5f1f8b` (the retail `openmenu` handler `0x5f1d80`, registered at `0x5f4a73`, ends in manager +0x6c; the
hook decides console provenance there and then continues through the same +0x6c request path without routing it a
second time). Round 6 hooked `0x5f1c2e`, which is inside `closemenu` (`0x5f1c10`, manager +0x74): the owned Host's
queued `openmenu host` then reached the +0x6c hook as a plain request and was refused (live 060p: hub Host 0/4,
`native_timeout`); the fixture now drives the real `0x5f1d80`. Difficulty shares the
Post call site, so commands other than the exact `setuphost` still go through
the stock queue. Native Host posting runs the actual `0x5f1bb0` handler,
including `0x606e90` and `0x609f50`. The console handler entry `0x5f1bb0`
and script producer boundary `0x49faac` also retain setup-generation ownership.
Terminal full-participant clears are observed at `0x6128ab` (inside session
shutdown `0x6123e0`) and `0x612d1f` / `0x612dda` (inside lost/local-participant
handling `0x612c40`). Broker Leave runs after the real `0x612070(false)` removes
the local participant, even without Hub navigation. The old host-only
`0x612a00` hook is removed: `+0x421` is host authority, while `+0x420` is the
active-session byte. Host setup/admission resets at `0x609fbe` / `0x60a0c9`,
keep-local clears and remote-only removal do not signal terminal Leave. Native Join admission runs `0x60a0a0` with
an actual 0x44-byte native discovery record.

| State | Event | Effect |
|---|---|---|
| Host Setup | Open (19) | Native Game Options; no broker room yet |
| Native setup | New/load, difficulty, name, max players | Established native dialogs/save services; max 2/3/4, native name <=31 |
| Native setup | Back/cancel | Native cancel, release selected image; no broker post |
| Native setup | Native Post | Capture accepted name/max/mode/difficulty/load choice; start one broker host request |
| Posting | Broker room confirmed | Run native setuphost; broker listing stays unjoinable |
| Host requested | Actual native Host activation | Publish existing v2 joinable metadata; keep native Host current |
| Code/Browse | Resolve confirmed | Wait for certificate, ICE and authenticated host tunnel |
| Discovery | Exactly one eligible current host record | Invoke native admission once; wait for native response |
| Admission | Native success plus a real local participant | Open native Join; Ready remains native |
| Any pending phase | Cancel, refusal, ambiguity, expiry | Invalidate generation/callback/queued open, native cleanup, broker leave |
| Native lobby | Start/Ready/Unready/Leave | Existing native actions, readiness gates and rule-26 next-area wait |
| Native session | Teardown | Leave broker room and release discovery filters without a Hub visit |

The tunnel/certificate/ICE wait is bounded to 45 seconds to allow relay/TURN
negotiation over a hotspot. Discovery, admission and menu activation each keep
their 15-second deadline; the broker retains its own 10-second deadline and shared limiter. The UI never
waits synchronously for the broker or a tunnel. Missing, duplicate, wrong-port,
wrong-address, disabled or incompatible records never select master/plain-IP
fallback. Native discovery is reset before searching, queries only the proven
host virtual address, and never keeps a record pointer across frames. Records
from another room member are ignored; only records sourced from the proven
host tunnel count toward uniqueness. A duplicate or invalid host record still
fails closed; zero host records waits until the discovery deadline.

The native admission callback contains a generation, not a menu pointer.
Success event 8 is insufficient without a native local participant; the retail
response can emit that event after allocation failure. Both native setuphost producers attach an internal `lc-native:<command-id>`
argument. The console handler consumes its real `const char**` cursor through
the retail tokenizer, matches the issued command ID and, for an owned setup, checks the saved
generation and requires Game Options to be current. Typed/usecmd unmarked
setuphost is refused while a flow is owned and never consumes another queued
command marker. Unmarked legacy argument cursors retain their original position.
These arguments are process-local console provenance, not a peer message or LC
command/API change. Queued Host opens still retain their actual console FIFO
order, including legacy commands. Cancel invalidates old ticket/serial metadata
while retaining denial markers until those actual commands drain. Console
tracking has no 16-slot activation limit. Host/Join
activation tokens use bounded current-generation storage; cancel drops old
entries into compact per-role denial counts, consumed before a replacement
activation of the same role. Fresh explicit legacy routing clears those role
counts and allocates no activation tokens, so unactivated legacy requests never
exhaust the owned-flow capacity. A canceled open
cannot later become a legacy Host or attach to a new controller/room. The custom
Host -> Game Options close is an explicit handoff; other current-screen closes
cancel pending work. Cached noncurrent menu closes cannot cancel a newer request.
Back+Post in one native setup tick consumes that old frame instead of queuing an
unbrokered setuphost. Failed setup returns to Host Setup and keeps that native
options context owned until it closes; a second Post cannot fall through to
legacy hosting. Tag-1 name completions and the native cached edit state are
drained/reset on a fresh setup without consuming the Hub's tag-0 name request. Rejected stale activation also suppresses its native tick, base input, mouse,
Back, refresh and deactivation, so it cannot mutate a replacement session while
recovery is queued. Initial activation still requires broker authentication;
already-active lobby reactivation survives broker loss. Native lobby activation
can repeat without publishing twice.

Native Host/Join deactivation retains its original Start/transfer flags. Real
Leave completes native cleanup before room cleanup; the native host-authority
latch makes repeated teardown harmless. Broker loss in an already activated
lobby retains native ownership and established tunnels. This adds no readiness
waiver, immediate mid-area admission, reload-free join or peer wire contract.

Session-end timing and containment (integration): the retail quit notices (0x10cc / 0x1106) are queued on the room
tunnel just before the terminal clear wrapper runs. The wrapper therefore invalidates native ownership at once but asks
the room layer to leave through the optional `leave_deferred` seam (`online_room::leave_after_flush`): the leave runs at
the start of the second transport tick after the request, once the tick that was already running has flushed the notices,
and the tunnels close with linger so reliable data still in flight is delivered. An explicit Leave, Cancel or a new
room operation completes or disarms a pending deferred leave; without GNS tunnels it leaves at once. The wrapper's whole
post-teardown body is exception-contained (it runs inside retail frames 0x6123e0 / 0x612c40 / 0x6133a0 that have no
unwind tables); a failing room leave is logged and never unwinds into the game. Fixtures: `r6-end-*` assert the leave is
still pending in the retail frame and happens one tick later; `r6-end-quit-throw-*` / `r6-end-loss-throw-guest` inject a
C++ exception into the room seams and require that none reaches the retail chain; `online_room_test` covers the
deferral arithmetic.

### LC adapter work

Require the additive `lc-online-native/1` probe for this route. Keep intent 19
on Open and use it for Host settings entry rather than fixture-only scripts;
all settings themselves are selected by native Game Options. Preserve the
native Game Options/save/text-entry return chain, paired menu halves, native
Host/Join class/roster/wait/transfer modes, focus links and mouse rectangles.
No LC checkout was edited in this DLL-only round.

At the existing Host status/preview anchors add initially hidden localized
nodes `lc_host_settings_unknown`, `lc_host_new`, `lc_host_load`,
`lc_host_campaign`, `lc_host_danger_room`, `lc_host_easy`, `lc_host_normal`,
`lc_host_hard`. Compose the player-count label from numeric DATA
`lc_host_max_players`. New/load and campaign/mode selectors are independent;
unknown difficulty selects none. The runtime clears the old fixture prose in
`campaign`, `save`, `preview_save`, `preview_slots`, `drop_in`, `preview_drop`.
Replace it with these localized variants; retain the original approved geometry.
`lc_host_drop_on`, `lc_host_drop_off`, `lc_host_drop_unknown` report the effective
installed rule-26 policy. This is read-only; the UI does not change that policy.

The DLL hides `lc_broker_room` while a native status node is shown, so LC can draw the four native nodes at the
same status anchor as the room line without the two overlapping (fixture `r6-native` asserts exactly one at a time).

Add status nodes `lc_native_tunnel` (Connecting to host),
`lc_native_discovery` (Finding the host's game), `lc_native_admission`
(Requesting native admission), `lc_native_posting` (Opening native lobby), and
`lc_notice_admission` (Setup/admission failed; go Back and try again). Keep the
existing generic inline fallback when a specialized node is absent. Do not
advertise broker membership as native admission, readiness or Playing. Use
"waiting for the next area" for late joins; no fabricated transfer percentage.

### Host-fixture and live verification

`r6-native`, `r6-lobby-new`, `r6-lobby-load`, `r6-generations` run in the mapped owned PE. They
execute retail Game Options tick, difficulty dialog, native game-type/max/
difficulty handlers, name accept polling/setter, setuphost, discovery request
and reply allocation/insertion, `0x60a0a0`, accepted/refused native response
handlers, Host/Join activation/refresh/Ready/Back and native Leave. The generation case also executes the actual console
openmenu handler and delays old commands/activation across a newer opposite
role; denied mouse/Back/refresh/base-input are tested before recovery. Dialog,
input, save-service results, participant factories and transport services are
invented. Loading a real save from disk and actual rendering are not inferred.
The twelve `r6-nit-*` cases cover the alternate retail setupHost script and
console handler, canceled/replacement script commands and mixed producer FIFO,
real session teardown for both roles, more than 16 unactivated legacy requests,
24 canceled replacement Hosts, delayed same-role activations, another member
beside the host discovery record, and a tunnel that succeeds at 44 seconds or
expires at 45 seconds. The two end cases now drive the retail quit caller
chain rather than calling `0x612960` directly. The twelve `r6-end-*` cases add
Host/Join quit through `0x5f27a0 -> 0x6133a0 -> 0x612c40`, local disconnect,
and guest host-loss through `0x612c40 -> 0x6123e0`, all reaching the real
participant-clear routine. Separate direct running/lobby guest-removal cases
reach `0x612dda` and `0x612d1f`; host records use native index zero. They also
execute remote-only removal, the effective
existing rule-26 solo-host branch, keep-local clear and campaign-start cleanup
`0x609400` without leaving the room. Native argument tokenization, unmarked
command refusal, noncurrent options and command-ID reordering are covered.
Destruction of invented participants and UI/network/scheduler services remain
invented; this does not claim a real gameplay launch, save, JIP transfer or rendering proof.
The production broker fixture separately drives current-generation target
checks, real in-memory GNS socket pairs, v2 max/mode/joinable metadata, raw-UDP
refusal, stale packets/status callbacks and existing broker-loss survival.

G/V remains a separately authorized release gate; no game launch or desktop
input is part of this round. After LC integration and review:

1. Before each boot follow the common HOLD-GAMES/external-process checks,
   slot.py --wait, private/background copy, registry export/byte-identical
   restore and no-focus rules. Use NoActivate only once supported by that DLL;
   this HEAD does not contain that setting. Never drive the owner's desktop.
2. Fresh direct executable entry, real controller acceptance -> Hub -> Host.
   Exercise new/load, every difficulty/max choice, name accept/cancel, native
   save cancel and posting failure. Confirm accepted metadata and a usable
   native Host with no launcher keys/manual list step.
3. Private Code and public Browse each 3/3 across real peers, including hotspot
   and forced relay. Verify a tunnel precedes discovery, actual native admission
   precedes Join/Ready, and malformed/duplicate/absent host discovery has no fallback.
   On hotspot/forced relay, verify Connecting survives 15 seconds and allows
   negotiation through 44 seconds; a tunnel still unavailable at 45 seconds
   cancels cleanly. Discovery/admission/activation retain separate 15-second waits.
4. Exercise Host alone, unready Start block, all-ready Start, duplicate Ready,
   Unready, pending/ready Leave, native next-area wait/solo watcher, Kick, broker
   loss with healthy P2P and Host -> Leave -> Join -> Leave -> Host. Cancel while
   each lookup/open is pending; delayed old replies/opens, including same-role
   replacement and mixed legacy/scripted setup commands, must remain inert.
   After a real gameplay session, quit to menus independently as Host and as
   Join; also test local disconnect and host loss. Without visiting the Hub,
   open native Join Game and verify that LAN/master games are visible and the
   old broker room/code filter is gone. Ordinary area changes and JIP/cohort
   transfer must retain the room instead. Try queued setup commands while a
   native save/name dialog is current and unmarked typed/usecmd setuphost
   before a queued marked Post: neither may post or consume another command.
5. After ordinary joins play >=10 minutes and cross two transitions with zero
   desyncs. Capture actual 02/04/05 and native service intermediates at 4:3 and
   stretched 16:9, keyboard -> pad -> keyboard, mouse rectangles, 720p/1080p,
   safe bounds, focus, wrapping and role-specific prompts against approved frames.
6. Stop all owned helpers, verify registry/input hashes, and remove only created
   copies/profiles with checked absolute Python paths. Record missing proof as
   pending; host fixtures do not establish G/V or WAN acceptance.

## Round 8: in-session Party screen (probe `lc-online-party/1`)

The pause menu's renamed Players row opens the base screen `lc_party`; Invite/Copy, the host's Kick and Leave, the
Propose-load placeholder and the rail's native routes are specified in [online-party-menu.md](online-party-menu.md)
(intents 63..74, authority, agreed departure, live procedure). The DLL needs no change for the rename itself; LC authors
the rename and the screen only when the DLL carries the probe.

## Round 15: the complete error table (probe `lc-online-errors/1`)

Presentation and already reviewed lifecycle calls only: no new network behaviour, no rule or wire change. The DLL still
shows/hides named nodes and binds DATA text; LC authors every sentence (docs `ONLINE.md` "In-game messages").

**Causes (`error_cause`, `lc_broker_err_<key>`; one scene per failure).** The first eight keep their `lc-online-broker/2`
meaning. `online_menus_rules::cause_table` is the one table: key, the next action the copy names, and whether Retry shows.

| Key | Ids that select it | Next action | Retry |
|---|---|---|---|
| `code_not_found` | `no_such_room`, `code_not_found` | edit code | no |
| `code_expired` | `room_closed`, `code_expired` | edit code | no |
| `room_full` | `room_full` | browse | no |
| `room_closed` | `room_not_joinable` | retry | yes (bounded by the shared limiter) |
| `version_mismatch` | `version_mismatch` | back | no |
| `rate_limited` | `rate_limited` | retry | yes, after the service's wait |
| `timeout` | `timeout` | retry | yes |
| `offline` | `broker_unreachable`, `worker_unavailable`, `send_queue_full`, `inbox_overflow` | retry (LAN/IP still work) | yes |
| `refused` | `broker_rejected`, `bad_auth`, `auth_failed`, `unsupported_version` | back | no |
| `pin_mismatch` | `tls_pin_mismatch`, `invalid_certificate` | back (no bypass) | no |
| `test_mode` | `test_mode_on` | back | no |
| `unsupported` | `online_dependencies_unavailable`, `native_discovery_guard`, the two room-guard sentences | other ways | no |
| `busy` | `transport_identity_busy` | wait, then repeat the screen's own action | no (the client's refused phase has no Retry: `online_room::retry()` accepts only retry_wait/error) |
| `invalid_name` | `invalid_room_name` | back | no |
| `generic` | everything else, including `malformed_reply`, `internal` and hostile ids | retry | yes |

Retry is **hidden** where it cannot help (and in the refused phase), and **disabled** (but visible) while the shared 2 s limiter /
escalating backoff runs; `broker_error` then counts down (`Retry in 7 s`) - only for a failure Retry can help - and returns to the
diagnostic. Browse's Refresh is disabled for the
same wait (`broker_api::retry_in`; absent seam = no countdown, as before). The limiter is the client's own
(`client_state::retry_in` is the arithmetic of `may_lookup`); Refresh cannot bypass it.

**One failure per generation.** `client_state::fail` now ignores a second report for a request that already failed (a
duplicate or late reply neither replaces the cause nor grows the backoff). Refusals (`refuse`, a new attempt's guard) and a
committed room's `disconnected` are unchanged. The room's service loss never ends healthy P2P.

**Redacted diagnostic.** `broker_error` is `<operation>: <known id>` (`join: room_full`), `join: other` for any id that is not
in `known_id()`, `join: install_guard` for the guard sentences. It never carries broker text, codes or addresses. For
`version_mismatch` it names both builds instead (`LC36 vs your LC37`; the host's token is `client_view::error_detail`, set
from the already validated resolve reply and cleared with the failure; tokens <= 8 allowlisted characters each).

**Native step failures** (`native_flow_error(why)`, every id registered in `native_failure_table`; a test scans
`online_native_flow.inl` so none escapes): inline notices `lc_notice_discovery` (host not found / ambiguous / changed),
`lc_notice_refused` (admission refused; `lc_notice_refusal_rules` + DATA `lc_refusal_rules` when the agreement layer named
the rules - `gameplay_agreement::refusal_rules()`, read-only, "possibly" because rule-summary buckets collide, <= 44
characters), `lc_notice_timeout`, `lc_notice_service_lost` (room generation or service lost mid-setup) and the general
`lc_notice_admission`. The reason and phase go to the log (`native flow notice ... (phase: reason)`), never to the screen.
The existing cleanup (leave the room, request `lc_host` from setup) is unchanged; Retry is the screen's own Done/Open.

**Clipboard.** Paste distinguishes `lc_notice_clipboard` (could not open or lock it) from `lc_notice_paste_invalid` (it holds no
text, e.g. empty or an image, or no code); valid typed text is never erased. `read_clipboard_result` is the tri-state;
`read_clipboard` keeps its optional form.

**Lobby inline (the lobby is never closed).** `lc_lobby_error_copy` / `lc_lobby_error_target` name the cause of the old
`lc_lobby_error` (which remains the fallback when LC authors neither); `lc_lobby_guest_left` + DATA `lc_toast_name`: a guest
who disappears from the native roster (matched by native id AND name; the local member never) is named for six seconds,
duplicates ignored, "the party continues; they can rejoin with the code"; `lc_lobby_service_lost`: a room is committed and
the service is gone (persistent strip, P2P continues, only new players cannot join).

**Host loss (0.6).** The session ends (native play ends as before); there is **no takeover and no recovery claim**.
`broker_api::host_lost` (online_room `host_loss_tracker`: this joiner's connection to the room's host ended AFTER the game
session ran over it (transport bound); a fresh connection to the host clears it; a local Leave/Quit (`leave_after_flush`) or
`online_room::suppress_host_lost()` - for the code that learns the host removed this PC on purpose, a kick - blocks it for the
rest of the room; reset on leave) is read in `native_session_cleared` for a guest session, before the room leave resets it, and raises the
Hub's modal panel: `lc_hub_host_left_veil` + `lc_hub_host_left` + buttons `lc_hostleft_again` (intent Host: Host setup,
where a new or saved campaign is chosen) and `lc_hostleft_back` (dismiss/Back). While it is up the Hub's rows are disabled
and every other posted action is ignored; a second report of the same loss changes nothing; a deliberate local leave never
raises it. The panel shows **no save claim**: 0.6 has no accessor for local saves, so the capsule says to choose a saved
campaign in Host setup. It appears at the next Hub entry (no automatic menu is opened from the session-end frames).
Not detected (and so never claimed): a normal host quit whose notice clears the session before the connection closes, and a kick
(round 8 must call `suppress_host_lost()` from the path that learns it). The retail terminal clear is the same code for a local
quit, a host-quit notice and a loss, so it cannot tell them apart; only the connection state can. Live proof of the ordering stays
pending. Rule names of a refusal are shown only when `refusal_serial` moved during this attempt (the counter the agreement layer
bumps on every refusal dialog, LAN browsing included).

**Deviation from 07-host-left (coordinator decision).** 0.6 keeps Host again always available (Host setup lets the host choose a
new or saved campaign) and the capsule makes no claim about a local save (0.6 has no accessor for one); the frame's
"AVAILABLE LOCAL SAVE / No local save available" and the disabled Host again are not implemented. The panel ends with any route out
of the Hub (a launcher preset included), is dismissed when its nodes are not authored (assets of an older LC keep the Hub usable),
and disables the Hub's prompt items (desctext3/4) as well as its rows. A cause the assets do not author falls back to the nearest
true scene (pin_mismatch -> refused, code_expired -> code_not_found, the rest -> generic). Mid-session Load is not in this release: nothing offers it.

Tests: `xml2_online_menus_test r15-errors` (table-driven id -> cause -> scene/next action/Retry for every row on four
screens, redacted diagnostics with hostile ids, limiter countdown, retry without restart, duplicate/late errors, build pair,
native failure registry, clipboard tri-state, toast/roster/host-left models), the native fixture's refusal/timeout/discovery
notices and the `r6-end-*` guest modes (host-left panel only for a lost host, modal, once; `r6-end-kicked-guest`: the
real `host_loss_tracker` told `removed_by_host()` before the bound host connection ends -> the END path raises no panel
and the Hub stays usable), `xml2_online_browser_test`
(limiter arithmetic, one failure per generation, detail), and LC `test_online_errors.py`.

## 0.6 preview fix E: mouse hit boxes, the kicked guest, the lobbies' pad buttons (probe `lc-online-hits/1`)

**Mouse hit boxes = the drawn focus bars.** XMen2.exe's menu mouse (base CMenu vt+0x80 `0x5ae0a0`; the lobbies' `0x5ba2b0` /
`0x5c8da0` fall through to it) receives the point in view units (`0x5f9eb0`: `x = left + px * width / [0xa09ffc]`,
`y = 384 - py * 384 / [0xa0a000]`, integers) and asks each shown, focusable item (`+0x54` bit 3) with its own vt+0x74 (text
`0x5c6490` -> `0x5bc1b0`). The item answers with the rectangle its text is laid out in (`0x5bc530`: `+0x70/+0x72` anchor +
box, `+0x74/+0x76` size, `+0x7a` the box top relative to its anchor, halved into the test), which cannot be widened without
moving the text. LC's bars are larger (an ordinary row: 12 view units left and right, 5 above, 2 below), so the top and both
ends of every bar were dead (live 060p: Join by code responded x 249-594 under a bar x 225-630 at 1280x720). The builder now
writes `Data/lc_online_hits.txt` (`lc-online-hits/1`, one line per focusable item: `menu item left bottom right top`, absolute
view units, y up; the Kick picker LISTBOX is not listed - it hit-tests its own rows). On our screens only (the base mouse slot
`0x69e49c` for the custom screens, the existing lobby mouse adapters for the skinned lobbies) the DLL picks the item whose bar
holds the point - the focused item's bar when bars overlap (only that bar is drawn), else the bar whose text box is nearest -
and hands the engine a point inside that item's own rectangle that no other listed item holds (the engine's focus, hover sound
and click code stay retail); a point in a listed item's rectangle but outside every bar becomes a point no item holds; anything
else, and every other menu, is passed on unchanged. With the screen pixel of the message in flight (`display::pointer_pixel`,
set by the fix's window procedure around the engine call) the test is pixel-exact against the drawn bar (Direct3D's top-left
fill rule: the pixel centre inside, top and left edges included); without it, to half a view unit. A missing or malformed table
(any bad line refuses all of it) keeps the engine's hit test. Host modes `r16-hits` (5 resolutions x hub + host lobby, every
edge pixel and the pixel past it, a whole-screen scan, clicks raise Accept / the prompt's token code through the retail item
code), `r16-hits-unit`, `r16-hits-absent` (the 060p dead edges reproduced), `r16-hits-refused`.

**The kicked guest.** The host's native Kick (`0x6133a0(record, 0x10ce, 0x10cf, 0x10cf, 1)`) sends the removed participant the
removal notice 0x2b (`0x611550`: its index, reason `0x10ce`, flag 1); the guest's handler `0x612f60` removes the named record
through `0x612c40(record, reason, flag)`, whose terminal clear calls the patched `0x612070` sites with EBP = `0x612c40`'s frame.
The three call sites now land on a stub that keeps EBP; `native_session_cleared` recognises "entered from the 0x2b handler
(return address `0x612f8e`), for this PC's own record (`0x614290`), reason `0x10ce`, flag 1" (`kicked_by_host`) and shows
`lc_notice_removed` ("The host removed you from the game.") at the next Hub tick instead of the host-left panel. The slow-player
drop's `0x10ce` with flag 0 and every host loss keep their behaviour. The host's GNS close reason "removed by the host" is not
used: on a room tunnel `declare_gone` only unbinds the transport (`gns_transport.cpp close`: `set_transport_bound(false)`), so
the guest never receives it; the in-session Party Kick (round 8, `remove_member`) therefore still reaches the removed PC as a
silent host (no distinguishing signal on that PC). Host modes `r6-end-kicked-guest` (the notice through the retail 0x2b
handler, with the room layer's host-loss latch SET), `r6-end-slowdrop-guest` (flag 0: the panel, no removal notice).

**The skinned lobbies' pad buttons.** The custom screens' scheme (`pad_menu_mask` / `pad_prompt_code`, fix C) now covers the
skinned Host / Join lobbies (`lc_lobby_v1`): while the lobby's player (the manager's controller `0x5d8570`) is shown the pad, the
lobby tick and the native lobby tick it wraps read pad X as MENU_OTHER (guest Ready / Unready, host Kick player) and Y / RT as
MENU_NEXT (Copy), and `pad_prompt_scope()` draws `$MENU_OTHER` / `$MENU_NEXT` as X / Y there. The keyboard (Del / KP5), an
unskinned native lobby and Prompts=off are unchanged. Host modes `r4-pad-host`, `r4-pad-join` (the real native Host / Join
ticks with masks built from the retail code -> action table).
