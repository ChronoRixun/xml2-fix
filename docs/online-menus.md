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

**Browse rows** (`online_menus_rules::browse_rows` = 7 rows per page, R=0..6, shared relative art; the co-op installer requires the `menu-anchors/512` capability, see below): `lc_rowR_st_{lobby,progress,closed,full}` (status kind),
`lc_rowR_pips_{0..4}` (players, unknown = 0), `lc_rowR_build_{same,other,check}`, `lc_rowR_dim` (shown for full,
closed, other-build and password rows) and the existing `lc_rowR_selected`. Status: players >= max -> full; started ->
progress; not joinable -> closed; else lobby. **There is no `loading` kind**: the broker has no such field, so none is
invented. Build: SAME only for a known equal token, OTHER only when both tokens are known and differ, otherwise CHECK.
Cells `room_R_4`/`room_R_5` no longer exist as DATA: the status word, the ping `--` (no measurement exists and no
connection is opened per row) and SAME/CHECK are drawn by the row scenes; `room_R_6` holds only the other build's
token (<= 8 characters). The hit/focus cells are columns 0,1,2,3,6; the five cells of a row share one focus anchor `room_R_focus`.

**Scene anchor capacity.** Retail `0x5882e0` / `0x587f90` / `0x587e40` use 175-entry fixed vectors; the 182nd anchor overwrites the count and the next push writes through that pointer (the 060p Browse crash). With Menus enabled, `online_menus_anchors` relocates the five-slot playfield manager into DLL storage, using stride 0x2000, 512 name/value pairs and 512 permanent scene-object pointers per slot. The temporary stack vector, sixteen-word masks, allocator metadata, pointer division, CRT constructor/destructor references and exception cleanup offsets all follow the expanded layout. The push refuses entries after 512 and the scene walk discards the entire overflowing binding set. Capacity and dispatcher writes share one guarded transaction; a failure retains native Play Online, and Menus off prepares neither contribution.

The shared retail mask helper `0x5874a0` also serves a caller at `0x5c6467`, so its 175 bound remains original: only the two playfield callers use the 512 helper. LC probes the supplied DLL for `menu-anchors/512`, uses 175 without it or 512 with it, and refuses seven-row authoring without the probe. `anchor_count`, `scene_problems` and `output_problems` still enforce the effective bound. The `r10-anchor-512` host fixture runs the real loader, recursive walk, push, insert, lookup, unload and allocator code, checks 512/513, five slots, reference balance, canaries and handle reuse. `r9-anchor-capacity` retains the unpatched overwrite control. A real-game smoke of every native and co-op menu remains required because playfields are shared by all menus.

Area shows `--` when the
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
with backing tables. A marked skin missing either returns to the plain LC failure page
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
pipe OFF/ON and NoActivate OFF/ON delivery in both focus states, failed-read
reset on the player path, and unrelated size/device/null pass-through. Agent
reads with NoActivate never call the native keyboard device; they start empty
and merge only pipe keys. The player kit also enables InputPipe: focused physical
keys remain available there, while unfocused pipe reads are synthesized even if
the foreground device is unacquired. Cooperative flags
stay unchanged, so no background physical keyboard access is requested.
Only OS foreground answers and the UI consumer
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

Missing-list activation queues the plain LC failure page rather than calling
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
waits synchronously for the broker or a tunnel. Missing, duplicate, zero-port,
wrong-address, disabled or incompatible records never select master/plain-IP
fallback. Native discovery is reset before searching, queries only the proven
host virtual address, and never keeps a record pointer across frames. Records
from another room member are ignored; only records sourced from the proven
host tunnel count toward uniqueness. A duplicate or invalid host record still
fails closed; zero host records waits until the discovery deadline.
A host whose local port 5165 is occupied may bind an ephemeral stock port.
Discovery accepts that nonzero port from the proven tunnel peer; native
admission preserves it. The port is the tunneled stock socket endpoint, not a
public NAT mapping or relay port. Host-local contention can affect two-PC play
as well as two copies on one PC. The native regression executes the retail
reply parser and admission with ports 50123, 65535 and 1, and rejects zero.


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
| `room_closed` | `room_not_joinable` without conclusive fresh listing context | retry | yes (bounded by the shared limiter) |
| `session_started` | `room_not_joinable` plus the client's fresh started/unjoinable/nonfull listing inference | ask host to return to lobby | no |
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
the guest never receives it. The in-session Party Kick (round 8, `remove_member`) now tells the removed PC with rule 64's
removal notice (fix F, [online-party-menu.md](online-party-menu.md) section 4), which reaches this same path as the native
0x2b of the PC's own index; a session end after an accepted notice that comes another way is classified by the room layer's
latch (`broker_api::removed_by_host`). Host modes `r6-end-kicked-guest` (the notice through the retail 0x2b
handler, with the room layer's host-loss latch SET), `r6-end-slowdrop-guest` (flag 0: the panel, no removal notice),
`r6-end-partykick-guest` (fix F: rule 64's native body through the retail handler), `r6-end-partykick-seam-guest` (fix F: the
notice accepted, the session ended by a host-connection loss: still the notice, never the panel), `r6-end-kicked-twice-guest`.

Review E follow-ups (fix F). (1) The removed record is compared with this PC's own participant record cached while the
session was live (`native_flow.local_record`, from `0x6111f0` / `0x614290` at admission, in the lobby tick and the Party
frame): by the `0x612070` call `0x611f50` -> `0x611e90` / `0x611c10` has already run the record slot's destructor, so the
record's own flag (+0x1c) is only a fallback. The cache matters for the LOBBY kick (0x612daa removes the local record through 0x611f50 before 0x612dda); in a running zone the record is not destroyed first. Refreshing it calls 0x6111f0, which writes session+0x78 / +0x274 while +0x78 is still 0 (the engine's own first-call writes; the menu tick runs outside any engine iteration). A pointer gone stale during play only falls back to the flag read, and a Party Kick has the rule 64 latch. The fixture's record removal now destroys the record in the same order (clears
its flags) before the classification runs, so a classification that reads the record fails the kick modes. (5) A new Host or
Join attempt clears a removal notice that never reached a Hub. (6) `r6-end-kicked-twice-guest`: after the kick the native quit
route runs the real `0x6123e0` again (wrapper entered twice); exactly one classification line, one deferred leave, the notice.

**The skinned lobbies' pad buttons.** The custom screens' scheme (`pad_menu_mask` / `pad_prompt_code`, fix C) now covers the
skinned Host / Join lobbies (`lc_lobby_v1`): while the lobby's player (the manager's controller `0x5d8570`) is shown the pad, the
lobby tick and the native lobby tick it wraps read pad X as MENU_OTHER (guest Ready / Unready, host Kick player) and Y / RT as
MENU_NEXT (Copy), and `pad_prompt_scope()` draws `$MENU_OTHER` / `$MENU_NEXT` as X / Y there. The keyboard (Del / KP5), an
unskinned native lobby and Prompts=off are unchanged. Host modes `r4-pad-host`, `r4-pad-join` (the real native Host / Join
ticks with masks built from the retail code -> action table).


## 0.6 preview fix E2: the hit table, hardened (review findings 2, 3, 4, 7 and the mouse half of 8)

**The engine's mouse walk, as read from XMen2.exe (`0x5ae0a0`).** It is NOT "start after the focused item and wrap". Pass 1
(hover) asks only `desctext1..5` (names at `0x69e36c` .. `0x69e33c`) with the UNSHIFTED box (`left = +0x70`, `top = +0x72 -
+0x76 - 1`, `right = +0x70 + +0x74 - 1`, `bottom = +0x72`; focusable and shown), sets the hover index `[0x6e4b18]` (drawn by
the vt+0x68 refresh `0x5ae6b0` with the `~~` style) and plays the hover sound unless the index is unchanged. Pass 2 walks the
menu's item tree IN ORDER - the container at `menu+0x824` (nodes at `+0xc + 16 n` = parent, left, right, key; root index at
`+4`; item pointers at `+0xde8`), leftmost node `0x585be0`, then successors `0x585c10`, ordered by the item NAME'S KEY, not by
anything we control - asking each shown, focusable item's vt+0x74 (shifted box, `+0x7a / 2`); the first to answer wins and
takes focus. The lobbies' `0x5ba2b0` / `0x5c8da0` add their own first pass over their buttons and over `text_playername1` (the
Kick picker: asked whatever its shown / focus state) with the unshifted box, then fall into `0x5ae0a0`. For Legends Classic
items `+0x7a` (the box top relative to the anchor, `0x5bc530`: the MAX corner's z) is 0 on rows and prompts, so both boxes are
the same there; it is nonzero only for items authored with a `hit_rect` (the lobby buttons, 4.8 -> a 2-unit shift).

**Hover = click (finding 2).** The remapped point is now chosen inside the target's boxes of BOTH passes (their
intersection), so the hover index, the hover sound and the click all name the target; if the two boxes do not intersect the
shifted box alone is used (the click wins; the engine itself could not hover such an item from the same spot). A point in a
listed item's first-pass box but outside every bar goes nowhere (no hover).

**Every focusable item counts (finding 3).** `scoped_point` walks the menu's real item tree (`engine_items`, the same two
retail functions) and treats every focusable item - listed or not, shown or not (shown only if no point is otherwise free) -
plus `text_playername1` as a blocker: the chosen point is held by no other item in either pass, whatever order the engine
walks them. A point outside every bar that an UNLISTED item holds is passed on unchanged (the Kick picker's rows keep
working); only a listed item's own dead strip becomes "nowhere". The builder refuses a table in which a bar reaches the
picker's rectangle (`hit_table`).

**Parser and builder (finding 4).** `online_menus_hits::parse` reads numbers with `std::from_chars` (strtod follows the C
locale), names the line and reason (`line 7: bad entry`) and additionally refuses a BOM, CR, lines over 256 bytes and a
duplicate (menu, item). A refused table is logged once and LOUDLY (`online menus: ERROR: Data/lc_online_hits.txt REFUSED
(...); the mouse hit boxes of the co-op screens are OFF ...`). The builder checks the same rules (`hit_table_problems`,
raised at build time by `hit_table`, repeated by the `v_online_menus` validator on the written file) and its output is fed
through the DLL's own parser (`xml2_online_hits_parse_test`) by the legends-classic tests, comparing accept / refuse verdicts.

**Tests (finding 7).** `r16-hits` / `r16-hits-orders` run the RETAIL `0x5ae0a0` and `0x5ba2b0` (and `0x585be0`, `0x585c10`,
`0x5ab7d0`, the item hit tests) in the mapped image over a real item tree (right-leaning chain or balanced tree, 12 key
orders); only the name -> key function, set focus, the hover refresh, the hover string, the sound call and the lobby
navigation `0x5bb150` are replaced, and each item's vt+0x74 is a recorder that forwards to the retail test. They assert the
answer, the walk order (the asked prefix equals the key order), the hover index and hover sound, an unlisted focusable item
inside a bar and away from bars, and a synthetic prompt whose boxes differ. `r16-hits-parse`: CRLF, BOM, long lines,
duplicates, malformed fields, locale-independent numbers, refused-table logging. `r16-hits-unit`: no pixel, a wheel
message, a stale pixel. legends-classic `test_hit_table_matches_the_focus_bars_as_built` compares the table with the focus
models' vertices placed at the built scene anchors (not `focus_placement` again). Finding: a wide solid focus bar LEANS (its
top edge is 4 view units right of the bottom edge: `FOCUS_LEAN`); the table lists the bottom-edge rectangle, so up to 4 units
(7.5 px at 1080p) at the top-left and top-right of a bar are wrong in opposite directions.

**Fallback logging (finding 8).** The half-view-unit fallback logs once per reason: `no window procedure pixel (display Mode
unset)`, `a mouse message without a window procedure pixel (wheel, or no client point)`, and `the window procedure pixel
does not match the engine's point`.

## 0.6 preview fix I: messages, lobby focus, small items (060q live failures 3, 4, 7, 9, 13)

**Why the messages were blank (failures 3, 4, 9).** Every per-cause error scene, the lobby's service-lost strip and its
guest-left panel were `MENU_ITEM_MODEL` scenes authored `hide="true"`. The engine's model item (vtable `0x6a0304`) parses the
`hide` bit first and installs the model second (`0x5c5080` -> `0x5bbdd0` -> `0x5c4c50`); with the bit set `0x5c4c50` loads
`ui/models/m_invis.igb` and releases the requested model, so a model that starts hidden keeps only the invisible stand-in, and the
DLL's later `setHidden(false)` (`0x5c4b90`) finds the stash at item+0x94 empty and restores nothing. A model that starts visible
and is hidden by the DLL stashes the real one and gets it back (the Host visibility pair: PRIVATE came back, PUBLIC never drew).
Plain text items un-hide correctly. So, in the LC assets: each `lc_broker_err_<cause>` is ONE hidden text item with the cause's copy
in its warn colour (the DLL's `hide_error_scenes` is unchanged: it hides / shows them by name through the text setter), `retry` is
a default-colour label (it was authored dark for a baked bar), and `lc_lobby_service_lost` / `lc_lobby_guest_left` are text items
(the departed guest's NAME stays the DLL's data node `lc_toast_name`, the copy sits on the line below it).

**Lobby message row.** The toast, the Copy / Kick notice and the Leave confirm draw on the row the ready line owns (the old
panels covered it): while any shows (`row_busy`), `lc_ready_summary` / `lc_ready_waiting` / `lc_ready_all` / `lc_ready_solo` are
hidden and `lc_ready_count` / `lc_member_count` / `lc_waiting_name` are empty. A card keeps its phase word "In lobby" when the
party facts are unavailable (a member observed in the native lobby roster is in the lobby: service loss emptied it).

**Lobby focus (failure 7).** `lobby_focus_guard` (every lobby tick after the visibility is bound): while the Leave confirm is up the
focus is `lc_leave_yes` / `lc_leave_cancel` (Up / Down shared the lobby's ring and carried the bar to Copy code or KICK PLAYER
while the confirm stayed drawn); while it is not up the focus is never on either button, and never on `lc_lobby_dismiss` unless
that notice shows (a bar on a hidden item draws nothing). "Code copied" is informational: it expires after 6 s
(`lobby_copied_ms`), Back / Esc / pad B ends it AND reaches the native Back (before, the first Esc was eaten by a notice the
player could not see); the Kick-target / Copy-failure notice keeps the "first Back dismisses" rule. Host modes `r4-focus-host`,
`r4-focus-join` (the real native Host / Join ticks and Back slots; every `lc_*` node is its own item so the compares are real).

**Small items (failure 13).** A clipboard text of the size of a code (8, or 4-4 with the dash) with a symbol no code has
(`AB0O1IL!`) is `clip_result::bad_symbols` and shows `lc_notice_code_invalid`; any other text / no text keeps
`lc_notice_paste_invalid`. A screen name that does not fit its field ends in "..." (`fit_name`: hub name 140 units at title size,
header name 64 / recent game 180 at row size; Barlow advance estimates, errs toward shortening). Join by code's Paste / Clear
prompts are the keyboard form in the asset (`Ctrl+V Paste`, `Shift+Del Clear`) and the DLL sets `desctext3` / `desctext4` every tick
to `Y Paste` / `X Clear` while the pad is shown (the engine rewrites the slots on every focus change). Pad B in the name entry:
the entry is the engine's own `TEXT_ENTRY_MENU` (vtable `0x69f06c`, opened through the name request `0x60c7e0`); our code never
sees its input and the hub does not tick behind it, so B stays the engine's - see the report's open items.

## 0.6 preview fix G: the joining crash, Browse Join, model scenes authored hidden

**The joining crash (060q: every Menus=1 guest, 3/3, ~1 s after the GNS link).** The join lobby's activation adapter was
written into `0x69f350` = the join class's vt+0x14 (the base CMenu enter callback `0x5af410`, no stack argument) instead of
`0x69f34c` = vt+0x10 (the class activation `0x5c8590`, `ret 4`; the host's `0x69f284` was right). The menu manager's transition
`0x5d4c70(manager, menu)` calls vt+0x14 with no argument; the adapter's `ret 4` took one dword more, so `0x5d4c70`'s own
`ret 4` returned into the menu object (private heap): the three different faults of 060q (writing 0x42, reading 0xFFFFFFFC,
privileged instruction) are three menus' bytes executed. The guard bytes had been taken from the wrong slot (`10f45a00` =
`0x5af410`), so the transaction verified it, and the fixtures called the patched slot directly WITH an argument. Now the
adapter sits on `0x69f34c` (guard `90855c00`); the guest's join activation is wrapped for the first time (room line, code,
cards). Host modes `r4-native-slots` / `r4-native-slots-join` drive the slots the way the engine does: the retail
`0x5d4c70` for vt+0x14 (it must reach the retail `0x5af410` and return), and a native-ABI caller (callee-cleaned pushes,
ESP compared) for activation, tick and the three refresh slots; vt+0x14 must stay `0x5af410` on both lobby classes.

**Browse Join (060q: Enter / pad A / click / the Join prompt did nothing).** The row command (24+R) only selected the row, and
the Join prompt's single resolve call was refused without a word while the shared 2-second lookup limiter was closed or a
list was in flight - after every list, i.e. the normal case (the fixtures advanced their clock 2 s before every Join). Now
the selection follows the focus over the rows (CMenu's focused item `menu+0x324` is one of `room_R_C`), the command of the
selected row joins it (a row command for another row selects it first), the prompt joins the selected row, and a Join is
held - no refresh starts meanwhile, an in-flight list is cancelled - until the broker client takes the lookup (<= 2 s; 5 s
timeout, logged). The resolve then joins exactly like Join by code. Log: `online menus: Browse join (row|Join prompt): room
lookup started after N ms ...` or `... not started: <reason>`. Host mode `r5-browse-join`.

**Model scenes authored hidden (060q: Browse STATUS / PING / BUILD empty; Host PUBLIC label, campaign / drop-in and the YOUR
ROOM badge; lobby card frames / badges; the Leave panel).** MENU_ITEM_MODEL's load (vt+0x44 `0x5c5080`, slot `0x6a0348`)
runs the base parse (hide="true" -> setHidden `0x5c4b90` while there is no model yet) before it sets the authored model
(vt+0x38 `0x5bbdd0` -> `0x5c4c50`); on a hidden item `0x5c4c50` draws m_invis and releases the requested model instead of
keeping it in the saved slot +0x94 that the next setHidden(false) restores - the show succeeds and draws nothing (no hide call
was refused: `hide_native` now logs once per item if one ever is). The adapter on `0x6a0348` runs the retail load, then, for
LC online-menu models only (model path `ui/models/lc_o_*`, item still hidden, +0x94 empty), sets the authored model while
shown and hides it again through the retail setHidden, which keeps it in +0x94. Retail models load unchanged. Host mode
`r5b-model-hidden` (the retail setHidden / setModel / load-model code with a fake model manager: the retail load releases
the model; the installed slot keeps it; retail and shown models are byte-for-byte the retail result).

**Crash context.** The crash line is followed by the registers, the faulting region (base / allocation / protect / type),
the bytes around EIP, the EBP chain and up to 40 return addresses found on the stack (a value in a module whose preceding
bytes are a call). With `XML2FIX_CRASHTRACE=1` in the environment (agent live repro only; no ini key) every first-chance
access violation / privileged / illegal instruction gets a line (60 at most, the first 8 with the full context).

## 0.6 preview fix H: the Hub's focused row, MAX PLAYERS, the host's room after Start Game
060q live failures 5 / 8 / 11. (The hidden-authored model scenes of failures 5 / 6 are fix G's model-load adapter on
0x6a0348; fix H's first draft of a builder table + restore was dropped in favour of it.)

- **Hub focus.** LC authors a navy twin `<row>_hint_on` of each focusable row's hint and one hidden help set per row
  (`lc_help_<row>_title` / `_1` / `_2`, the Host row's shown); the Hub tick shows the focused row's twin and help, focus on
  another item keeps the last row's help. `browse_hint_on` is bound with `browse_hint`.
- **Host MAX PLAYERS** reads `--` until native Game Options produced the number.
- **Tests.** r17-hub-focus (real text items, the CMenu focus over four rows, Change name and the Party row keep the last
  help, a new visit starts on the Host help).
- **The host's room survives Start Game.** `native_lobby_departing` treated "+0x1972 and +0x1973 both clear" as the host
  leaving, but XMen2.exe only ever zeroes those flags (0x5b7f8e, 0x5ba919), so every host lobby deactivation - Start Game
  included - left the room silently (060q dh: right after the solo start `online: GNS listening on ...` = no room
  requested; the Party screen then had no room name, invite code or host row and nobody could drop in). The host lobby's
  only native Leave is its tick entered with phase +0x1960 == 2 (set by Back, 0x5bb136; consumed at 0x5bacd6 with the
  session teardown 0x607c40(0)); `lobby_original` latches exactly that and the deactivation departs only after it. Guards
  for the three retail spots. Test: r6-lobby-new / -load deactivate the real host lobby with phase 0 (room kept) before
  the native Leave (room left once).

## 0.6 preview fix K: mouse hit boxes that reach the engine, lobby focus and prompts (060r live failures 8, 9)

### Mouse: why fix E / E2 had no effect live
The 060r probes (live-060r/evidence/mh-*.json, scan-host720.json: 12 px / 3 px steps) put the Hub HOST row's live hit box at
exactly the retail text box (x 246..596, y 212..245 at 1280x720 = item box x[46,233) y[254,272) view units), as in 060q: the
remap never happened on an ordinary row. Cause: fix E2 counted every item with +0x54 bit 3 as one that can answer the walk, but
bit 3 is ENABLED, not focusable (the item parser sets it on every item at 0x5bc981; `enabled="false"` clears it; neverfocus is
bit 5, hidden bit 2). Every focusable LC item has a `<item>_focus` MENU_ITEM_MODEL laid out at the item's own anchor and box
(online_menus.py scene_layout), shown and enabled, so it covered the whole box: `free_point` found no point, `scoped_point`
returned false and the engine got the raw point. A model never answers (CMenuItemModel vt+0x74 = 0x5bc1b0 needs a non-empty
text); neither does an empty text. The Join by code prompt and Browse Refresh's lower half "worked" in 060r only because their
retail boxes moved with fix H's footer layout. The fixtures passed because they had no focus models, labels or panels.
Fix (src/online_menus_hits_rules.hpp engine_item / engine_menu / engine_picks / picked_point; online_menus.cpp
engine_item_of / scoped_point): the DLL models the engine's own passes - the first pass over desctext1..5 (unshifted box,
shown + enabled, no text test: the hover index), the lobbies' pass (0x5ba2b0: start / kick / leave, 0x5c8da0: ready / leave,
then the picker's box whatever its state), and the second pass in the item tree's own walk order, where an item answers only
as its class does (0x5bc1b0 / 0x5c6490: shown, enabled, not hidden, text not empty, PtInRect of the shifted box; any other
class: its box, conservatively). The point handed over is one where that model gives the target the focus, the hover and the
click. Outside every bar the engine's own answer for the raw point decides: an unlisted item keeps it, a listed item's strip
outside its bar is nobody's. When no point exists (a label with text covering a whole item) the bar keeps the engine's test and
a once-line names the item that covers it.
The Leave confirm (er2: hover on Leave left the bar on Cancel, a click on Leave acted as Cancel): its panel model
(lc_leave_confirm, x 34..476) covers Leave / Cancel / Dismiss; with E2 the remap gave up there too. Now a click on Leave is
Leave's own Accept (r16-hits live_060r).
Diagnostic (agent live checks only; `[Test] InputPipe=1`): one line per mouse message on a co-op screen or skinned lobby,
`online menus: mouse <menu> 0x<msg> pixel X,Y of WxH (exact|half a unit) engine x,y view vx,vy; bar <item|->; <remapped |
nowhere (...) | passed | no point the engine gives to it (blocked by <item>); unchanged>; handed x,y; model answers <item>;
after: focus <item>, hover <n>` - the last two are read after the engine's walk. A line equal to the previous is not repeated.
New guards (9): 0x602200, 0x5bc1b0, 0x5c6490, the model / text vt+0x74, text vt+0x5c, 0x5bccd0, 0x5aafd0, 0x5bc981.

### Lobby pad / keyboard (failure 9)
- START GAME stuck: bind_lobby dims START (clears bit 3) while a guest is not ready and the focus may sit on it; CMenu's
  navigation (vt+0x40 = 0x5aafd0) does nothing from a focused item that is not enabled, so every key and pad direction was
  dead. `lobby_focus_enabled`: the focus never rests on a disabled native lobby button - the item's own neighbour search
  (vt+0x5c = 0x5bccd0, which skips hidden, neverfocus and disabled items) moves it to the next control; when START is enabled
  again and the focus is still where it was moved, it goes back (r4-focus-host: the lobby fixture's own START dim).
- No lobby prompt was drawn: the native desctext items kept `animtext_scene="help"` (the native help bar's fade-in, absent from
  the authored scene). LC drops it on every LC-laid native item (legends-classic online_menus.py lobby_tree), as its codex /
  options / pause reskins do. Copy follows the device: `Y Copy code` / `Ctrl+C Copy code` (LC authors the keyboard form).
- F5 on Browse did not switch the prompts back to the keyboard: online_menus::keyboard zeroes the keys it consumes (F5, Page
  Up / Down, Ctrl+C / Ctrl+V) before the game's poll, which is where the device tracker samples. Any new key on a co-op screen
  now calls pad_prompts::note_keyboard (wired by exports.cpp), counted as typed at the next poll.

## 0.6 preview fix L: messages, lobby cards, presentation (060r live failures 5, 6, 7, 10)
Items 7, 8, 9 and 10 of the 060r fix brief (agent L; no launches - the live questions are listed at the end).

- **Host with the service down (failure 5).** `native_flow_tick` read every broker failure while the host flow was posting as
  `room_generation_or_service_lost` ("Lost the online service during setup"). Nothing was lost: no room existed yet. A failed
  open of THIS attempt (same generation, state retry_wait / error / refused, phase posting) is now `service_silent` ->
  `lc_notice_service_silent` "The online service did not answer. Try again."; a room that existed and lost its service, or a
  changed generation, keeps the lost wording. The earlier tests passed because they only drove the room that did open or a
  cancelled / refused native step; r6-native now fails the broker op itself (`menu_broker.fail`) while posting.
- **Guest refused for different settings (failure 5).** The refusal (`game info 0x18 ... refused (reason=6; differs: 29 TickRate)`)
  was only read when an admission reply came back, which a refused game info never produces: the flow ran into its 15 s
  timeout ("Took too long to connect"). A refusal that moves the agreement layer's serial DURING the attempt (tunnel /
  discovery / admission phases) is now `settings_refused` -> `lc_notice_refusal_rules` with the rule names, or
  `lc_notice_refused` when the refusal named none. Stale rules (an earlier or another host's refusal) are still never shown.
  r6-native: a refusal raised after the attempt started ends it at once with the rules, one without rules reads the generic
  refused line, neither reads the timeout.
- **Retry (failure 5).** The state the live probes ran in was never logged. `online menus: Retry hidden / shown, disabled
  (waiting) / shown, enabled (screen N, cause N, wait N s)` is logged on every change; the hit / hover half is fix K's (the
  RETRY item has no code of its own beyond its authored bar: label style, colour 4, hit rectangle = the bar).
- **Guest lobby header / campaign line (failure 6).** `lc_room_name` was only bound from the broker's room name, which a guest's
  resolve reply may not carry (and an Other ways join has no room at all), so it read "Room details pending" for the whole
  visit. It is bound from the host's native row now ("<host name>'s game", the browser's default naming) else "Online game";
  a name the broker gave still wins. The host's `lc_campaign_summary` names what native Game Options chose ("New campaign -
  Normal. Friends join at the next area."); the guest's and the LAN host's authored line no longer says "pending".
- **CHECKING BUILD... forever (failure 6).** An Other ways guest has no broker room, so no card ever got build facts and
  stage 0 (checking) stayed on every card. Without a room the build is not being checked: no badge (stage -1). In a room a
  member without facts yet is still checking. `r7-bounds` pins the no-room case.
- **Host card badges (failure 6, diagnostic only).** The live frames show the host card alternating between {HOST} and {READY,
  You, SAME BUILD} and never all together, with P2 correct. The cause is not established from code or frames, so no guess
  is shipped: `online menus: lobby cards: <role> | P1 id N [HOST] [READY|NOT-READY] [YOU] | ... | local N | confirm open /
  closed | room facts yes / no` is logged on every change of the bound facts (60 lines at most). See the live questions.
- **Third guest bar (failure 7).** The retail Join tick (0x5c8840) writes "Unready" / "Leave" into `text_leaveroom` when the
  ready state flips, and again on each tick the accept is held, over the `lc_guest_leave` / `lc_guest_unready` overlay in the
  same rectangle (2-3 labels on top of each other). A bind cache never notices the engine's write, so the guest lobby blanks
  the native text uncached after every tick. r4-join counts the native writes (cause) and asserts the text is empty.
- **Host setup side marker (failure 7).** The amber slash was baked beside CAMPAIGN. LC authors one hidden marker scene per
  focusable settings row (`lc_host_marker_visibility` / `_room_name` / `_campaign`; DROP-IN is read-only and never focused);
  `host_marker_follow` shows the focused row's and none while the focus is on the Open bar or a prompt. r5b-views moves the
  focus over four distinct row items.
- **Pale text on amber (failure 7).** Two causes. (1) The body and label styles carried green / blue as their highlight, the
  colour a FOCUSED body / label item blends toward (Cancel, F5 / Y Refresh read green): all three styles a focus bar can sit
  under now blend navy -> navy, and the two never-focused alias styles keep green and blue allocated. (2) A focusable item's
  inline `~NN` colour beats the style's focus colour: Next page / Previous page (label, colour 3) carried a prefix although 3
  is the label's own resting colour; no prefix now for a focusable label at 3 or 4. The unselected visibility chip is a navy chip
  (fill + the same pale outline / text), so it reads on the amber bar and on the dark panel.
- **Join by code badge (failure 7).** "Room connected" did not fit the 67-unit badge field ("Room connecte"): the badge reads
  "In a room" (`LC online_menus.ROOM_BADGE_TEXT`; r5b-views pins the DLL's word).
- **Games list row (failure 10).** The refused host also reaches the list by a second route that files its row with no
  agreement check (only the direct / LAN 0x18 is guarded), so the reason only ever replaced an EMPTY list. The refused sender's
  address bytes (`native_address(sender)`, the bytes the games table copies into the record at +8) are remembered; while the
  refusal is current the games list's update names that record's row by the reason (fitted to the record's 37-character name
  field) before the retail refresh builds it. Another host's row is never touched. The live round must confirm that the
  record's +8 bytes equal the sender's (the log line `the refused host's row in the games list is named "..."` appears only
  when they do; otherwise the list is exactly what it was).

Live questions for the next check: (1) the `lobby cards:` lines of a host with a guest, with and without the Leave confirm
open (which facts alternate: HOST flag, ready flag, local id?); (2) the `Retry ... enabled` line before the RETRY probes (enabled
and not waiting?); (3) does the refused host's row on the Games List read "Settings differ: ..." (the log line above)?


## 060s Advanced Options window ownership

With Menus enabled, the Advanced Options menu's deactivation slot `0x6a1f80`
wraps the retail `0x5d1e50`; its resource-close slot `0x6a1fac`
also performs the same idempotent cleanup before retail `0x5af5a0`. Retail restores input and sound but assumes the
normal terminal close already destroyed the independent BXIG windows. Live
060s no-change Esc departed without the Cancel callback, leaving the window
family at `0xa6ad34..0xa6ad44` drawing over subsequent screens.

If any owned window remains at deactivation, the wrapper uses native Cancel's
settings reload `0x619770` and the complete native five-window teardown
`0x6192e0`, applies the terminal-close flags from `0x5d1eb0`, then runs original
deactivation. A completed normal close leaves no pointers and takes the same
deactivation path as before. The hook and helper signatures join the existing
guarded Menus transaction; absent/disabled Menus installs no new hook.

`r18-options-orphan` and `r18-options-orphan-popups` drive native deactivation,
window teardown and subsequent native draw enumeration; all outstanding
windows must disappear exactly once. `r18-options-orphan-native` retains the
unpatched negative control, which still draws the abandoned panel.
`r18-options-orphan-close` covers resource unload without prior deactivation.
`r18-options-close` drives the ordinary native close and production Cancel hook.
`r18-close-cycles` and `r18-model-cycles` exercise the retail resource-close chain
and model adapter/assignment/destruction with balanced fixture ownership.

The owner cleanup is independently tested. The precise 060r-to-060s change
that first lets the owner depart before the normal close remains unproven;
this does not claim that a specific anchor operand or input adapter caused it.
Integration must repeat no-change Esc at both 4:3 and widescreen, verify the
panel is gone and the next main-menu input works, and also check changed
settings with Accept and Cancel.

Session lifecycle (060s follow-up): successful native guest admission arms host-loss tracking even when a late joiner
has only its own participant and no bound native host link. Closing that authenticated host tunnel ends the admitted
guest and shows THE HOST LEFT. Discovery-only tunnels, local departures and authenticated rule-64 removals keep their
existing meanings. The terminal removal handler returns before the lobby is routed to the Hub; that route runs before
the Join tick can create its separate session-unavailable popup. A host present only in the room roster keeps its HOST
badge and observed phase; no native participant or readiness is invented for that card.

### 060s lobby and Browse regressions (fix P)

The displayed prompt device is presentation state, so it cannot identify a raw keyboard Delete press. On a skinned lobby,
DirectInput consumes scan D3 and keeps a role-action edge until the native Host/Join tick has read it. This also preserves a
quick tap released before that tick. Pad X still performs Ready/Kick, pad Y/RT still copies, and Ctrl+C remains Copy.

Browse gives the selected enabled row focus when its first populated list completes. An empty replacement list moves a
row's obsolete focus back to Refresh. Page actions move selection and focus together. Every row command is activation:
it joins that row on the first command, including when a mouse release precedes the binding tick. The Browse mouse
adapter commits the drawn row target after the retail walk and posts its row command through the existing mailbox.
Refresh has device-specific text and, in the companion LC layout, a dedicated focus model. Fresh listed rows and Join
remain enabled during a refresh so real native Accept can reach the held-join path; stale rows still refuse a lookup.

The native lobby refresh can replace all five description slots without changing the focused pointer. The DLL restores
all owned footer slots after the native tick. A newly allowed START returns focus from Kick/Leave even if navigation
retired the earlier disabled-focus restoration pointer. Other ways uses Online game; a guest header reports the accepted
native mode and difficulty. It does not infer the host's new/save choice from this PC's selected-save pointer.

The companion LC change draws card words as native text in front of the card model. Overlapping coplanar raster badge
surfaces are no longer responsible for HOST/READY/build labels. Party's Copy code hint has a navy twin selected with the
Invite focus. These rendering changes require rebuilding LC assets together with this DLL; exact in-game appearance still
requires a rendered acceptance check.

Regression coverage: r4-pad-join (including stale pad indication and press/release before a tick), r4-focus-host (navigation,
readiness, overwritten footer), r4-join (native header settings), r5-browse-join, r5-browse-empty, r16-hits-browse (retail mouse
walk followed by actual focus/mailbox/lookup), and r8-party-host/guest. LC's test_online_contract covers the corresponding
native-text/depth, dedicated Refresh bar, navigation and navy hint contracts.

### 060s mouse and modal actions (Q)

The retail item parser adds `neverfocus` (+0x54 bit 0x20) when `enabled="false"`, even when the authored item says
`neverfocus="false"` (0x5bc94e..0x5bc9c8). Its later enabled setter (0x5ade70) changes only bit 3. The runtime action
bindings now clear the parser-added flag when enabling an authored action; native Start/readiness controls retain their
original setter. This restores Leave/Cancel, Retry, and Host Left actions that the mouse walk answered without moving
focus (0x5ae331), so the old control received Accept. The hit-test predictor also refuses to certify a neverfocus target.

`lc-online-hits/2` adds a seventh field, the horizontal shear from the bottom to the top of the drawn bar. At height `y`,
both x edges move by `shear * (y-bottom)/(top-bottom)`. This covers the right end and top-right corner while excluding
the undrawn top-left triangle. The reader still accepts rectangular `/1` tables with zero shear. The companion builder
requires `/2` support before emitting the new table, and validates all four corners against the actual built model vertices.
No geometry, hook site, release identity, or wire protocol changes are required. Rebuild the LC menu output to get `/2`.

Host Left's two authored actions form a closed directional ring. While the panel is active, a native refresh that restores
the Hub's startactive row moves focus back onto Host Again. Back to Online still dismisses the panel; Host Again opens
the existing Host screen.

Regression fixtures execute the retail flag parser block, enabled setter, stored-focus setter, item navigation, mouse
walk and native usecmd boundary. The old r16 fixtures started every action at flags=8 and replaced setFocus with a recorder,
so they never reproduced a disabled-at-load action retaining bit 0x20. The shape fixture also checks real slanted corners
and the live right-end pixel, rather than checking the rectangular table against itself.

### Advanced Options Back during opening

The manager processes generic Back before the widget input tick during the opening animation. Advanced Options' retail
permission slot (`0x6a1fe0`, vtable `+0x7c`, originally `0x47b580`) permits the manager to reverse transition state 1 to 2.
That skips the widget's Cancel path, leaving its independent window family and `0x8afde4` latch alive after deactivation.
The existing guarded menu transaction now replaces this slot with a permission check that returns false while any window
at `0xa6ad34..44` exists. Early Esc is ignored until opening completes; settled widget Accept/Cancel retains normal behavior.
An empty family retains retail permission. The owner-deactivation and resource-close cleanup remains as a fallback.

`r18-options-opening-back` executes retail `0x5abd80` and manager Back `0x5da030` with transition state 1 and a live clean
panel. It requires the transition to remain 1 and verifies each family pointer, empty-family permission, settled native
widget Cancel, and another menu's unchanged early Back. `r18-options-close` covers actual native Accept/Cancel teardown.
Startup `advanced-back-guard` corrupts the new slot and requires the whole menu transaction to refuse without installing its hooks. With InputPipe on,
an Advanced Options Esc keyboard edge logs the transition state before manager input for the live timing check.

Pending live acceptance: Options > Space > Esc during opening (within about 0.5 seconds), x3, and after settling, x3,
at both resolutions. Capture the transition-state diagnostic for each Esc; check for no surviving panel or later input
misrouting, and confirm settled Accept commits and settled Cancel discards changes.

### Host departure while playing or waiting to join

A broker `room_closed` notice is service loss, including `host_left` (the host's
broker TCP session can drop independently of healthy P2P) and room expiry.
It stops new admission but never establishes host departure from payload silence.
Healthy zone loading already produces four-second gaps, and a closure can remain
in effect for hours. Native/GNS connection-end handling therefore owns liveness
with its existing timeouts, including loading tolerance. There is no second,
shorter closure deadline. Unbound waiting-room tunnels retain the existing
100 ms keepalive so their GNS connections remain observable too.

An authenticated GNS host-end callback still classifies departure immediately,
including admitted late guests without a native host record. The game-thread
watch consumes that event on the next frame; native host Leave/Quit notices
keep their direct route. A process crash can take GNS's own detection interval;
there is no promise of detecting a crash from three seconds of silence.
A terminal GNS host callback drains queued receive batches before
`CloseConnection` discards unread data; callbacks run before the normal room tick.
The owned room identity remains available to rule-64 validation
until Leave even when new admission is closed. Accepted removal and deliberate local Leave/Quit suppress
host loss, so closure-then-removal and removal-then-closure both show the removal.

Pause or Party Quit can finish through asynchronous `mainmenuexit 1`. When the main menu becomes current,
the game-thread keyboard poll retries the Hub every 500 ms so its pending THE HOST LEFT panel appears without a
Play Online selection. The removal-to-Hub request uses the same rate limiter and
cannot suppress this host-left route. The existing panel, modal controls and missing-assets fallback are retained.

The native end-chain matrix checks main-menu routing for host notices/connection loss and its absence for
Kick and local departures. `xml2_online_broker_test --room-closed-offline` exercises the production inbox
and P2P receive/removal handler without initializing GNS or opening sockets. Optional subcases cover healthy
host-only broker loss, room expiry, both closure/removal orders, silent/recent host traffic, clock wraparound,
a removal behind a full receive batch, queued removal at terminal host close (including a second batch), local
Leave, pre-admission, host, service loss and stale/foreign events.


### 060t presentation and accepted resolution follow-up

Advanced Options snapshots the native resolution at panel construction. Retail Accept
copies its selected resolution into `0xa68d9c` at `0x61f843` before the save call at
`0x61f8a4`. The existing save hook still calls the native settings writer once, then
persists changed dimensions as Display Width/Height and synchronizes the display
query override for later opens. Cancel reloads native settings and leaves the INI
and override intact. A no-change Accept does not introduce dimension keys.

Code and Browse must fit retail's **75 onfocus records** (`0x5af147`; records at
`0x8a72b0`, 52 bytes each). The former 80/82 records silently dropped late actions,
including Retry. The companion author now uses one shared focus model and one gain
record per action on these two single-plane screens. The outgoing gain is stopped
by retail focus loss (`0x5bbd74` -> `0x5ae580`); the incoming gain replaces the shared
model. Other screens keep their paired gain/loss records. The authoring validator
rejects a menu with more than 75 records.

A known empty Browse cache remains visible after a failed refresh, with "No games
in the last list"; a fresh empty result keeps its public-games message. Singular
counts retain "1 public game" and "1 last known game". Browse paging occupies its
own footer cells, leaving a full diagnostic cell in the error row. All footer focus
bars use the same shear, including narrow prompts, so their upper edges cannot
cross a neighbouring flat bar. The host-left panel bakes neutral action backgrounds;
only the focused action supplies amber, sized to its authored hit rectangle.

Lobby tool prompts follow the physical controller captured on entry, before native
Join selects the guest's network slot. The Party confirmation queues the stock
already-confirmed command `mainmenuexit 1` once. Outside a native session, where
Party delegates without its own question, bare `mainmenuexit` preserves the stock
confirmation. The PDA mouse slot `0x69edec`
(retail `0x5cc1f0`) has an atomic, byte-guarded adapter: only a mode-0 PDA carrying
`lc_party_link` and `option07` uses that always-visible bar's bounds, remaps to the
link's native text hit box, and commits its stored focus for keyboard Accept.
Other pages and assets retain their native mouse route.

Regression coverage: `r19-options-resolution`, `r19-pause-party`,
`r4-guest-footer`, both Party role fixtures, `r5b-views`, and the LC 060t authoring
regressions. `r19-pause-party-native` is an intentional failing negative control
that restores the stock PDA mouse slot. Offline fixtures execute retail menu leaves
in mapped memory; they do not launch the game or establish live rendering/input
acceptance.
## Frontend strings and compact focus lifecycle (060u follow-up)

Code/Browse retain 40/41 gain records, below the retail 75-record focus pool.
The shared Code origin needs a distinct model resource name for each grid cell.
Those names share the frontend string table with menu names and native text:
`0x5ab780 -> 0x41a460 -> 0x419da0`, getter `0x602200`. This is separate from
W2's zone-string table. Retail has 1,024 slots and a 7 KiB arena at `+0x1008`;
it publishes a slot before checking arena capacity and its probe never stops
when every slot is occupied. An arena overflow therefore aliases unwritten
strings, and repeated lookups can fill the table and spin at `0x419db4..0x419de0`.

The Menus transaction now pairs a 64 KiB arena allocation with a bounded
frontend interner. Slot indices, generation bits, hash, arena offset and native
allocation/free ownership stay native-compatible. Both full-table and full-arena
refusals return the empty handle and publish nothing. Menus off installs neither
write. The native fixture compares handles and complete original backing with
retail, reproduces the original Code-name spin offline, and repeats Code editing
and open/close without starting the game.

Gain-only focus records also need an explicit final loss: when Code/Browse has
no shown, enabled, actually focused action, the UI tick replaces `lc_focus` with
the invisible model. It does not setHidden, which would retain an obsolete bar
in the model's saved slot. Repeated unfocused frames do not reload the resource.
If showing/enabling Retry retains focus without a new gain, the native
`0x5aaf80` loss/gain pair restores the authored model without running usecmd.
Browse's visible `list_count` owns the whole fresh/stale count (including the
singular forms); matching LC authoring reserves that header cell and clears the
old static header. Regenerate menus/models and the hit table with this DLL.

With InputPipe diagnostics enabled, the Advanced Options Esc edge is observed
before the first-online-tick guard. It reads only already-published manager and
window pointers; it neither creates a menu nor changes Esc handling.

### Y1 review follow-up: model identity and changing DATA

The model identity used by compact focus cleanup is `CMenuItemModel+0x58`.
Retail `0x5c4cc9` writes floating-point time to `+0x98`; it cannot identify a
model. The native regression executes `0x5bbdd0`, `0x5c4c50`, `0x5bc3f0` and
`0x5aaf80` at a fixed clock, including a focus gain and loss between ticks.

The frontend table still has 1024 slots. Changing DATA now uses a stable
1024-byte buffer per bound widget, read through the base-text and MENU_ITEM_TEXT
getter slots (`0x6a02c0`, `0x6a1148`). The native formatted setter reuses the
widget's existing name handle for nonempty DATA and retains zero for empty
DATA. No new handle is needed per value. Hit-test inspection uses the displayed
buffer; native `$MENU` prompts keep their original interned text because native
mouse handling reads those tokens directly. Ordinary native text and gamevar
widgets keep the stock `0x5c67f0` getter and `0x5c6850` setter behavior.

Buffers survive deactivation and cached menu visits, and are released after
resource close. The native Host and Join close adapters delegate to their
original `0x5ba270` and `0x5c8020` methods. A guarded entry detour at the common
resource closer `0x5af5a0` also covers Advanced Options, other derived vtables,
and direct calls. It replays the six-byte prologue and preserves the native
cleanup result before erasing that owner's buffers and binding cache. Recreated owner/widget/name
addresses therefore cannot retrieve an earlier resource's DATA. `authored_text`
compares the owned display value before using the native getter, including plain
localized labels without `$MENU`. The DATA regression fills all native
slots through the real setter, binds a fresh room successfully, then changes
2200 room names and 1200 codes without increasing slot usage. The general
interner still logs a bounded refusal for unrelated native strings at capacity.


## 060u follow-up: lobby activation and removal ownership

The skinned lobby's scoped mouse adapter commits a native action button before
the retail mouse pass. That pass only navigates among Start, Kick and Leave
when its previous focus is already a native button; Copy code is outside that
group. Host pad MENU_OK selects Start and offers one native Accept edge. The
retail host tick still owns authority, readiness, admission, transfer and its
effective minimum-player byte. `r4-start-mouse` and `r4-start-pad` cover those
denials, held Start, and a roster reduced to the ready host after guests leave.

Lobby removal suppresses the stock reason dialog and retires Join until the
asynchronous Hub request completes, with retries every 500 ms across current-menu
changes. A fallback already on Browse is retried too. After three seconds without
the Hub, the latch is released and the current screen resumes normal input;
the removal notice still waits for the next Hub visit. Repeated session clears
retain one deadline, one removal notice and one broker leave. In-zone removal retains its existing
native main-menu teardown. The kicked/host-left lobby fixtures deliberately
keep the old screen current for extra frames.

PDA Party hover is revalidated when Enter is sampled. A valid plain Enter queues
the authored constant Party command once and consumes that key until release,
so a later native focus refresh cannot redirect it to another row. Navigation,
mouse-out, page/menu departure and disabled or absent links cancel the hover.
Other native inputs and a refused queue keep their existing route.

Hub assets author hidden `lc_hub_select_normal` and `lc_hub_select_hover` text
sources. They carry localized token text with explicit palette-correct colors;
the Hub binder follows the native footer hover index after description refresh.
It changes no focus or hit geometry. Assets without the sources retain their
previous presentation.

The broker TLS client owns a valid SSPI context as soon as
`InitializeSecurityContextA` produces it, before any send, receive or error
return. Reset deletes that context before freeing credentials. The native TLS
fixture's `--lifetime-offline` mode exercises partial and successful handshakes,
reuse and destruction with synthetic socket/SSPI imports and real event handles.
`--pin-offline` checks real SPKI hashing/pin rejection and protocol selection
without entering the system certificate-chain path. Neither mode opens sockets.


### Independent-review follow-up limits

The socket-free broker regression now covers closure, five seconds of zone-load
silence, resumed traffic, a later sixty-second stall, and then the authenticated
transport-end callback. The native end-chain fixture loses the first Hub request,
changes Join to Browse, checks 500 ms retries, and verifies Back works after the
three-second release. The main-menu host-left fixture also loses a request and
requires a retry. The DATA fixture executes a retail derived close directly,
reuses the same addresses/value without deactivation, repeats Advanced Options
close/recreate 200 times, and checks a plain localized Hub label without
rebinding it each frame.

`removal_start` already calls `removal_send_tick` synchronously before the member
link unbinds, so Quit before the *next* transport tick does not omit its first
notice. A first notice can still wait for an authenticated member report. The
terminal drain also retains its existing 8192-message / 1 MiB gameplay-queue
bounds: an extreme backlog can still discard a later removal and show host-left.
Those forced orderings remain live re-checks; this follow-up does not claim to
eliminate every Kick/quit race or prove delivery of an unreliable notice.
# 060w follow-up: departure delivery and presentation

Pause Quit with a dead host hero still queued and sent native opcode `0x2b`.
The guest received it into the native reliable parser, but no native removal
callback followed. The room now classifies that existing explicit native host
departure before native queueing, only on its current authenticated host tunnel,
for a bound/admitted guest. Broker closure alone cannot classify host-left;
another member, stale connection, unreliable notice, Kick, and local departure
are rejected. The exact downstream native queue/record condition remains a live
investigation; classification no longer depends on that native callback.

The session watcher also left the room immediately, without reliable linger,
because the native transport was already destroyed. It now arms the existing
flush/linger path and completes the leave immediately. This preserves GNS's
already queued reliable notice without waiting for a native tick that cannot run.
No wire format or broker-only host-loss inference changes.

An admitted guest's authenticated host loss requests the existing Hub panel at
classification time, while `mainmenuexit 1` finishes asynchronously. The panel's
actions and engine rearm wait for the old native transport to end; the normal
main-menu return still routes back to the panel. A late terminal native clear
suppresses its stock reason dialog while the early panel owns that session end.
A room-closed broker notice
alone continues to leave established P2P play running.

The early Advanced Options Esc diagnostic also observes the destination staged
at manager `+0x866a8` while a transition is active. Retail `0x5dac00` consumes and
clears the pending request before `0x5da8a0` stages that destination, leaving the
old menu current during fade-out. A stale staged name with no transition does
not produce a diagnostic.

Footer recolouring accepts an already expanded Back token. Browse Back and Code
Delete use hidden localized normal/hover sources from the companion LC builder,
because native description refresh adds an explicit light palette prefix that
overrides the focused text style. Empty Browse paging controls are disabled when
no page exists and retained focus moves to a usable footer action. The native
neighbour search itself does not use label content to determine availability.

Native fixture modes: `r6-end-deadquit-host`, `r6-end-hostloss-zone-guest`,
`r23-options-staged`, `r23-footer-expanded`, `r23-compact-footer`, and
`r23-empty-paging`. The latter also preserves navigation when a nine-room list
has another page. Live two-PC timing/draw checks remain necessary.

### Two-PC follow-up: Party input and Host visibility

After an inline Party Kick closes, delayed Confirm/Dismiss commands from that
old panel are discarded before the next Party action enters the single mailbox.
A valid rail Quit hit posts the existing Leave intent in the same frame. Party
handles the DirectInput Esc edge and the pad Back mask through that mailbox;
the first Back dismisses an open confirmation, the next reaches native Back.
No additional removal or native quit is sent by these input repairs.

Host visibility mouse clicks select the authored Private/Public segment. A click
on the selected segment leaves the setting unchanged. Keyboard and pad activation
of the visibility row retain their existing toggle behavior. No new asset intent
or capability is required. Native fixture modes are `r24-party-native` and
`r24-visibility`; live keyboard/pad/mouse timing still needs a two-PC check.


Host setup DROP-IN is an editable admission choice (intent75, additive capability
lc-online-drop-in/1). Keyboard/pad focus links include it and a mouse release posts
that same bounded mailbox action. The row has its own focus marker. It changes
LateJoin only before a host session is posted and does not mutate the INI or
frozen gameplay record. The startup default remains0. The complete JIP module
must already be installed; JoinInProgress=0 remains disabled until relaunch.
Guests show campaign/difficulty without claiming this PC's future host policy
is the remote host's setting. The host's lobby summary reflects its own choice.


## Code refusal after the session started (fix AN)

A resolve refused with `room_not_joinable` triggers an existing protocol-2 `list_rooms` request on the same connection,
within the original resolve's 10-second deadline. No new metadata or broker operation is sent. Only a complete, valid,
fresh list with the exact code, `in_progress=true`, `joinable=false`, and known occupancy `1 <= players < max_players`
is a candidate. Because broker occupancy can update before the host's paced advert, the client waits 6 seconds (the 5-second presence interval plus observation margin), then confirms with one more `list_rooms` request. Only a second matching candidate
selects `lc_broker_err_session_started`: "This session has started. Ask the host to return to the lobby." Retry is hidden.
The current host advert uses this combination for started with drop-in disabled; full rooms also advertise false.
The second observation avoids mistaking a just-freed seat with a still-full advert for that policy. Native posting
advertises false before the started latch. Level loading alone does not change that advert.

Unknown/private/absent/truncated/malformed/full rows, a changed joinable state, or failure of the explanatory lookup keep
`lc_broker_err_room_closed`: "Not taking players right now. Try again soon." Retry retains its limiter/backoff. The lookup
never admits or connects to the host; duplicate refusals do not send another lookup. Cancel, timeout and generation checks
reject late results. Failure of the lookup preserves the original refusal instead of replacing it with a lookup error.

`session_started` is presentation context only. The diagnostic remains `join: room_not_joinable` and launcher status
remains `room_closed` (contract v1.5). No gameplay rule, framing, capability marker or release identity changes. Older
assets without the dedicated scene use the existing generic scene with Retry hidden. LC must be rebuilt with the companion
scene for the specific copy. Private rooms cannot be distinguished through the public list; fix AN2 therefore keeps
a started private room with DROP-IN off resolvable at the broker (while rule 26's started refusal is installed), so its
code join reaches the host and shows the native notice `lc_notice_started` ("This session has started; ask the host to
return to the lobby") through the tagged 0x1c refusal instead
([online-rooms.md](online-rooms.md#started-private-rooms-with-drop-in-off-fix-an2)). A private room still refused by the
broker (native posting, full, no installed refusal) keeps the retry copy.
This remains an inference from this client's advertisement, not a broker-supplied refusal reason. Protocol 2 has no
policy bit or host-advert timestamp: a host stalled beyond its normal presence interval can still leave stale metadata.
The confirmation stays within the original deadline; if there is insufficient time, the retry copy wins.

## LC Game Options and ready-roster presentation

The companion builder can author `lc_game_options` with the native
`GAME_OPTIONS_MENU` class and unchanged option/data identifiers, native commands,
platform/mode gates and focus notifications. The original definitions are read-only authoring inputs. With Menus installed, requests select the LC variant only if
`Data/lc_online_options.cap` exactly contains `lc-online-options/1` plus LF and
both menu halves, scene, package, two backdrop models and the shared row-focus
model exist. Unsupported or incomplete authoring selects the plain LC failure page.
The native tick, session setters, save selection, Post and Back remain the
existing setup flow; there is no second Game Options state machine.

The separate ready roster at `0x5d93f0` uses roster-local LC body-font measurement
and navy panel adapters. Its localized names, statuses, hero/slow suffixes and
readiness decisions remain native. Missing private body metrics/atlas retain
native arguments. This uses the existing Menus switch and font family.

Native text commands defer drawing and do not store their font index. Roster
commands are therefore marked by stream identity and full generation-bearing
token, then grouped by font at the real `0x6a362c` render callback. Every group
uses native `0x5ef2e0` with the actual traversal arguments and native cleanup,
consuming each token once. The `0x5ef1a0` reset adapter carries color scopes
between groups before cleanup resets their depth, and retires bookkeeping on
normal reset/destruction. Unmarked streams retain one native render call. The
large stream snapshot is allocated only in the marked slow path. Each group also
binds its matching font atlas to the viewport material, using the borrowed
texture getter and material setter used by `0x5f1049..0x5f105c`. The viewport
initializer only binds its original atlas once: changing deferred font metrics
alone samples the wrong texture and produces sliced glyphs. The original atlas
is restored after the split. Host fixtures
cover the installed adapters and deferred batches; actual graphics traversal,
clipping and final appearance still need live acceptance on host and guest.

### Optional native online scenes and text entry

With `Online/Menus=1`, the companion can select `lc_text_entry`,
`lc_other_online`, `lc_campaign_lobby`, `lc_games_list` and
`lc_player_game_options`. Each keeps its original native menu class, mode
predicates, values, policy/information content, list services and return chain.
The builder reads the original resources and publishes a separate exact
`Data/lc_online_skin_<native>.cap` only for a supported template and companion
DLL. Selection checks the complete scene/menu/package/backdrop/focus closure;
missing assets or an invalid marker select the plain LC failure message.

The PC editor implements keyboard characters but leaves the retained 40-key
pad grid unpopulated. The scoped LC adapter supplies lower/upper/symbol pages,
Backspace, Space and Done, plus a clickable Escape/Cancel button. Constant
intents 100..140 are consumed only by the
live LC editor and enter the original character lane (`0xa0a061`). Native
`0x5e72f0` still validates the request tag's charset and byte limit and handles
Enter/Back/Escape; `0x5e6f30` owns acceptance. The native input-context name
comparisons stay unchanged: the stock editor uses text-only context 2, while the
LC editor uses normal menu context 1 so pad bindings are available. Closing/deactivating retires pending
input. Printable keyboard text comes from Windows' translated `WM_CHAR` stream,
so the active layout owns letters, Shift/Caps Lock, AltGr and dead-key
composition. Committed `WM_IME_CHAR` text uses the same queue. The display
wrapper covers managed windows and native/fullscreen modes. When online
menus are installed it uses a Unicode subclass, forwarding
to the original ANSI procedure through `CallWindowProcW`. A thread-local
Unicode `WH_GETMESSAGE` hook captures LC characters before an ANSI
`PeekMessageA`/`GetMessageA` pump can narrow them. Only removed messages for
the editor's window are queued; repeated `PM_NOREMOVE` peeks do not type.
Consumed messages become `WM_NULL`, so later dispatch cannot type them twice.
Sent character messages use the subclass directly. Window destruction retires
the hook; failure to install it logs the failure and visibly refuses physical
text (the on-screen grid remains usable). With Menus disabled the original
ANSI subclass and forwarding stay in place.
DirectInput supplies only arrows, Enter, Escape and Backspace. Their `WM_CHAR`
copies are ignored, and printable/numpad DI state is consumed without producing
text or a second menu action. The explicit `chars` test lane shares this queue
while LC owns the editor; synthetic printable typing uses `chars`, navigation
uses `keys`.

Only printable ASCII can enter the native character byte. Unsupported Unicode
(including composed accents, IME text and surrogate pairs) displays
"Unsupported" instead of truncating to a low byte. Native
`0x5e72f0` remains the authority for the narrower per-request charset and byte
limit: a refused printable byte displays "Unsupported" or
"Text limit" in the spare fourth footer label for three seconds.
The queue is bounded at 64 entries and reports overflow visibly.
For tag 3 IP entry, a comma translated from the numpad decimal scan (`0x53`)
is a period; a main-keyboard comma is still refused. Name punctuation is
unchanged. ASCII digits and periods from either keyboard section enter the
same native IP filter. Keyboard arrows
and menu D-pad bits move focus across enabled keys in the generated 9-column
layout; Enter/A presses that key, including Done. Escape/B cancel. The native
menu mask is suppressed only during the wrapped editor tick, so its old
Enter/Start terminal shortcut cannot also close the grid. Mouse commands retain
the same 100..140 lane. Owner activation/deactivation and input loss retire queued
input; page changes repair focus if a cell becomes unavailable.
The native tag 3 IP editor exposes digits and period only. Hub and Game Options
names retain their 31-byte limit; the Other ways name and IP requests retain
their native 15-byte limit. Unscoped requests for this legacy editor also select the plain failure page.

The native campaign chooser's Join action requests `player_game_options` on
PC, whose handler owns Connect by IP and queues `openmenu games_list`. Its
optional scene is therefore part of the connected family. The browser retains
the actual LISTBOX, columns, selection, search, filtering and admission;
TEXTBOX/LISTBOX font metrics remain native because those renderers bypass the
private CText scale hook. Original policy content is retained in all four
informational modes; no replacement policy is authored.

### Network notice lane

`0x60a600` queues native network notices; `0x60aa40`/`0x60a690` own their
expiry and retirement. Audit address `0x4029f0` polls the active message through
`0x60a5d0` to keep GUI/HUD rendering during a stall; it is not a drawing entry.
The existing hold-overlay evidence documents timed native notices failing to
appear during holds. Menus now registers a separate pre-Present callback that
draws the active native message with the fix's own geometric panel and GDI text.
It uses the bounded native formatter, strips native inline colour scopes,
requires a running campaign, and yields to admission holds and the online
pages' existing inline/footer notices. It never changes queue priority, expiry,
readiness, the simulation clock or the stall decision. Failed reads/draws leave
the native queue intact. This works independently of the JoinInProgress switch.

With the experimental JoinStallWrapper enabled, a classified protected restart
wait skips the render tail and Present, so this notice lane is also suppressed.
An active notice does not authorize that tail: it contains the local render work
the wrapper is testing for lockstep mutations. Native message polling and prefix
services continue; ordinary stalls and modal/loading/video exclusions retain
presentation. Notices remain owned by the native queue and can expire while
presentation is suppressed. Live A42 acceptance must distinguish these cases.

The host matrix modes `r27-text`, `r27-other` and `r27-network` cover editor
effects, both keyboard context selectors, native Host/Join and browser services,
resource fallbacks and network queue observation. Actual keyboard/pad/mouse
rendering, policy scrolling, aspect ratios and overlay placement still require
live acceptance; no synthetic fixture establishes those visual results.


### No GameSpy screens (beta.2)

Rebuilt LC main menus request `lc_online_entry`. That resource is a plain LC
failure page by default; a working online adapter intercepts it for the Hub.
Disabled/refused hooks, fonts or registration therefore cannot expose native
ONLINE_MENU. Runtime resource/marker checks, entry failure, request exceptions
and unusable lobby controls select `lc_online_unavailable`, never a native
GameSpy-era screen. This supersedes historical native-fallback instructions
above. Native services behind verified LC variants and skinned save lists stay.

The builder always authors the failure resource for the XML1 front end, even
with online menus off: eight anchors within the original 175 limit, shared
native-font styles, original navy geometry and no script-intent dependency.
The visible Back item has `menu_cancel=true` and `usecmd="openmenu main"`;
retail Escape/pad Back dispatches that item without our hooks. `menu_cancel`
is an item boolean, not a root command. Rebuild/update guidance is plain text.
Original online resource names receive this same message unless an existing
LC-skinned host/join resource is retained. The builder removes branding model
records, their decorative references and logo precaches from retained skins.

C02-C07 legacy/friends/hero/region/Live requests are refused to this page;
C08 Players/controller resource requests are refused when issued from the
online flow. Ordinary PC options and save dialogs retain their own routes.
The companion capability is `lc-online-no-gamespy/1`; regenerate matching
content before accepting the candidate. Live visual/input acceptance remains
required, including guard/font/marker/resource failures and Back at both aspects.

## Native item-map budget and LAN list fixes

The scene-anchor expansion does not expand CMenu's separate item map.
Retail item insertion at 0x5af490 refuses another item when the count at
menu +0x1608 equals 0xaf (175); the check is at 0x5af4af. This was rechecked
against the owned retail image during the beta.2 blocker fix. Lobby markers
placed after that limit were absent, so `lobby_live()` selected native refresh
and input. The companion builder now validates the independent 175-item limit,
puts the marker before lobby content, and reuses `lc_pN_hero_fallback` as one
DATA field for the local/session rate pair, or the slow warning naming the session
target (unknown rate: the normal hero fallback). Native text stays on one line.
Its owned host/join resources use 173/169 items including backdrops. Browse
retains seven rows at 172 items: numeric occupancy replaces redundant pip
variants and one DATA label per row carries SAME/CHECK; another build still
shows its token and the existing dim state.

The native discovery receiver 0x6090d0 allocated at 0x609122 and appended at
0x6091d3 on every compatible answer. Repeated broadcast/direct replies could
therefore create duplicate rows. After gameplay/settings validation, the browser
adapter walks the current bounded 100-row native table and refreshes an existing
source-IPv4/game-port row in place, issuing the same status-6 update. It keeps
the native endpoint and version flag semantics, checks the complete chain before
writing, and refuses malformed/cyclic/out-of-pool tables. Different source
addresses or ports remain distinct; names alone never identify a host. There is
no cache across searches or changes to gameplay/admission settings.

`lc-fonts-list/1` adds the private style scale at LISTBOX clipping/drawing
(0x5c2136/0x5c21f2) and row-height initialization (0x5c0b31). The stored row
height covers rendering, scrolling and native mouse hit division (0x5c0e79).
Private height derives from atlas metrics and viewport height with four view
units of spacing; classic styles return their original height and scale.
These writes join the existing guarded font transaction. The builder uses the
private row style for the LAN list and centered `lc_o_lan_focus` art, because
native positioning places focus at the row center (0x5c14b4..0x5c14c8).
The second header describes the native composite column as CAMPAIGN / PLAYERS.
The details textbox retains its native metrics. Both repositories must be
integrated and content regenerated together; older companions lacking the
list-font capability are refused by builder preflight.


### LAN lobby facts

A native LAN/direct-IP lobby without a room hides ROOM CODE and Copy (including
the footer), labels its visibility LAN, and hides the empty-card room-sharing
hint. The existing empty-card title remains Waiting for a player. The native
accepted game-info name takes precedence over the host participant's name when
no broker title is known. The local card can show its local/session rate pair;
remote cards show the known session rate alone when no party report supplies
their local setting. Such cards do not claim a ping measurement is in progress.
Code-room cards keep the existing broker facts and controls.

The native LAN row formatter separates game name and difficulty with a space.
The companion LC builder uses amber focus art for this list, matching its navy
selected-text style.


### Forwarded beta.2 Host controls and service state

Host setup uses an explicit row order: Visibility, Room name, Campaign, Drop-in,
Open. Up/Down move between enabled rows; Left/Right change a value and never
move focus. Binary values toggle; Room name and Campaign open the existing
native Game Options editor. Enter/A activates, Del/Y changes the selected value,
and Escape/B goes Back. The Host tick owns these input edges, preventing the
base menu from dispatching a second navigation/Accept. A held Host Enter and
the same pad Accept edge are fenced across the native-chooser transition.
Mouse/usecmd retains first action priority. The footer hides Change on Open or
a locked value. Host's Select/Change footer uses the existing Host-scoped intent
9; no new public number, wire field, or release identity is added.

Service status remembers the last observed Connected/Unavailable state across
operation cancellation; cancellation still invalidates its tickets and clears
request-specific errors. A new pending request displays Connecting until it has
an outcome. The UI shows localized failure scenes and retry countdowns, without
raw operation/error IDs. Hub fallback prompts are readable on the first authored
frame; runtime prompts follow keyboard/pad use. F4 is the keyboard Rejoin alias
(the legacy native keypad alias remains). A closed started session asks the
host to start a new session, rather than promising a return to its old lobby.


Native footer input still reads the interned $MENU token. Stable key labels are
provided through widget-owned text at the first getter call; serialized Hub/Host
prompts retain native tokens. Binding updates preserve the original token and
localized suffix, and cached tokens remain tied to the widget owner/name and
resource lifetime. This keeps native mouse hover/activation while preventing
startup binding-row names from flashing on screen. Native first-getter and raw
getter assertions cover both sides of that contract.
