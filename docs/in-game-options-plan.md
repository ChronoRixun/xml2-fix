# In-game display options for X-Men Legends II - research findings and implementation plan

Status: research complete (read-only spike, 2026-09-27/28); **menus at 60 fps implemented** (2026-09-28, see
"Menus at 60 fps"); **phase 1 implemented** (frame cap + VSync from
the ini, `src/frame_rate.cpp` / `src/frame_rate_rules.hpp`, VSync in `rewrite_present`; see "Decisions" and
1.6 for the windowed-vsync measurement); **phase 2 implemented** (the rows, `src/options_menu.cpp` /
`src/options_menu_rules.hpp`) - see "Phase 2 as implemented"; **phase 3 implemented** (the 64-slot resolution table, `src/resolution_list.cpp` /
`src/resolution_rules.hpp` - see "Phase 3 as implemented"); all three await the in-game test. Phase 4 not
started (out of scope by decision 2). Target: xml2-fix branch `display`, on top of `src/display.cpp` /
`src/display_rules.hpp`.

## Decisions (Owen, 2026-09-27 22:10) - binding

1. **Default `FrameRate`** when the user never set it = stock behaviour: the game's own 60 fps cap, untouched.
   The user picks e.g. 180 in the menu or the launcher.
2. **Display mode and resolution changes apply on restart** (no live mode switch; phase 4 is out of scope).
   The panel shows a clear "applies after restart" status line.
3. The menu **includes a "Run in background" row**.
4. **Windowed VSync: measure first** (done, 1.6). Only offer a real toggle where it takes effect; if windowed
   presents always waited for the vertical blank, show a read-only "On (desktop)" row instead.
5. The **HUD aspect fix at 21:9 is a separate follow-up**, not part of this work.
6. **English labels first**; translations later.

Default-off (the brief's rule, applied in the review fix round): with no new ini key set and the rows never
used, the game behaves as before this work; the rows themselves may appear (the four call-site patches A-D are
what puts them there). Everything else waits for a value: no Present hook, no frame-limiter patch, no mode-list
hooks and no resolution-table relocation in the stock mode without `ResolutionList` (the Direct3D create/reset
hooks the rows need to start the limiter live pass every call through unchanged). Reverting a row to its stock
value removes its key again (see "Review fix round" below).

Single source of truth: `xml2-fix.ini` next to `dinput.dll`, `[Display]` section (the launcher writes the same
keys): `Mode`, `Width`, `Height`, `Topmost`, `RunInBackground`, `FrameRate` (`<n>` | `refresh` | `0`; absent =
stock 60 cap untouched), `VSync` (`0` | `1`; absent = the game's own presentation interval). The menu writes a
key only when the user changed that row (Accept), with `WritePrivateProfileStringW`, creating the file if needed.

### Phase 1 as implemented (2026-09-28)

- `FrameRate` set -> the imm32 at 0x401db7 is patched to 0.0 after the 16 bytes 0x401dab-0x401dba are checked
  (`frame_rate_rules::stock_cap_patch`; `xml2_test` checks them against `docs/research/XMen2.exe`); frames are
  paced in the `Present` hook by `frame_rate::on_present` (high-resolution waitable timer for all but the last
  0.6 ms, then a spin; `timeBeginPeriod(1)` + a 2 ms margin where the flag is unavailable). `refresh` = the
  desktop rate from `EnumDisplaySettings` (60 if unknown). Late frames re-anchor the cadence (no catch-up burst).
  The 1/30 constant at 0x401dae is untouched. Patch mismatch -> logged, the game's spin stays (the pacer
  still runs, so a cap below 60 works; one above can't).
- `VSync` on a fullscreen device (stock mode, `Mode=fullscreen`) -> `FullScreen_PresentationInterval` ONE / IMMEDIATE
  in `rewrite_present`, only if `GetDeviceCaps().PresentationIntervals` offers it (else logged, engine's kept).
- `VSync=1` in `borderless`/`windowed` -> **not** `COPY_VSYNC` (see 1.6: ~32 fps) but frames paced at the desktop
  refresh rate by the limiter (`frame_rate_rules::effective_target`: never above `FrameRate`; with `FrameRate`
  absent the stock 60 spin stays in charge, so VSync alone never raises the rate). `VSync=0` in a window only
  undoes an engine-side alchemy `windowedVSync` swap effect. Consequence for phase 2: the VSync row is a real
  On/Off toggle in every mode; in the windowed modes its help text should say "On = paced at your desktop's
  refresh rate" rather than promise a vertical-blank wait.
- The test pipe's `status` reply ends with `; fps <n.n>; frame rate <setting>` (Presents counted over the last
  full second), so the cap is verifiable in game without screenshots.
- Default-off verified by `xml2_test`: with none of the keys set the display fix returns before hooking anything
  ("display: as the game has it (no [Display] Mode, FrameRate or VSync in xml2-fix.ini)").

### Menus at 60 fps (2026-09-28, after the first in-game run at 180)

Reported in game at `FrameRate=180` (harness `_nb2` and Owen, borderless): holding Down 300 ms in the pause menu
moved the highlight 1 item at 60 fps and 2 at 180; the XML1 main menu's highlight disk "spun faster", the
highlight skipped, *Begin Story* was hard to hit. Gameplay was fine (same walk distance per second at 60 and 180).

**Read, not guessed (Ghidra: `docs/research/decomp_fps1.c` .. `decomp_fps8.c`).** Nothing in the menu layer counts
frames; it all runs on the menu manager's time:

| What | Where | Driven by |
| --- | --- | --- |
| menu time and dt | `CClient::frame` 0x4021c5 -> `CMenuMgr` vt+0x2c 0x5d4570: `+0x86048 = dt`, `+0x86044 += dt` (skipped when dt <= 0) | the frame's real dt (game time, `getTime(1)`) |
| navigation auto-repeat (every menu, popup and conversation choice) | mgr vt+0x1f8 0x5da1a0: fires on the press, again after 0.3 s held (0x3e99999a, 0x5da1f2), then every 0.1 s (0x6e70a4); x2 on a list after 1.4 s (0x68602c) | sums of dt (vt+0x28 0x5d81e0) |
| menu model items (loops, one-shots) | item vt+0x20 0x5c5060 -> 0x5c4cf0: position += dt / length | dt |
| rigid (igTransformSequence) model animations, e.g. XML1's `model_button_highlight` disk | `CRigidAnimCtrl` 0x5721f0: bias + scale * menu time, or Alchemy's global timer (QPC) | time |
| text glow / highlight fade | 0x5d8ae0 (sin of the menu time), 0x5c6030 (dt) | time |
| main menu idle timer | `CMenuMain` vt+0x38 0x5c9640: `+0x192c += dt` | dt |
| menu effects | mgr vt+0x3c 0x5d6d60 -> effect manager vt+0x10(time, max(dt, 1 ms)) | dt |
| Cerebro backdrop camera (`cameraFollowMotionPath`) | `CCamera` vt+0xec 0x448390, update 0x447730: game time minus the path's start | time |
| mouse button held | 0x61a600: 0.2 s of game time | time |
| conversation choices | 0x45d1a0: the same navigation (0x45d405) | dt |
| HUD | 0x59f1a0 hands its parts a time (vt+0x8c); messages fade by time (0x5a7a30) | time |

The 300 ms measurement sits exactly on the 0.3 s repeat delay: 300 ms of hold is 17-19 frames at 60 and 53-57 at
180 depending on where the pipe's tick-count hold lands, so one or two moves at either rate. What does depend on
the frame rate is the *sampling*: a direction fires on a frame where it is down and wasn't the frame before (the
controller's edge, `CController` vt+0x18 0x550b70), with no debounce, and the sticks fire on crossing +-0.5
(0x682ff4 / 0x680488) with no hysteresis (edge bytes 0x8b0ce0 / 0x8b0cd6). At 180 fps the game looks three
times as often, so a d-pad contact bounce or a stick resting near 0.5 becomes a second press - the skipping
highlight. Frame-counted timers to scale by dt (fix (a)): none found, so none patched.

**Fix (b), implemented.** While a menu, popup or conversation is on screen the fix paces at 60 (at `FrameRate` when
that is lower) and returns to `FrameRate` in play; `frame_rate::on_present` decides it every frame.
"On screen" is the game's own test - `CClient::frame` skips its pause-button handling on exactly these three
(0x401ef8): popup manager `[0x8b13ec]` vt+0x78 0x5e9e30 (popup `+0x403c`'s byte `+0x18 + i*0x1560 + 0x155d`
bit 0, popup 0's when the index is out of range), `CMenuMgr` `[0x8aff18]` vt+0x204 0x5d8870 (stack count
`+0x86068` > 0, first entry `+0x8605c`, and a current menu `+0x86090` or a pending name `+0x85db0`),
`CConversationSystem` `[0x717aac]` vt+0x20 0x458010 (`+0x21b24` bit 1). The fix reads those fields directly (no
calls, no getter that would construct the singletons), under SEH. Kept at `FrameRate`: movies (mgr vt+0x1b4
0x5d8420, `+0x85cec` bit 5 - the stock loop skips its spin for them too, 0x401fb1) and the loading screen (the
current menu's vtable is `CMenuLoading` 0x69fa1c): no input there, and pacing a load lower only slows it.
All-or-nothing: 23 retail-byte guards (`frame_rate_rules::screen_guards`: the frame's test, the three getters,
the constructors that store the objects and set their vtables, the four vtable slots, the four functions,
CMenuLoading's RTTI); a mismatch logs `menus at <FrameRate> too - XMen2.exe doesn't have the expected code at ...`
and every frame is paced at `FrameRate` as before. `xml2_test` runs the game's four functions from a mapped
copy of the exe on blocks of its own (every menu case, 56 popup states, all 256 conversation flag bytes) and
compares each answer with the fix's reading. Only when it matters: the spin is off and `FrameRate` is above 60
or unlimited (`FrameRate=0` now hooks `Present` for it). Log: `frame rate: 180 fps, paced by the fix (...);
the game's 60 fps spin is off; menus, popups and conversations at 60 fps`, then the first 12 switches
(`frame rate: 60 fps while a menu is on screen`, `frame rate: 180 fps again (play)`). The pipe's `status`:
`frame rate 180 fps (menus, popups and conversations at 60)`, or `(60 now: a menu, popup or conversation is up)`.

**The borderless live-switch "lock-up".** Owen switched 60 -> 180 from the Advanced Options panel in borderless
and the game locked up; the log ended at `frame rate: now 180 fps, paced by the fix from the next frame` - which
is also the last line of a good run (nothing more was logged after a live change). Read again: `retarget`
patches the spin's imm32 on the game thread (the instruction isn't executing), creates the timer, restarts the
cadence; `on_present` waits at most one interval per frame (timer waits capped at 100 ms, re-planned every
wake); the display mutex is recursive and not held by `Present`; the stock Accept applies no display change
(1.3), so no `Reset` follows. No hang path found in the fix, and nothing in it differs between borderless and
windowed. Two changes make a repeat both less likely and diagnosable: the switch happens in a menu, which now
stays at 60, so the first 180 fps frame comes only in play; and the fps is logged 2 and 30 s after every live
change (not only after the start), with each menu/play switch - a repeat's log shows whether frames were
still being presented and where.

### Phase 2 as implemented (2026-09-28; hooks A-D)

- `options_menu::install` (from `display::install`, `[Display] InGameOptions` default 1) compares the 16 bytes at
  each of the four call sites (A 0x61f356, B 0x61f8a4, C 0x61f8be, D 0x61f667) and the first 16 bytes of the nine
  game functions the rows call (operator new 0x671fc2, BXIGCycle ctor 0x623e10, BXIGLabel ctor 0x6229a0,
  setRect 0x61fe20, addOption 0x6216a0, setOptionRect 0x621750, setSelection 0x621730, registerNav 0x621e00,
  findItem 0x621d60) before writing a single byte; any difference -> logged ("options: XMen2.exe doesn't have
  the expected code for ...") and the panel stays stock. `xml2_test` checks the tables against
  `docs/research/XMen2.exe`. Then the four rel32s are rewritten (all unprotected first, so a failure leaves nothing
  half-patched).
- Hook A (`__fastcall finish_panel(panel, edx, slot)` in place of the `__thiscall FUN_006222b0(panel, 0)` call):
  reads the ini afresh (`display::read_ini_options`, so a launcher edit shows), builds four `BXIGCycle`s exactly
  as the FSAA row is built (new 0x208 -> ctor with `texs\toggle.png` -> setRect (33, y, 196, 12) -> setText ->
  style 3 -> addOption x n -> setOptionRect (120, y, 97, 18, style 5: right-aligned, ending at 217 like FSAA's) ->
  callback + userdata = the toggle bar (read from FSAA's +0x6c) -> setSelection -> the FSAA item's slot 0/1
  enter/exit descriptors copied), at y = 170, 194, 218, 242, plus a `BXIGLabel` status line (id 0x4f, (33, 272,
  196, 24), style 0 = small font) and four nav records `{row, row, row, up, down, 1, 0}` through registerNav;
  FSAA's record gets `down` = row 0 and Accept's `up` = row 3 (`options_menu_rules::relink`). Then the original
  `FUN_006222b0(panel, 0)` runs. Item ids 0x40-0x43. `BXIGCycle::handleInput` (0x623bf0, read from the asm): WM_KEYUP
  RETURN/RIGHT = next option, LEFT = previous (both fire events 1 then 2), UP/DOWN = focus in/out by the +0x1ac
  flag (events 3/4); WM_MOUSEMOVE hit-tests the row rect (events 3/4), WM_LBUTTONUP = next. `setSelection`
  (0x621730) fires event 1 only (it rewrites its argument to 1 and tail-jumps to the callback), which is why the
  row callback acts on event 2, like the game's FSAA callback.
- Highlight: `highlight_animation` in the bar's slot 5 (an `anim_slot` of 11 dwords: fn, start x/y, target x/y,
  delay, duration, elapsed, two spare - the target row goes in one -, slot index); on its first frame it hides
  item 0x12, greys 0x15, 6 and our rows and whitens the target, then moves the bar to (30, row y - 21) in 0.05 s
  (at once when the bar is still off-screen, y >= 481, as `FUN_00617f10` does). Slots 0/1 (the panel's slides)
  are never interrupted; the bar's +0x74/+0x70 are set directly because `FUN_006200c0` refuses a move within the
  same slot.
- Hook B (`on_save`, after the game's `FUN_00619440`): reads each row's +0x1e0, `ini_changes` (only rows whose
  index differs from the one shown at open) -> `WritePrivateProfileStringW(L"Display", key, value, xml2-fix.ini)`
  -> live apply: `display::set_frame_rate` (-> `frame_rate::retarget`: patches the 60 fps spin now if it never
  was, re-aims the pacer; Present is hooked then, if nothing hooked it at device creation - review fix round:
  the rows no longer force a Present hook from the start; the device is known from the pass-through
  `CreateDevice` hook, installed in the stock mode when the rows are in place),
  `display::set_run_in_background` (the Present/TestCooperativeLevel hooks read the flag every frame now, and a
  window of ours gets both hooks whatever RunInBackground says at start), `display::set_vsync` (a window: pacer
  retarget, live; fullscreen: the next device creation or reset - "applies after restart"). The display mode is
  a restart. Hook C (`on_cancel`) and D (`on_revert`: setSelection to Fullscreen / 60 / Off / On, status refreshed)
  wrap the originals. Values: Mode fullscreen|borderless|windowed; FrameRate `<n>`|refresh|0 (Unlimited); VSync
  0|1; RunInBackground 0|1. Absent keys show Fullscreen / 60 / Off / On. Review fix round: a row put on that
  stock value (or left on it by Revert) **removes** its key on Accept when the ini has one - before, Revert +
  Accept wrote Mode=fullscreen / FrameRate=60 / VSync=0 (the fix's variants), and nothing could return a key to
  absent. FrameRate back to absent puts the game's 60 fps spin back live (`frame_rate::switch_spin_on`, byte
  checked; if it can't, the fix paces at 60 until the next start).
- Frame-rate options: presets 30 60 120 144 165 180 240, plus the desktop's refresh rate and the ini's own
  value when not among them, sorted, then Refresh and Unlimited; ten at most (a BXIGCycle holds ten), dropping
  165, 144, 240, 120, 30 in that order when room is needed (never the desktop's rate or the ini's value).
- Status line: "Restart for the new mode" / "Restart to apply VSync" / "Restart to apply both" (fullscreen only for
  VSync), refreshed on every change and on revert; at most 25 characters (the panel doesn't clip - the first
  wording, "Display mode applies after restart", ran into the key-binding pane in game).
- Test pipe: a `wm KEYS [ms]` command (posted WM_KEYDOWN/WM_KEYUP) was added here and **removed in the review
  fix round**: the panel reads the DirectInput keyboard (1.1, "Input feed", corrected), so posted key messages
  did nothing and the pipe's existing `tap` drives it. `wm` now answers "error wm is gone ... use tap".
  (`tools/fixinput.py` in xml1-port had gained an uncommitted `wm`; it should go too.)
- `xml2_test`: row tables, frame-rate list rules, ini_changes, status text, an ini round trip through a temp file
  (only the changed keys written, comments and other keys kept), relink on fake records, anim/nav struct sizes,
  call-site tables (decoded targets, rel32) and the game-function fingerprints against XMen2.exe, the bytes of
  the panel's input path (why `tap` drives it); the pipe child confirms `wm` is refused with a pointer to `tap`
  and that a non-game process leaves the panel alone.
- **Not yet verified in game** (the main session's job, see section 6): the rows' look (label/value widths in
  the large font: "Run in background" vs a right-aligned "Fullscreen"; the small-font status line within 196 px),
  the enter slide of the new items, highlight travel and colours, keyboard order, sounds, Back/Esc asking on a
  dirty panel, and a live frame-rate change showing in the fps display / pipe `status`.

### Phase 3 as implemented (2026-09-28; patches F-L, the 64-slot table)

- **Re-verified with capstone over the whole image** (`docs/research/petools.py xref` plus a dword scan of every
  section): exactly seven dwords equal 0x6e9800 (the sites F-L below) and none falls anywhere else in
  0x6e9700..0x6e9a00 - no reference to the table's interior or its end, and nothing in .data/.rdata points at
  it. Ten dwords equal the count 0xa68da8, every one used as `count` (the two index-search loops at 0x61e626 and
  0x61f68d compare against it) or `count - 1` (the slider maths at 0x618179/0x618199, 0x61d5d7, 0x61e599/0x61e5b9,
  0x61f533, 0x61f80e); the only `cmp ..., 0x14` in the panel code (0x61968f, 0x619a03) are the 42-action binding
  loops, unrelated. The engine DLLs never call `GetAdapterModeCount`/`EnumAdapterModes` (checked over libIGGfx and
  libIGDisplay: no vtable call at +0x18/+0x1c on the IDirect3D8 at vc+0x140); the game's only caller is the list
  builder `FUN_00619ac0`, reached from the panel builder alone (0x61dc51). So the count needs no change and the
  hooks that feed the list are safe in the stock mode too.
- `resolution_list::install` (from `display::install`, `[Display] ResolutionList` = `all` default | `game`)
  compares the 16 bytes at each of the seven instructions with the retail build's (`resolution_rules::table_sites`:
  F 0x6181bf `lea eax,[edx*4+imm]`, G 0x619b95 `mov ebx,imm`, H 0x61d61f `lea`, I 0x61e636 `mov eax,imm`,
  J 0x61f57b `lea`, K 0x61f6a1 `mov ebp,imm`, L 0x61f843 `lea`; imm at +3 / +1), copies the game's 20 slots (the
  shipped "640x480".."1600x1200" defaults) into a 64 x 12-byte table in the DLL, unprotects all seven sites, then
  writes the table's address into each imm32. Any mismatch -> "resolution list: XMen2.exe doesn't have the expected
  code for ... - the game's own 20-slot table stays and the Video options list is trimmed to it" and the capacity
  stays 20. `xml2_test` checks the seven sites (consistency: decoded imm == 0x6e9800, opcode vs imm offset,
  address order) and their bytes against `docs/research/XMen2.exe`, read-only.
- The list (`resolution_rules::build_list`, fed through the display fix's `GetAdapterModeCount`/`EnumAdapterModes`
  hooks - installed in every mode with `ResolutionList` set, see the default-off note below): the
  adapter's sizes >= 640x480, one per size, the desktop's, the forced `Width`x`Height`, and in borderless/windowed
  (`extra_sizes`) the common sizes of the desktop's aspect ratio (within 1 %, so 1366x768 is 16:9 and 2560x1080 /
  3440x1440 are one 21:9) up to the desktop plus 1/2 and 3/4 of the desktop rounded to even numbers (render-scale
  presets; borderless stretches them). Exclusive fullscreen (stock and `Mode=fullscreen`) gets adapter sizes only,
  since a mode the adapter lacks would fail `CreateDevice`. Sorted ascending, every text <= 9 characters (the
  registry read at 0x61983e is 10 bytes; five-digit widths are dropped), at most the table's slots (the smallest go
  when there are more - unchanged rule, now rarely reached). On the development PC: 24 sizes in both modes (all the
  16:9 extras are adapter modes already).
- Nothing else changed in the game's flow: the slider's step is 1/(n-1) of `count`, Accept compares the chosen
  entry with `Settings\Display\Resolution` and raises `NEW_RESZ_RESTART` / `CheckRez` as before, the close
  function saves the entry to the registry; a resolution applies at the next start (decision 2). `Width`/`Height`
  in the ini still shows as the selected entry because the registry read is answered with it and `build_list`
  lists it.
- ~~Default-on element~~ **Made opt-in in the review fix round** (the brief's default-off rule: no new key, no
  change): `ResolutionList` absent = exactly the behaviour before phase 3 - in the stock mode no mode-list hooks
  and no patch (the game's own list, including its overflow on a >20-size adapter), in the fix's own modes the
  pre-phase-3 list (`curate_modes` into 20 slots, no window extras; `resolution_rules::video_list`).
  `ResolutionList=all` = the 64-slot table + `build_list` in every mode; `game` = the game's 20 slots,
  `build_list` trimmed to 20 in every mode (the overflow fix alone). **For Owen:** the overflow at 0x6e98f0+
  corrupts the default key bindings on the development PC (24 sizes), so `all` as the default is a one-line
  change in `display_rules::options` once he OKs it; until then the launcher can write `ResolutionList=all`.
- Safety of the 64-slot table (review fix round): the relocation now happens only after the `Direct3DCreate8`
  import hook succeeded (before, a failed hook left the table relocated with no list clamp: the writer's
  unbounded loop could run past the DLL's 64 slots), and the two hooks build the list for whatever adapter is
  asked (the writer always asks adapter 0; before, they passed other adapters through unclamped whenever the
  device was on another one). `xml2_test`'s pipe child runs with `ResolutionList=all` and no libIGGfx.dll and
  checks that the table is never touched.
- **Not yet verified in game**: the slider walking 24 entries (3 virtual px per step at 192 px), the value label
  for each, picking 2560x1440 or a render-scale preset -> `NEW_RESZ_RESTART` -> the size after the restart, and
  `Revert to default` re-syncing the slider to "640x480"/the default through the relocated table.

### Review fix round (2026-09-28; three reviewers, `git diff 600bcc8..display`)

Each finding was re-read in the binary before changing anything.

1. **The pipe's `wm` couldn't drive the panel - confirmed, fixed.** The panel's keys come from the DirectInput
   keyboard (1.1 "Input feed", corrected); `wm` removed (refused with a pointer to `tap`), docs corrected;
   `xml2_test` checks the input-path bytes. Drive the panel with `tap UP/DOWN/LEFT/RIGHT/RETURN/ESCAPE`.
2. **Default-off violated - confirmed, fixed.** Without `ResolutionList` nothing touches the Video list in the
   stock mode (no hooks, no relocation; the fix's modes keep their pre-phase-3 list); the rows no longer force
   a Present hook (hooked when the limiter starts); with no keys and no rows nothing is hooked at all. The
   64-slot table as the default awaits Owen's OK (it fixes the stock overflow into the default bindings).
3. **Revert + Accept wrote the fix's variants instead of stock - confirmed, fixed.** A row on its stock value
   removes its key (when the user put it there or after Revert); FrameRate back to stock restores the game's
   spin live (byte-checked undo; the fix paces at 60 if it can't), so frames are never left unpaced.
4. **The 64-slot table could be relocated without the list clamp - confirmed (low likelihood), fixed.** Relocated
   only after the `Direct3DCreate8` hook succeeded; the list hooks answer for every adapter (the writer asks 0).
5. **Mouse messages still reached the unfocused game - confirmed, fixed.** In borderless/windowed the engine's
   window gets a procedure in front that drops 0x200-0x20e while another process has the foreground.

Goal (owner's request): expose display mode (fullscreen / borderless / windowed), a frame-rate cap, vsync
and a modern resolution list inside the game's own *Advanced Options* panel, the way community clients
(iw4x and friends) do, while `xml2-fix.ini` stays the single source of truth so the launcher can edit the
same settings.

All addresses are for the retail `XMen2.exe` (base 0x400000, .text 0x401000-0x67e7b5, 3,129,344 bytes,
Sep 2005) and the engine DLLs shipped with it (`libIGGfx.dll`, `libIGDisplay.dll`, image base 0x10000000).
Everything marked **VERIFIED** was read in the disassembly/decompilation (Ghidra 12.1.3 headless project in
`docs/research/ghidra`, decompiles in `docs/research/decomp_*.c`, full listing `docs/research/xml2_text.asm`,
helper `docs/research/petools.py`). **UNVERIFIED** items are inferences to confirm at implementation time.

---

## 1. Findings

### 1.1 The Advanced Options panel is hand-coded Direct3D UI (Beenox "BXIG" widgets)

**VERIFIED.** The panel is not an XMLB menu. It is built by one 5,990-byte function,
`FUN_0061dc10` (0x61dc10-0x61f375), called from `FUN_005d1dd0` (0x5d1de4, the Controller options menu's
"Advanced" action). The widget classes have RTTI (`.?AVBXIG...@@`, read via the complete-object locators):

| Class (RTTI) | vtable | constructor | `new` size | used for |
| --- | --- | --- | --- | --- |
| `BXIGItem` (base) | 0x6a543c | `FUN_00622770(this, window, id)` | - | common item state |
| `BXIGImage` | 0x6a55e4 | `FUN_00622850(this, window, id, "texs\\x.png")` | 0x1b0 | col/title/bg/help/toggle/tab images |
| `BXIGLabel` | 0x6a5634 | `FUN_006229a0(this, window, id)` | 0x1b0 | text labels ("Advanced Options", resolution value, help text) |
| `BXIGClickableLabel` | 0x6a5724 | `FUN_00624190(this, window, id)` | 0x1b4 | text buttons (Back 0x3f6, REVERT_TO_DEFAULT, Defaults 1-3) |
| `BXIGClickableLabelToggle` | 0x6a5774 | `FUN_006242d0(this, window, id)` | 0x1b4 | the "Resolution" row label (id 0x15) |
| `BXIGClickableImage` | 0x6a56d4 | `FUN_006240b0(this, window, id, png)` | 0x1b4 | `selecteds.png` (id 0x12), `bgright.png` |
| `BXIGSlider` | 0x6a54dc | `FUN_006231b0(this, window, id, "texs\\slider.png")` | 0x1cc | resolution slider (id 7) |
| `BXIGCycle` | 0x6a557c | `FUN_00623e10(this, window, id, "texs\\toggle.png")` | 0x208 | **FSAA (id 6): the "OFF/2x/4x/6x/8x" option cycler - the widget we want for our rows** |
| `BXIGButton` | 0x6a548c | `FUN_00622ca0(this, window, id, "texs\\button.png")` | 0x1b8 | Accept (id 1) |
| `BXIGTabButton` | 0x6a5684 | `FUN_00622f10(this, window, id, "texs\\tabbtn.png")` | 0x1b8 | Player 1-4 tabs (ids 0xa-0xd) |
| `BXIGControlList` | 0x6a57ec | `FUN_00625eb0(...)` | 0x6e0 | key-binding list (id 0xe) |
| `BXIGScrollBar` | 0x6a552c | `FUN_006236e0(...)` | 0x1e4 | list scrollbar (id 0xf) |
| `BXIGWindow` | 0x6a55cc | `FUN_00621ca0(this, d3dDevice, d3d)` | - | item container, input routing, animation |
| `XML2IGConfig : BXIGWindow` | 0x6a4c6c | `FUN_0061c590(this, [0xa0a004], [0xa0a098])`, size 0x285cc | the panel and its popups |

Memory is allocated with `FUN_00671fc2` (operator new, cdecl, arg = size); the builder always does
`new(size)` -> ctor(this=eax, window=[0xa6ad34], id, png).

**Drawing** is raw Direct3D 8 on the engine's device: `[0xa0a098]` = `IDirect3DDevice8*` (vc+0x144),
`[0xa0a09c]` = `IDirect3D8*` (vc+0x140), both set in `FUN_005f7050`. Items own a 0x70-byte vertex buffer
(FVF 0x144 = XYZRHW|DIFFUSE|TEX1, `CreateVertexBuffer` slot +0x5c, `Lock` +0x2c, `Unlock` +0x30) and draw
with `SetTexture`(+0xf4) / `SetRenderState`(+0xc8) / `SetTextureStageState`(+0xfc) / `SetVertexShader`(+0x130)
/ `SetStreamSource`(+0x14c) / `DrawPrimitive`(+0x118). Textures come from loose PNGs through
`FUN_00645434(device, "texs\\x.png", w, h, 1, 0, 0x15, 1, 3, 3, 0, &info, 0, &tex)` (the same loader the HUD
uses). Because everything is pre-transformed vertices, **our rows draw with the game's own code as long as we
construct the game's own widget objects.**

**Coordinate system.** All `setRect` calls use a 640x480 virtual space; `BXIGWindow` scales by
`+0x38 = width/640` (`_DAT_006a3be8` = 1/640) and `+0x3c = height/480` (`_DAT_006a3be4` = 1/480)
(`FUN_00621ca0`). The panel scale float `[panel+0x40]` comes from the display singleton
(`thunk_FUN_005f6df0()->vtbl+0x2c`). Consequence: the panel (and the whole 2D HUD, see 1.7) is stretched to
the screen's aspect ratio.

**Item layout (`BXIGItem`, 0x1b0 bytes).** From `FUN_00622770` and the users:

| offset | meaning |
| --- | --- |
| +0x00 | vtable |
| +0x04..+0x10 | scaled rect x1,y1,x2,y2 (pixels; recomputed by vtable slot 10 `FUN_0061fe50` / slot 6 `FUN_0061ff70`) |
| +0x14..+0x20 | virtual x,y,w,h (set by `FUN_0061fe20(this,x,y,w,h)` which then calls vtbl+0x28) |
| +0x24 | texture; +0x28 vertex buffer; +0x2c parent `BXIGWindow` |
| +0x30 | visible (vtbl+0x14 `FUN_0061fde0(bool)`); +0x31 enabled |
| +0x34 | text (malloc'd copy; vtbl+0x2c `FUN_0061fd50(const char*)`) |
| +0x38 | has text rect; +0x3c..+0x48 text rect (`FUN_0061fdf0(x,y,w,h)`) |
| +0x5c | item id (looked up by `FUN_00621d60(window, id)`) |
| +0x60 | text style/alignment int (vtbl+0x34 `FUN_00538310`; the builder uses 3 for cycles/buttons, 0 for the Resolution label, 4 for the title, 2 for the value label) |
| +0x64 | text colour ARGB (vtbl+0x30 `FUN_0061fd40`; 0xffffffff selected, 0xffcccccc idle) |
| +0x68 / +0x6c | callback function / callback userdata (`FUN_00620160(this, fn, userdata)`) |
| +0x70 / +0x74 | current / previous animation slot (-1 = none) |
| +0x78.. | 7 animation slots x 0x2c bytes (0x78 + 7*0x2c = 0x1ac, the end of the object) |
| +0x1ac | (byte, first byte after the slots) "keyboard-selected" flag, used by the navigation code; the Accept button starts with it set (0x61f03d) |

`BXIGCycle` extras (0x208 bytes): +0x1b4 hovered flag; +0x1b8..+0x1dc up to 10 option strings
(`FUN_006216a0(this, index<10, text)`); +0x1e0 **selected index**; +0x1e4..+0x1f0 scaled option rect;
+0x1f4..+0x200 virtual option rect and +0x204 style (`FUN_00621750(this, x, y, w, h, style)`);
`FUN_00621730(this, index)` sets the selection and redraws. `BXIGSlider`: +0x1b4 value 0..1 (`FUN_00620d10`
reads it), +0x1c4 step (`FUN_00620dd0`), +0x1c8 (`FUN_00469300`), +0x1b8 knob scale (`FUN_00620b70`),
`FUN_00620d20(this, value, force)` sets it.

**Item vtable (BXIGItem 0x6a543c; Cycle 0x6a557c overrides in brackets).** slot 0 (+0x00)
`FUN_0061fd10` fire callback(event) - prints "BXIGItem callback for type %d not implemented" if none;
1 (+0x04) rebuild quad [`FUN_00621b00`]; 3/4 enable/disable; 5 (+0x14) setVisible; 6 (+0x18) move
[`FUN_00621810`]; 7 (+0x1c) mouse-left notification; 8 (+0x20) draw [`FUN_006218f0`]; 9 (+0x24) per-frame
update(dt); 10 (+0x28) recompute rect; 11 (+0x2c) setText; 12 (+0x30) setColour; 13 (+0x34) setStyle;
16 (+0x40) `handleInput(msg, wParam, lParam)` [`FUN_00623bf0`]; 17 (+0x44) destructor.

**Callback protocol.** `void cb(BXIGItem* item, int event, void* userdata)`, events: **1** value changed,
**2** activated/clicked, **3** focus/hover gained, **4** focus lost, **5** window-level "back" (fired by
`BXIGWindow` vtbl+8 `FUN_00621e80` on ESC). Every builder callback gets `userdata` = the `toggle.png`
highlight bar item (see below). UI sounds: `FUN_005d8920()->vtbl+0xe8(6)` click, `(0)` hover.

**Animation slots** (`FUN_00620090(this, slot, struct[11 dwords])` copies into `item+0x78+slot*0x2c` and
writes the slot index at +0x28): f0 = anim function `int fn(BXIGItem*, float dt)` (returns 1 while running),
f1/f2 start x,y, f3/f4 target x,y, f5 delay, f6 duration, f7 elapsed, f10 = slot index. Slot 0/1 are the
panel's enter/exit slides (`FUN_00618ad0`, duration 0.13 s). `BXIGWindow::setAllAnim(slot)`
(`FUN_006222b0(window, slot)`, 0x6222b0) switches every item that is in a slot >= 2 or in none, and is called
with 0 at the end of the builder (enter) and with 1 on Accept/Cancel (exit). `FUN_00622300` (per frame from
`FUN_0061f380`) advances the animations; `FUN_006223b0` reports "exit animation finished" (+0x44).

**The row highlight.** The `toggle.png` `BXIGImage` (created at 0x61dfc3, pos (30,500) = off-screen,
196x38) is the moving highlight bar. Its slots 2/3/4 use `FUN_00617f10`: slot 2 -> off-screen (Accept row),
slot 3 -> also off-screen but shows `selecteds.png` (id 0x12) and whitens label 0x15 (Resolution row),
slot 4 -> y = 0x7d (125), whitens cycle id 6 (FSAA row), 0.05 s. Row callbacks call
`FUN_006200c0(toggle, slot)` (0x6200c0: sets +0x70 = slot, +0x74 = old, elapsed = 0, no-op if already in that
slot) on event 3. `FUN_00617f10` hard-codes ids 0x12/0x15/6, so **our rows need their own highlight
function** (see 4.3).

**Navigation records.** `FUN_00621e00(window, rec)` (0x621e00) appends an 0x18-byte record to
`window+0x18` (count `window+0x28`, capacity `window+0x30`, grows by 100):
`{ BXIGItem* self; left; right; up; down; byte keepFocusLR (+0x14); byte keepFocusUD (+0x15) }`.
The builder registers three: Resolution `{label 0x15, slider, slider, Accept, FSAA, 1, 0}`, FSAA
`{cycle 6, cycle 6, cycle 6, label 0x15, Accept, 1, 0}`, Accept `{button 1, 0, 0, cycle 6, label 0x15, 0, 0}`
(0x61e469-0x61e4a6, 0x61e81a-0x61e861, 0x61f047-0x61f089).
`BXIGWindow::handleMessage` (`FUN_00621ea0`, 0x621ea0) implements keyboard navigation on **WM_KEYUP
(0x101)**: it finds the record whose `self->+0x1ac` is set, picks the neighbour for VK_RETURN (self),
VK_LEFT (+4), VK_UP (+0xc), VK_RIGHT (+8), VK_DOWN (+0x10); if the neighbour is self or the keep flag is set,
the key is forwarded to the neighbour without moving focus (left/right on the Resolution row drive the
slider), otherwise focus moves: `target->+0x1ac = 1`, `target->handleInput(0x101,...)` (a `BXIGCycle` with
+0x1ac set fires event 3), `self->+0x1ac = 0`, `self->handleInput(0x101,...)` (fires event 4).
Mouse (0x200/0x201/0x202/0x20a): hit-test every item's scaled rect (+4..+0x10) and call `handleInput` on
the one under the cursor; `window+0x10` is the captured item. ESC -> `FUN_00621e80` -> window callback
event 5 (`FUN_00617d10`: result 2, exit animation).

**Input feed. VERIFIED (corrected 2026-09-28 in the review fix round; the first reading was backwards):**
the panel's **keys and pad buttons come from DirectInput**, not from window messages. `FUN_006223d0` (the
window message filter, reached only from the game's window callback `FUN_005faa20` -> `FUN_006193e0`)
passes WM_KEYDOWN alone to `FUN_00621ea0` (`cmp eax,0x102` / `lea edx,[eax-2]; cmp edx,0xfe; ja drop`: 0x101
- 2 = 0xff is dropped, and the index table at 0x62245c maps only 0x100 to the call), and `FUN_00621ea0`
acts on keys only for WM_KEYUP (0x621ebf) - so a real or posted key message never reaches a widget.
The keys the panel acts on are made by `FUN_00619070`, run every frame through the `CMenuSebas` vtable
0x6a1f64 +0x38 (0x5d1e90 -> `jmp 0x619070`; +0x10 of the same vtable, 0x5d1dd0, is what calls the builder):
released-key edges of the game's DirectInput keyboard (`FUN_00627100`: byte `[obj+0x26e4+dik]` set, the
previous frame, and `[obj+0x25e4+dik]` clear, the current one; DIK Esc/Enter/Up/Down/Left/Right) and pad
buttons (`FUN_00618f20`) become VK 0x1B/0x0D/0x26/0x28/0x25/0x27 in a synthetic `FUN_00621ea0(0x101, vk, ..)`
call (0x6192c7). The keyboard state is `IDirectInputDevice8::GetDeviceState(0x100, obj+0x25e4)` at 0x62861e
(vtable +0x24, slot 9) - the slot the `[Test]` pipe hooks, so **`tap DOWN` / `tap ENTER` / `tap LEFT` drive
the panel**. Mouse messages (0x200-0x20a) do pass the filter and are hit-tested on lParam. (`xml2_test`
checks these bytes against the retail exe.)

**Panel lifetime.** `DAT_00a6ad34` = the panel; `DAT_00a6ad38/3c/40/44` = popups (revert-defaults confirm,
NEW_RESZ_RESTART warning `FUN_0061cbf0`, CANCEL_WARNING `FUN_0061c740`, unbound-keys warning
`FUN_0061d0a0`; each is a fresh `XML2IGConfig` with two `BXIGClickableLabelToggle` yes/no buttons
(string ids 0x7ea/0x7eb) and a Back button (0x3f6)). `[panel+0x48]` = result: 1 accept, 2 cancel, 3 = popup
"yes"; `[panel+0x285c8]` = dirty flag; `[0xa6ad48]` = force-close. Per frame `FUN_0061f380` (0x61f380)
advances animations and, once the exit animation has finished, applies (see 1.3) and destroys the panel
(`FUN_00617870` + `operator delete`). `FUN_00619390` draws (`FUN_006223a0` -> `BXIGWindow::draw`
0x624520 which calls each item's slot 8).

### 1.2 The builder, row by row (`FUN_0061dc10`)

Prologue: `FUN_00619770` (load Settings\Display\* + Controls from the registry into globals),
`FUN_00619ac0([0xa0a09c])` (rebuild the resolution table, 1.4), `new(0x285cc)` + `FUN_0061c590` -> panel,
`panel->vtbl+4(FUN_00617d10, 0)` (window callback).

| # | at | widget | id | png / text | rect (x,y,w,h) virtual | callback | notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1-2 | 0x61dd30, 0x61dd7c | Image | 0 | col.png | (-66,0,62,480), (635,0,62,480) | - | side columns |
| 3 | 0x61ddcb | Image | 0 | title.png | (-45,14,275,55) | - | |
| 4 | 0x61de5d | Label | 0 | lookup("Advanced Options"), style 4 | (0,33,239,24) | - | |
| 5 | 0x61df0a | Image | 0 | bgleft.png | (-45,77,280,276) | - | **left pane background: rows live here** |
| 6 | 0x61dfcd | Image | 0 | toggle.png | (30,500,196,38) | - | highlight bar; anim slots 2,3,4 |
| 7 | 0x61e10d | Image | 0 | helpb.png | (-45,415,721,60) | - | |
| 8 | 0x61e1a8 | ClickableLabel | 0 | string id 0x3f6 (Back), style 3 | (80,428,80,30) | `FUN_00617bb0` | cancel |
| 9 | 0x61e263 | ClickableLabel | 0 | lookup("REVERT_TO_DEFAULT"), style 3 | (378,428,150,30) | `FUN_0061dba0` | opens revert popup |
| 10 | 0x61e322 | ClickableImage | 0x12 | selecteds.png | (30,102,196,20) | - | hidden (`setVisible(0)`) |
| 11 | 0x61e3c4 | ClickableLabelToggle | 0x15 | lookup("Resolution"), style 0 | (33,102,192,36) | `FUN_00618bc0` | nav record 1 |
| 12 | 0x61e4d6 | Label | 8 | (value text, style 2) | (30,103,187,12) | - | shows "WxH", updated by the slider callback |
| 13 | 0x61e56a | Slider | 7 | slider.png | (30,124,192,12) | `FUN_006180e0` | knob 0.84, step 1/(n-1), value = index/(n-1) of the current resolution |
| 14 | 0x61e6d6 | **Cycle** | **6** | toggle.png; text lookup("FSAA") style 3; options 0..4 = OFF/2x/4x/6x/8x (2x..8x only if `[0xa09e84..87]` support bytes are set); option rect `FUN_00621750(177,146,40,18,5)`; `FUN_00621730([0xa68c98])` | **(33,146,196,12)** | `FUN_00618c00` | nav record 2; **the template for our rows** |
| 15 | 0x61e896 | Image | 0 | tabimg.png | (269,33,408,44) | - | right pane |
| 16-19 | 0x61e936.. | TabButton | 0xa-0xd | Player 1-4 | (281/363/445/527,50,77,16) | `FUN_00618270` | |
| 20 | 0x61ec1b | ClickableImage | 0 | bgright.png | (244,74,433,250) | `FUN_00618c50` | |
| 21 | 0x61ece8 | ControlList | 0xe | controllist/mouse/joy1-4.png | (284,91,300,140) | `FUN_00618390` | 42 actions x 0x94 from 0xa68f98 |
| 22 | 0x61ee3b | ScrollBar | 0xf | scrollbar.png | (584,91,12,140) | - | |
| 23 | 0x61eedb | Label | 9 | "" style 0 | (284,239,312,62) | - | help text |
| 24 | 0x61ef82 | Button | 1 | button.png, lookup("Accept") | (-45,335,280,61) | `FUN_0061d550` | +0x1ac = 1 (initial focus); nav record 3 |
| 25 | 0x61f0bd | Image | 0 | tabimg2.png | (270,327,408,44) | - | |
| 26-28 | 0x61f147.. | ClickableLabel | 0x16-0x18 | Defaults 1/2/3, style 4 | (291/394/497,340,85,25) | `FUN_006188c0` (userdata 0/1/2) | |
| end | 0x61f356 | `call FUN_006222b0(panel, 0)` | | | | | **hook site A** (bytes `E8 55 2F 00 00`) |

Every item also gets slot 0 and slot 1 copies of the enter/exit descriptors (two `FUN_00620090` calls).
The left pane (bgleft: y 77..353) holds only rows at y=102 (Resolution), 124 (slider), 146 (FSAA); **y 165
to ~330 is empty** - room for six 22-px rows before the Accept button at y=335. Text through
`FUN_00629ba0([0xa6b174], key)` (1.8).

### 1.3 Read-on-open, Accept, Cancel, Revert - and what "apply" means today

**VERIFIED.**
- Open: `FUN_00619770` loads `Settings\Display\FSAA` -> `DAT_00a68c98`, `Resolution` -> `DAT_00a68d9c`
  (10-byte buffer, "WxH"), etc.; the builder positions the slider/cycle from these globals.
- Accept (`FUN_0061d550`, event 2): reads slider (`FUN_00620d10` -> index) and cycle `+0x1e0`, checks
  unbound keys, compares with the current globals; unchanged -> result 1; changed and `WarningRes`
  (`DAT_00a68dac`) -> `FUN_0061cbf0` popup **"NEW_RESZ_RESTART"** (yes -> result 1, "no more warnings" ->
  result 3 clears WarningRes); changed and no warning -> `FUN_0061c110` (`Settings\CheckRez = 1`) +
  result 1. Then `FUN_006222b0(1)` (exit animation).
- Close (`FUN_0061f380`, after the exit animation): result 1 -> copies cycle 6 into `DAT_00a68c98`, the
  slider's table entry into `DAT_00a68d9c`, gamepad names and bindings, then **`FUN_00619440` (0x61f8a4,
  hook site B)** writes everything to `HKCU\Software\Activision\X-Men Legends 2\...` through
  `FUN_00616b20` (`RegCreateKeyA`/`RegSetValueExA`; `FUN_00616df0` DWORD, `FUN_00616dc0` string,
  `FUN_00616f20` bool). Result 2 -> **`FUN_00619770` (0x61f8be, hook site C)** reloads from the registry
  (discard). Revert popup accepted -> **`FUN_006196c0` (0x61f667, hook site D)** loads defaults into the
  globals, then re-syncs slider (0x61f706), cycle (0x61f728), tabs and list.
- **Resolution and FSAA never apply live.** No device reset follows Accept; the new values take effect at
  the next launch (`FUN_005fac10` creates the device from the registry at startup; `CheckRez`/`RestartOldRez`
  are the crash guard: 0x61bf80 on boot, `FUN_0061c1d0` clears `RestartOldRez` once a frame has rendered,
  0x401f50). XMen2.exe imports no `CreateProcess`/`ShellExecute`: it cannot restart itself. The stock
  UX is therefore already "Accept, then restart".

### 1.4 The resolution list

**VERIFIED.** `FUN_00619ac0(IDirect3D8*)` (0x619ac0): `GetAdapterModeCount` / `EnumAdapterModes`,
filter `FUN_00619a40` (>= 640x480, unique WxH), `qsort` `FUN_00617170` (width, height, refresh, format),
count -> `DAT_00a68da8`, and `sprintf("%dx%d")` into **20 x 12-byte slots at 0x6e9800 with no bounds
check**. The bytes right after the table (0x6e98f0..) belong to the default-bindings data (`0x6e9940 +
n*0x1848` is the "Defaults 1-3" table used by `FUN_006188c0`), so a stock overflow (24 modes on the owner's
PC) corrupts binding defaults. xml2-fix's `curate_modes` currently trims the list to 20 for that reason.

Users of the table base (all imm32, patchable): `0x6181bf lea eax,[edx*4+0x6e9800]` (+`lea` x3 = x12),
`0x61d61f`, `0x61f57b`, `0x61f843` (same pattern), `0x619b95 mov ebx,0x6e9800` (writer), `0x61e636 mov
eax,0x6e9800` (builder: find current index), `0x61f6a1 mov ebp,0x6e9800` (revert re-sync). The count is a
plain global; the slider maps index/(count-1). The registry string is read into a 10-byte buffer
(`FUN_00616e10(...,&DAT_00a68d9c,10)` at 0x61983e/0x61b4d1), so entries must stay <= 9 characters
("3840x2160" fits; five-digit widths do not).

### 1.5 Frame timing - VERIFIED, with one UNVERIFIED flag

`CClient` is a static singleton at **0x6f3ac4** (vtable 0x67fff4, RTTI `.?AVCClient@@`; `FUN_00401b00`
returns its address). `CClient::frame` = **`FUN_00401d70`** (vtable slot 3), decompiled in
`decomp_batch4.c`:

1. `+0x18` (0x6f3adc) **minimum frame time is rewritten every frame**: `1/60` (imm32 0x3c888889 at
   **0x401db7**, instruction `C7 46 18 89 88 88 3C` at 0x401db4) or `1/30` (0x3d088889 at 0x401dae,
   instruction at 0x401dab) when `FUN_00610d20(FUN_00612be0())` (byte +0x420 of the 0xa53058 object) is
   set. The `[DISPLAY_OPTIONS] max_fps` config key read in `CClient::init` (`FUN_004016f0`, 0x4017df,
   default 60, `> 60` => 0 = uncapped) is therefore overwritten before it can matter. **Stock XML2 is
   hard-capped at 60 fps (30 in that mode).** UNVERIFIED: what the +0x420 flag means (movie, loading or
   network game are the candidates).
2. Real time: `FUN_0055b610()->vtbl+0x28(0)` = `FUN_0055b470(this, gameTime)`: `(*(this+4)->vtbl+0x60)()
   * _DAT_0069a7b0` - the engine igTimer (libIGCore `igWin32LongTimer`, QueryPerformanceCounter-based)
   converted to seconds; `getTime(1)` is game time, frozen while paused (+0x30/+0x34).
3. `+0x8 = now - +0x4` (**variable dt**, no clamp seen in this function); optional fixed step `+0x10`
   (`FUN_00401660`, clamped to 0.017 s by `_DAT_006801b0`) is 0 in normal play. Game update
   `FUN_005d8920()->vtbl+0x2c(dt)` receives `now - +0x20`.
4. The limiter (0x401f9c-0x40200c) is a **busy spin**: `while (now - +0x1c < +0x18) now = getTime(0);`
   skipped when `FUN_005d8920()->vtbl+0x1b4()` is true (movie playing, UNVERIFIED name). A 10-frame
   average feeds the fps display `_DAT_0081d6c0`.

Conclusion: game speed is wall-clock driven, so an uncapped 180 Hz window or a 30 fps cap does not change
the simulation speed by design (animation/physics take dt; script `waittimed` counts game time). What a
higher cap can expose is per-frame-increment code that assumes ~60 Hz (camera smoothing, UI fades) -
UNVERIFIED, to be checked in the test tour (compare a timed tour step at 60 vs 144 vs uncapped). The
stock cap also means "vsync off" in fullscreen never showed >60 fps.

### 1.6 Present parameters, vsync and windowed mode (libIGGfx `igDxVisualContext`)

**VERIFIED** (`decomp_gfx.c`, `decomp_gfx2.c`):
- `setDeviceParameters(int)` @1002cfe0 fills the `D3DPRESENT_PARAMETERS` at vc+0x150 from the request
  at vc+0x154 and `alchemy.ini` `[GFX]`: `multiSampleType`, `lockableBackbuffer` (Flags |= 1),
  `BackBufferFormat = getRenderTargetFormat`, `Windowed = !vc[0x180]`.
  - Fullscreen: size from the render destination, `FullScreen_RefreshRateInHz = getRefreshRate` (ini
    `refreshRate`, default 0), `FullScreen_PresentationInterval = FUN_1002cae0(caps)`: ini
    `presentationInterval` 0 (default) -> **IMMEDIATE (0x80000000) if the caps allow it** (they do on any
    modern driver), 1 -> ONE, 2 -> TWO, 3 -> THREE, 4 -> FOUR, each falling back to the next supported.
    So stock fullscreen runs **vsync off**, limited only by the 60 fps cap above; vsync on = interval ONE.
  - Windowed (`vc[0x180]==0`): `RefreshRate = request+0x2c`, `PresentationInterval = 0`
    (D3D8 requires DEFAULT when windowed), and ini `windowedVSync=true` sets
    `SwapEffect = 4 (D3DSWAPEFFECT_COPY_VSYNC)`, otherwise the requested swap effect. D3D8 has **no
    IMMEDIATE interval for windowed devices**; COPY_VSYNC is its only windowed vsync switch.
  - **MEASURED 2026-09-28** (`xml2_test` `check_windowed_presents`: a 64x48 unfocused tool window, 90 timed
    Presents after 10 warm-up frames, Windows 11 26200, desktop 2560x1440 @ 180 Hz, RTX-class HAL device,
    software vertex processing):

    | windowed device | ms/frame | fps | waits for vblank? |
    | --- | --- | --- | --- |
    | DISCARD, interval DEFAULT, window hidden | 0.026-0.029 | ~35,000 | no |
    | DISCARD, interval DEFAULT, window shown | 0.125-0.154 | 6,500-8,000 | **no** |
    | COPY_VSYNC, interval DEFAULT, window shown | 31.1-31.8 | **~32** | waits, but far below 180 Hz |
    | COPY_VSYNC, same, `timeBeginPeriod(1)` | 30.3 | 33 | same (not a scheduler-tick artefact) |
    | COPY, interval DEFAULT, window shown | 0.125-0.205 | 4,900-8,000 | no |
    | `CreateDevice` windowed, interval IMMEDIATE | - | - | **refused, D3DERR_INVALIDCALL (0x8876086C)** |
    | `CreateDevice` windowed, interval ONE | - | - | refused, D3DERR_INVALIDCALL |

    Conclusion: a windowed D3D8 present with the default interval **never waits** for the vertical blank
    under DWM (the game runs free, capped only by its spin or our limiter), and COPY_VSYNC, the only
    windowed sync D3D8 offers, is unusable (~32 fps on a 180 Hz desktop; DWM shows both without tearing).
    Decision 4 therefore resolves to: VSync **is** a real toggle in the windowed modes, implemented as
    "on = frames paced at the desktop refresh rate by the limiter", never as COPY_VSYNC; `VSync=0` there
    only undoes the engine's own `windowedVSync`. Fullscreen VSync is the presentation interval as planned.
- `endDraw` @1002eb70: `EndScene` (+0x8c) then `Present(0,0,0,0)` (+0x3c); `D3DERR_DEVICELOST` sets
  vc[0x15c]. `beginDraw` -> `getLastError` @1002dac0: `TestCooperativeLevel`, `resetDevice` @1002ae40
  (`releaseVolatileResources` + `Reset(vc+0x150)` + `restoreVolatileResources` + `setupAll`), else
  `Sleep(200)`. `resetDevice` and `setVideoMode(igVideoFormat*)` @1002f040 are **exported by name**, so
  xml2-fix can call them for a live vsync change.
- `igWin32Window::setVideoMode(fullscreen, w, h, bpp)` @10005fc0 (libIGDisplay, exported): toggles the
  style bits (0xcf0000) with `SetWindowLongA`, calls `vc->vtbl+0x3f8` with `{fullscreen, 2, 1, 60.0}`, and
  for fullscreen re-creates the render destination (+0xc0/+0xc4/+0xc8/+0xcc), `setViewport` (+0x2e8),
  `MoveWindow(0,0,w,h)`; `setFullScreenState(bool)` @10006250 = `setVideoMode(b,-1,-1,-1)`. This is the
  engine's own live mode switch and is the candidate for a live display-mode change (5.).

### 1.7 HUD and aspect ratio at 21:9 / 32:9

**VERIFIED:** `FUN_005fac10` stores `w`, `h` in `[0xa09ffc]`/`[0xa0a000]` and `aspect = w/h` in the display
singleton (+0x10, read through `FUN_005f5f90` and used by the projection getters `FUN_005f6000`/`FUN_005f6010`
= aspect-scaled frustum), so the 3D view gets the true aspect (Hor+ behaviour is **UNVERIFIED** but the
projection is built from w/h and a fixed vertical constant `+0x48`, which is Hor+). The 2D layer (HUD
`FUN_005fc100`, menus, mouse `FUN_005f9eb0`, the BXIG panel) multiplies normalised or 640x480 coordinates by
`w/640` and `h/480` separately (`_DAT_006e83e8/_DAT_006e83ec`, `fimul [0xa09ffc]`/`[0xa0a000]`): **the HUD is
stretched at any non-4:3 ratio** - already at 16:9 today, more so at 21:9 and 32:9. `_DAT_006e8488 =
(4/3)/aspect` is computed at 0x5fad76 and never read (dead). No FOV option exists. An aspect-correct HUD
(letterbox the 2D layer at 4:3-scaled size, centred) is a separate feature; out of scope here but the two
scale globals and the panel's `+0x38/+0x3c` are the levers.

### 1.8 Localisation of labels

**VERIFIED.** `FUN_00629ba0(table, key)` (0x629ba0) looks `key=` up in `[0xa6b174]`, a `KEY=value`
line table loaded by `FUN_00403220` from **`igct<lang>.bnx`** in the game folder (`igct.bnx` English,
`igctfre/ger/ita/spa/pol/rus.bnx`; language code at 0x6d4b90, "none" until set) via `FUN_006299b0`
(fopen "rb", split on newlines). **If the key is missing the key itself is returned**, so we can pass
our own English strings ("Display mode", "Frame rate") and they render as-is; the engine font renders
ASCII. For other languages we ship our own tiny table keyed by the loaded language (read 0x6d4b90 or the
`igct*.bnx` name) - no game file is modified. Beware the 0x61e29a-style strings such as
"REVERT_TO_DEFAULT" are keys that exist in `igct.bnx`; ours must not collide (prefix `XF_`).

---

## 2. Approach

### 2.1 Chosen: extend the left pane with the game's own `BXIGCycle` rows (hook A) + hook the four
apply/discard sites (B, C, D) + persist to `xml2-fix.ini`

- One call-site patch at **0x61f356** (`call FUN_006222b0` -> `call xf_finish_panel`), reached only from the
  builder, with `ecx = panel` and the pushed `0`. Our function builds the extra rows with the game's
  constructors, fixes the navigation graph, then tail-calls the original `FUN_006222b0(panel, 0)`. No
  mid-function detour, no relocation of game code.
- Rows are `BXIGCycle` objects created exactly like the FSAA row (0x61e6a6-0x61e861 is the template),
  placed at y = 170, 194, 218, 242 (22-24 px pitch, under FSAA at 146, above Accept at 335; bgleft ends at
  353). Same texture (`texs\toggle.png`), same style 3, same option rect geometry shifted in y, same
  enter/exit animation descriptors (copy slots 0 and 1 from the FSAA cycle item: `memcpy(new+0x78,
  fsaa+0x78, 2*0x2c)` and set +0x70 = -1 so `FUN_006222b0(0)` starts them).
- Rows: **Display mode** (Fullscreen / Borderless / Windowed), **Frame rate** (Off / 30 / 60 / 120 / 144 /
  Refresh), **VSync** (Off / On), later **Resolution list** handled by widening the existing slider (1.4,
  phase 3) rather than a new row. A fifth optional row **Run in background** (Off/On) fits.
- The engine draws, animates, hit-tests and navigates them; we only supply the callback (event 1 -> store
  pending value + set `[panel+0x285c8] = 1`; event 3/4 -> highlight on/off) and the accept/discard logic.
- Persistence: on hook B (`FUN_00619440` from `FUN_0061f380`, result 1) write `[Display]` keys with
  `WritePrivateProfileStringW` to `xml2-fix.ini` and apply what can apply live; on hook C (result 2) drop
  pending values; on hook D (revert) reset pending values to the defaults and re-sync our cycles
  (`FUN_00621730`). The launcher keeps editing the same ini; at panel open we read the ini afresh so a
  launcher edit made while the game runs shows correctly.

Why this and not the alternatives:
- **A second tab / repurposed pane**: the tabs (Player 1-4) belong to the key-binding list and the right
  pane is 100% occupied; the left pane has ~180 px of empty space designed for exactly this kind of row.
  A new tab would need a new list widget and page switching - all custom drawing. Rejected.
- **Hooking `FUN_00621e00` (nav registration) or the builder entry**: more sites, fragile ordering; the
  finalize call site is unique and late. Rejected.
- **Replacing the panel with our own D3D-drawn UI**: throws away the animation/input/sound/localisation
  the engine already provides and looks foreign. Rejected.
- **An XMLB menu entry**: the Video menu is data-driven (`CMenuOptions`), but adding items there means
  patching game data files, which the project refuses to do. Rejected.
- **Patching `max_fps`**: dead (1.5). Rejected.

### 2.2 Frame cap: our limiter in the `Present` hook, the game's spin disabled

Patch the imm32 at **0x401db7** (1/60 -> 0.0f) so `CClient::frame` never spins in normal play; leave the
1/30 constant at 0x401dae alone so whatever mode uses it still gets its cap (UNVERIFIED semantics).
Implement the cap in `display.cpp`'s `hooked_present` (install it whenever a cap or vsync option is active,
not only with `emulate_pause || frame_hook`): QPC-based target interval, hybrid wait (a high-resolution
waitable timer `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)` with a 0.5 ms spin tail;
fall back to `Sleep(1)` + spin), "Refresh" = the desktop refresh from `EnumDisplaySettings`.

### 2.3 Vsync: rewrite in `rewrite_present`

Fullscreen: `FullScreen_PresentationInterval = ONE` (on) or `IMMEDIATE` (off, the stock value), when the
adapter's `PresentationIntervals` caps offer it. Windowed / borderless: **not** COPY_VSYNC (measured at ~32 fps,
1.6) - "on" is the limiter pacing at the desktop refresh rate (`effective_target`, never above `FrameRate`),
"off" = `DISCARD` (undoing only an engine-side `windowedVSync`). *Implemented in phase 1.*
Live apply (phase 2): after Accept call the exported `igDxVisualContext::resetDevice` on the vc (pointer =
display singleton `[0xa0a138]+0xc`, or `igWin32Window+8`); our `hooked_reset` rewrites the parameters. The
windowed "on" needs no reset at all (a pacer retarget).

### 2.4 Display mode

Phase 1: the row edits the ini and shows "restart to apply" (the stock UX for resolution, 1.3).
Phase 4 (experimental): live switch through `igWin32Window::setFullScreenState` @10006250 with our
`SetWindowLongA`/`MoveWindow`/`Reset` hooks already in place; keep a 15 s revert timer like iw4x
(revert on ESC/no confirmation) because a failed Reset leaves the device lost.

### 2.5 Resolution list beyond 20

Relocate the 12-byte string table to a 64-slot buffer in the DLL by patching the seven imm32 users listed
in 1.4 (all `lea`/`mov` immediates; the count global and slider maths need no change), and lift the
`game_mode_slots` cap in `curate_modes`. Curated content: adapter modes >= 640x480, the desktop size,
common 16:9 / 16:10 / 21:9 sizes not above the desktop, plus render-scale presets (0.5x/0.75x of the
desktop) in borderless mode; keep every string <= 9 chars.

---

## 3. Hook points and data (exact)

### 3.1 Code patches (all in XMen2.exe .text; `VirtualProtect` + write, verify the original bytes first)

| site | address | original bytes | patch | when |
| --- | --- | --- | --- | --- |
| A finalize | 0x61f356 | `E8 55 2F 00 00` (`call 0x6222b0`) | `call xf_finish_panel` | `[Display] InGameOptions=1` (default) |
| B save | 0x61f8a4 | `E8 97 9B FF FF` (`call 0x619440`) | `call xf_on_accept` (calls 0x619440 first) | same |
| C cancel | 0x61f8be | `E8 AD 9E FF FF` (`call 0x619770`) | `call xf_on_cancel` (calls 0x619770 first) | same |
| D revert | 0x61f667 | `E8 54 A0 FF FF` (`call 0x6196c0`) | `call xf_on_revert` (calls 0x6196c0, then re-syncs our cycles) | same |
| E 60 fps spin | 0x401db7 | `89 88 88 3C` (imm32 of `C7 46 18 ..` at 0x401db4) | `00 00 00 00` | a frame cap or "Off" is configured |
| F-L table base | 0x6181c2, 0x61d622, 0x61f57e, 0x61f846 (`8D 04 95/85 imm32` = `lea eax,[reg*4+imm32]`, imm at +3), 0x619b96, 0x61e637, 0x61f6a2 (`BB/B8/BD imm32` = `mov ebx/eax/ebp, imm32`, imm at +1) | `00 98 6E 00` | address of our 64x12 table | phase 3 |

Read the bytes at each site and compare before patching (the exe has one known retail build; refuse
politely and log if they differ). Also verify `DAT_00a6ad34` is the panel at hook A (`ecx`).

### 3.2 Game functions we call (thiscall unless noted)

| purpose | address | signature |
| --- | --- | --- |
| operator new | 0x671fc2 | cdecl `void* (size_t)` |
| BXIGCycle ctor | 0x623e10 | `BXIGCycle* (this, XML2IGConfig* window, int id, const char* png)` |
| BXIGLabel ctor | 0x6229a0 | `(this, window, int id)` (for a "restart required" status line) |
| set rect | 0x61fe20 | `(this, int x, int y, int w, int h)` virtual 640x480 |
| set text | vtbl+0x2c (0x61fd50) | `(this, const char*)` (copies) |
| set style | vtbl+0x34 (0x538310) | `(this, int)` use 3 |
| set colour | vtbl+0x30 (0x61fd40) | `(this, DWORD argb)` |
| add option | 0x6216a0 | `(this, int index 0..9, const char*)` |
| option rect | 0x621750 | `(this, int x, int y, int w, int h, int style=5)` |
| set selection | 0x621730 | `(this, int index)` |
| get selection | `*(int*)(cycle+0x1e0)` | |
| set anim slot | 0x620090 | `(this, int slot, struct anim[11])` by value (44 bytes pushed) |
| set callback | 0x620160 | `(this, void (*cb)(BXIGItem*, int, void*), void* userdata)` |
| register nav | 0x621e00 | `(window, xf_nav_record*)` record is `new`'d 0x18 bytes, game frees it |
| find item by id | 0x621d60 | `BXIGItem* (window, int id)` |
| select highlight | 0x6200c0 | `(toggleItem, int slot)` |
| lookup text | 0x629ba0 | cdecl `const char* (table = [0xa6b174], const char* key)` |
| UI sound | `FUN_005d8920()->vtbl+0xe8(int)` | 6 click, 0 hover |
| engine reset | libIGGfx `?resetDevice@igDxVisualContext@Gfx@Gap@@QAE_NXZ` | `bool (this)` |
| engine mode switch | libIGDisplay `?setFullScreenState@igWin32Window@Display@Gap@@UAEX_N@Z` | `(this, bool)` |

Item ids for our rows: 0x40 (mode), 0x41 (frame rate), 0x42 (vsync), 0x43 (run in background),
0x4f (status label). `FUN_00621d60` is used by the game only for ids 1, 6-9, 0xa-0xf, 0x12-0x18.

### 3.3 Row construction (mirror of 0x61e6a6-0x61e861)

```
cycle = new(0x208); FUN_00623e10(cycle, panel, id, "texs\\toggle.png");
FUN_0061fe20(cycle, 33, y, 196, 12);            // label rect, y = 170 + 24*n
setText(cycle, xf_text("XF_DISPLAY_MODE"));     // fallback = our English string
setStyle(cycle, 3);
for i: FUN_006216a0(cycle, i, xf_text(option_i));
FUN_00621750(cycle, 177, y, 40, 18, 5);          // value column, same x as FSAA
FUN_00621730(cycle, current_index);
memcpy(cycle+0x78, fsaa+0x78, 2*0x2c); cycle+0x70 = -1;   // enter/exit slides like every row
FUN_00620160(cycle, xf_row_callback, toggle);   // toggle = the highlight bar; it has id 0 so it cannot be
                                                // found with FUN_00621d60 - read it from the FSAA cycle's
                                                // callback userdata: toggle = *(BXIGItem**)(fsaa + 0x6c)
rec = new(0x18); rec = {cycle, cycle, cycle, prev_row_item, next_row_item, 1, 0}; FUN_00621e00(panel, rec);
```
Relink: walk `panel+0x18[0..count)` (count at `panel+0x28`), find the record whose `self` is the FSAA cycle
(`self+0x5c == 6`) and set its `down` to our first row; find the Accept record (`self+0x5c == 1`) and set
its `up` to our last row; our last row's `down` = Accept, first row's `up` = FSAA cycle. Keep the toggle
highlight bar pointer (`fsaa_cycle+0x6c`) for `FUN_006200c0`.

Callback (`xf_row_callback(item, event, toggle)`): event 1 -> pending value = `item+0x1e0`, panel dirty
(`*(byte*)(panel+0x285c8) = 1`), refresh the status label ("Restart required: display mode"), sound 6;
event 3 -> `xf_highlight(toggle, item)` + colour 0xffffffff + sound 0; event 4 -> colour 0xffcccccc.

`xf_highlight(toggle, item)`: our own anim function in **slot 5** of the toggle item (slots 0-4 are the
game's; 5 and 6 are free of the 7): write `f0 = xf_toggle_anim, f3 = 30, f4 = item_y - 21, f5 = 0,
f6 = 0.05`, force `toggle+0x74 = toggle+0x70; toggle+0x70 = 5; slot5.elapsed = 0` (bypassing
`FUN_006200c0`'s same-slot early-out so moving between two of our rows re-animates). `xf_toggle_anim` copies
`FUN_00617f10`'s behaviour: on elapsed == 0 capture the current position, hide item 0x12, grey label 0x15,
cycle 6 and our cycles, whiten the target; then interpolate with `FUN_0061fe20`. When focus returns to the
game's rows, their `FUN_00617f10` greys only 0x15 and 6 - our event-4 handler greys ours.

### 3.4 Accept / cancel / revert glue

- `xf_on_accept()` (hook B, after the original save): write ini; apply live: frame cap (atomic store),
  vsync (`resetDevice`), `RunInBackground`; if display mode changed and live switching is off, log
  "restart required". Values that need a restart are shown in the status label while the panel is open
  (the game itself uses the NEW_RESZ_RESTART popup for resolution; we do not add popups in phase 1).
- `xf_on_cancel()` (hook C): pending = current.
- `xf_on_revert()` (hook D, after `FUN_006196c0`): pending = defaults, `FUN_00621730` our cycles
  (find by id through `FUN_00621d60` - the panel pointer is `DAT_00a6ad34`).
- Panel destruction: our item pointers are owned by the window (`FUN_00624490` frees items); we keep only
  ids and re-find through `FUN_00621d60`. Reset our statics when `DAT_00a6ad34` becomes 0.

### 3.5 ini keys (`xml2-fix.ini`, `[Display]`) - the launcher writes the same

| key | values | default | applies |
| --- | --- | --- | --- |
| `Mode` | `fullscreen` / `borderless` / `windowed` (existing) | absent = stock | restart (phase 1), live (phase 4) |
| `Width`, `Height` | existing | 0 | restart |
| `Topmost`, `RunInBackground` | existing | 0 / 1 | live |
| `FrameRate` | `0` (unlimited) / `10`..`1000` / `refresh` | absent = the game's own 60 fps cap, untouched (decision 1) | live (phase 1 done: start-up) |
| `VSync` | `0` / `1` | absent = the engine's own interval | fullscreen: via reset; windowed: pacer retarget (phase 1 done: start-up) |
| `InGameOptions` | `0` / `1` | `1` | start |
| `ResolutionList` | `game` (the game's 20 slots, the fix's list trimmed to them in every mode) / `all` (the relocated 64-slot table, the fix's list in every mode) | absent = as before phase 3 (the fix's list only with a `Mode`, 20 slots) | start (phase 3 done) |

The in-game cycles show exactly these values; "Refresh" reads as "Refresh rate (180 Hz)". Unknown ini
values fall back to the default and are logged.

---

## 4. Phases (each shippable)

1. **Frame cap + vsync in the ini and the Present hook** (no UI) - **DONE 2026-09-28**: patch E, limiter in
   `hooked_present` (`frame_rate::on_present`), vsync in `rewrite_present`, `[Display] FrameRate/VSync`, the
   measured fps logged after the 2nd and 30th second and live in the pipe's `status`. The windowed-vsync
   question (1.6) is answered by `xml2_test`.
2. **In-game rows (display mode, frame rate, vsync, run in background)**: hooks A-D, rows, highlight,
   status label, ini persistence, live apply for cap/vsync/background; display mode = restart notice -
   **DONE 2026-09-28** (see "Phase 2 as implemented"), in-game verification pending.
3. **Resolution list**: relocate the table (F-L), 64 slots, curated list + render-scale presets; keep the
   game's NEW_RESZ_RESTART flow (WarningRes) as is - **DONE 2026-09-28** (see "Phase 3 as implemented"),
   in-game verification pending.
4. **Live display-mode switch (experimental, ini-gated `LiveModeSwitch=1`)**: `setFullScreenState`
   through our window/device hooks, 15 s revert prompt reusing the game's popup builder pattern
   (`FUN_0061c740` is the template; result codes 1/3 are read in `FUN_0061f380`).
5. **Polish**: translations for fre/ger/ita/spa from the loaded `igct*.bnx` language, README/launcher fields.
   (A pipe command posting WM_KEYUP was planned here to drive the panel; not needed - the panel reads the
   DirectInput keyboard, so `tap` drives it - and removed.)

---

## 5. Risks

- **Retail build only**: every patch is address-bound; check bytes, log and skip on mismatch (demo exe has
  the same code but different addresses - `DAT_006f3c2d` demo flag exists, but we do not support it).
- **Stack/register assumptions at hook A**: `ecx = panel` and one pushed arg; our function must be
  `__thiscall`-compatible and preserve `ebx/esi/edi/ebp` (the builder uses ebx = FSAA cycle, ebp = last
  button afterwards only for the return, but be safe).
- **Anim slot 5/6 of the toggle bar**: the object is exactly 0x1b0 bytes and slot 6 ends at +0x1ac (the
  byte the nav code uses); slot 5 is safe, slot 6 overlaps nothing but stay on 5.
- **`FUN_00621e00` records are freed by the game** (`FUN_00624490`, UNVERIFIED which function frees them) -
  allocate them with the game's `operator new` (0x671fc2) so `operator delete` matches (MSVCR71 heap).
- **Device reset for vsync** while the game renders: call it from the render thread (inside our Present
  hook on the next frame), not from the UI callback.
- **Windowed vsync/limiter measurements** may show D3D8 already syncing in windowed mode; then "VSync" in
  borderless is informational only.
- **Frame-rate-dependent code** at >60 fps: gameplay measured fine at 180 (walk distance); the menu layer read
  and found time-based, its input sampling not - menus, popups and conversations now run at 60 (see "Menus at
  60 fps"). Still worth watching in the tour: camera lerps and particle spawn rates in play.
- **Font glyphs**: keep labels ASCII; "Hz" fine, avoid `×`.
- **The relocated table's address is an imm32 in game code**: the seven sites are verified by bytes that embed
  the absolute 0x6e9800, so a relocated (ASLR'd) or patched exe fails the check and keeps its own table (the list
  then trimmed to 20). The retail exe has no relocations and loads at 0x400000. The DLL's table is a static array,
  alive as long as the DLL (always, for a dinput.dll).
- **Test pipe and the panel**: not a risk after all - the panel's keys come from the DirectInput keyboard the
  pipe feeds (1.1, corrected), so `tap` drives it; the `wm` command added for it was removed.

---

## 6. In-game test checklist (final, after the review fix round)

Setup for the focus-free runs: the branch's `build/bin/Release/dinput.dll` next to `XMen2.exe` and

```ini
[Display]
Mode=windowed
RunInBackground=1
[Test]
InputPipe=1
```

(no FrameRate/VSync/ResolutionList). Keys go through the pipe with `tap` (DirectInput names: UP, DOWN, LEFT,
RIGHT, RETURN, ESCAPE; `tools/fixinput.py key down` etc.); `wm` is gone. Take a `screenshot` after each step
that moves the highlight. If a tap right after a screen opens seems ignored, tap again.

1. **Start, main menu** (~35 s): `status` ends with `fps ~60.0; frame rate the game's own 60 fps cap`. Log:
   `display: mode windowed`, `the game's window ignores the mouse while another window has the focus`,
   `resolution list: the game's own 20-slot table (no [Display] ResolutionList...)`, `Present hooked (the
   window's pause without the focus)`, `options: Advanced Options gets the rows ...`.
2. **To the panel.** Main menu (from `ui/menus/main.xmlb`: Begin Story (start) > Load Game > Danger Room >
   Review > Options > Play Online): `tap DOWN` x4, screenshot (OPTIONS lit), `tap RETURN`. The Options screen's
   help bar shows `<key> Advanced Options` (CMenuOptions sets it from `$MENU_OK`, and its per-frame handler
   0x5cbdf0 runs `openmenu sebas` = the panel on that action): screenshot, read the key, `tap` it. Log:
   `options: rows added to Advanced Options - Display mode Windowed, Frame rate 60, VSync Off, Run in background
   On (4 navigation records, 2 relinked)`.
3. **What the rows show** (under FSAA, same large font, values right-aligned ending where FSAA's does):
   `Display mode  Windowed`, `Frame rate  60`, `VSync  Off`, `Run in background  On`; status line (small font,
   y 272) empty. Frame rate choices on the 180 Hz desktop: 30 60 120 144 165 180 240 Refresh Unlimited.
   Keyboard order from Accept (the initial focus): UP = Run in background, VSync, Frame rate, Display mode, FSAA,
   Resolution; DOWN back. The highlight bar follows (0.05 s slide), the focused row turns white, hover sound.
4. **Frame rate live.** `tap UP` x3 (Frame rate lit), `tap RIGHT` x4 (60 > 120 > 144 > 165 > 180; click sound
   each), status line stays empty, `tap DOWN` x3 (Accept), `tap RETURN`. Then: `xml2-fix.ini` `[Display]` has
   `FrameRate=180` added, `Mode`/`RunInBackground`/comments unchanged; log `options: [Display] FrameRate=180
   written`, `frame rate: now 180 fps, paced by the fix from the next frame`; `status` `fps ~180; frame rate 180 fps`
   (within ~2).
5. **Back to stock.** Reopen the panel (rows now Windowed / 180 / Off / On), Frame rate `tap LEFT` x4 to 60,
   Accept. The ini's `FrameRate` line is **gone** (not `FrameRate=60`); log `FrameRate removed from xml2-fix.ini
   (the game's own behaviour)` and `frame rate: now the game's own 60 fps cap (its spin is back in charge; the
   fix doesn't pace)`; `status` `fps ~60`.
6. **VSync in a window.** Frame rate RIGHT to Unlimited, VSync RIGHT to On, Accept: ini `FrameRate=0`, `VSync=1`;
   `status` `fps ~180` (`180 fps (the desktop's refresh rate, for VSync in a window; FrameRate unlimited)`).
   VSync back to Off, Accept: `VSync` line removed, fps well above 180. Frame rate back to 60, Accept: removed, ~60.
7. **Display mode (restart).** Display mode `tap LEFT` (Windowed > Borderless): status line `Display mode applies
   after restart`. Accept: ini `Mode=borderless`. Restart: log `display: mode borderless`, a borderless window over
   the whole monitor at 2560x1440, still behind the owner's windows, `status` ~60 fps. Put it back (Windowed,
   Accept, restart). Picking Fullscreen removes `Mode` (the game's own fullscreen) - skip it while the owner
   works: it takes the screen.
8. **Cancel.** Change a row, `tap ESCAPE`: the game's discard warning appears (the rows set the panel's dirty
   flag); confirm it (keys if its buttons take them, else see 10): ini untouched, log `options: cancelled`.
9. **Mouse while unfocused** (owner, 10 s): move the mouse over the background game window with the panel
   open: no hover sound, highlight doesn't move; log once `mouse messages to the game's window are dropped`.
10. Mouse-only items (Revert to default, Back) and the look of the rows need the owner or a focused session:
   Revert > yes shows Fullscreen / 60 / Off / On and `Restart for the new mode`; Accept would then
   remove every key the panel owns (Mode too: fullscreen at the next start), so Cancel it in the harness.
11. Optional: `ResolutionList=all` > log `resolution list: ... replaced by one of 64 slots ... (7 references
   patched)` and, on opening the panel, `video options list for adapter 0: 24 sizes`; the Resolution slider walks
   them. Without the key the list is the pre-phase-3 20.
12. Default-off (a short fullscreen launch, only when the owner OKs it): no `[Display]`, no `[Test]`: log
   `display: as the game has it (no [Display] Mode in xml2-fix.ini); the Direct3D device is hooked for the
   in-game options` and `frame rate: the game's own 60 fps cap`, and **no** `Present hooked`, `resolution list:`
   or `video options list` lines; the Video list is the game's own.

---

### 6.1 Results, 2026-09-28 (harness on build/_subway, remote session: desktop 1920x1080 @ 32 Hz)

| step | result |
|---|---|
| 1 start | pass: every log line; `fps 59.9`, the game's own cap |
| 2 to the panel | pass, with a correction: Options (main menu) opens the plain Options screen; its help bar says `[Space] Advanced Options`, so `tap SPACE` opens the panel |
| 3 rows | pass: Windowed / 60 / Off / On under FSAA, values end where FSAA's does, status line empty, UP from Accept = Run in background, the bar follows |
| 4 frame rate live | pass: 60 > 120 > 144, Accept: `FrameRate=144` added, the rest kept; `fps 142.8` |
| 5 back to stock | pass: the key removed, `fps 59.9` |
| 6 VSync in a window | pass: choices 30 60 120 144 165 180 240 Refresh Unlimited (wraps; 32 joins them when the desktop runs at 32 Hz, by design); Unlimited + VSync On: `FrameRate=0`, `VSync=1`, `fps 31.9` (the 32 Hz desktop); VSync Off: key removed, `fps 600.8` |
| 7 display mode | pass: status line shown, `Mode=borderless` written; restart: borderless 1920x1080 window, shown without the focus, `fps 59.9`. **Fixed**: the status text ran past the pane (shortened, `status_max_chars`) |
| 8 cancel | pass: the game's "Do you want to Cancel?" dialog, Yes: `options: cancelled - xml2-fix.ini untouched` |
| 9, 10 mouse | not run (owner) |
| 11 ResolutionList=all | pass: `replaced by one of 64 slots ... (7 references patched)`; list `960x540 1280x720 1366x768 1440x810 1600x900 1920x1080` (the remote adapter reports one mode); the slider walks them |
| 12 default-off fullscreen | not run (takes the screen) |

Known cosmetic: toggle.png draws a small value box (two ticks) at the bar's right end, sized for FSAA's "4x";
the longer values (Windowed, Borderless, Refresh, Unlimited) start left of the first tick, so it crosses the
text on the focused row. Readable; scaling the bar can't fix it (the box scales with it).


## 7. Open questions for the owner - answered, see "Decisions" at the top

1. Default for `FrameRate` when the user never opens the panel: **stock 60, untouched.**
2. Display mode live switch (phase 4): **no; restart to apply**, with a status line.
3. Fifth row "Run in background": **yes.**
4. Windowed-mode VSync: **measured (1.6)** - D3D8 never syncs a window by default and its COPY_VSYNC is
   ~32 fps, so the row is a real toggle whose "On" paces at the desktop refresh rate.
5. HUD aspect correction at 21:9: **separate follow-up.**
6. Labels: **English first.**

---

## Appendix: research artifacts (`docs/research/`, git-ignored)

`XMen2.exe`, `libIGGfx.dll`, `libIGDisplay.dll`, `libIGCore.dll`, `alchemy.ini` copies; `petools.py`
(strings/xref/dis/func/fulldis/bytes/dwords); `xml2_text.asm` (full .text listing);
`adv_options_builder.asm` (0x61dc00-0x61f4ff); Ghidra project `ghidra/xml2opts` (all three binaries
analysed) + `ghidra_scripts/DecompAt.java`; decompiles `decomp_batch1.c` (builder, resolution, display init,
widget ctors), `decomp_batch2.c` (base ctor, layout, callbacks, registry), `decomp_batch3.c` (nav registry,
Accept, handlers, cycle widget, config system, timers), `decomp_batch4.c` (highlight, popups, CClient
frame/init, HUD), `decomp_batch5.c` (widget methods, window input, wndproc), `decomp_batch6.c`
(panel driver, timers), `decomp_batch7.c`-`decomp_batch11.c` (localisation, close/apply function
`FUN_0061f380`, window message dispatch, display getters, defaults), `decomp_gfx.c`/`decomp_gfx2.c`
(setDeviceParameters, createDevice, endDraw, resetDevice, getLastError, presentation interval),
`decomp_display.c` (igWin32Window setVideoMode, open, dispatchEvent, ...). Re-run any decompile with
`analyzeHeadless.bat <proj> xml2opts -process XMen2.exe -noanalysis -scriptPath ghidra_scripts
-postScript DecompAt.java out.c <hex addrs>` (JAVA_HOME = D:\tools\jdk-21.0.12.1+1).
