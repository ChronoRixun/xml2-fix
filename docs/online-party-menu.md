# Online Party screen: pause Party view and synchronized departure (0.6 round 8)

The in-session Party screen `lc_party` (a base `CMenu` the builder authors, 06-party), opened by the pause menu's renamed
Players row `lc_party_link` (`usecmd="openmenu lc_party"`). Presentation and intents only: the room, readiness, admission,
the lockstep transport and the native session stay the owners of their facts, and nothing a peer sends authorizes any
action here. Pure model: `src/online_menus_rules.hpp` (`party_model`, `party_roster`, the intents and the mailbox scope);
screen: `src/online_menus.cpp` (`party_frame`); in-session removal: `gns_transport::remove_member`; host fixtures:
`test/online_party_menu_fixture.inl` (modes `r8-party-host`, `r8-party-guest`, `r8-party-intents`). Probe string for the
builder: `lc-online-party/1` (`online_menus_rules::party_capability`, logged at install).

## 1. Opening, returning, the rail

- The pause menu (`PDA_MENU`) keeps its modes. LC renames the Players item `label_option07` (the native PDA intercepts that
  exact name at `0x5ccfa0`, independently of `usecmd`) to `lc_party_link`; the DLL needs nothing for the rename. Retail
  looks `label_option07` up by name and finds nothing, so the native Players mode can no longer start beside Party; the
  PDA's hardcoded mouse table skips missing items and the renamed row falls through to the generic command mouse
  (`0x5ae0a0` / `0x5bc1b0`), i.e. exactly one action per click.
- `openmenu lc_party` closes the PDA (a menu opened from another menu replaces it and records the return state), so the
  Party screen is the only ticking menu. It does not assume the PDA lives underneath.
- **Back** = the native base Back (`0x5ad7c0` -> `0x5b9fe0`), requested once from the screen's tick. While a confirmation is
  open Back only closes the confirmation. **Resume** (intent 63) is the same native Back. Open (live, unverified): whether
  the manager's return state lands on the PDA (one more Back resumes the game) or on the game; the procedure in section 7
  settles it.
- **Team / Options** are the rail items' own `usecmd`s, copied by the builder from the native PDA halves
  (`openmenu team`, `options`), so their authorization is the native one. **QUIT GAME** is NOT the native `mainmenuexit`
  (the native row has no confirmation of its own; the PDA does not intercept it): it is intent 66, the Leave confirmation,
  so a host always sees "End for everyone". Outside a native session (nothing to end for anyone) 66 runs the native quit at
  once, exactly what the pause menu's own Quit does. **Codex**
  is `runscript codexMenu(0)` (the script function XML1's console uses, opens the native codex menu; unverified from the pause).
  The Party row itself (current page) has no command. None of these reaches the DLL.

## 2. Intents (script `lcOnlineIntent(n)`, only on `lc_party`)

| n | action | DLL behavior |
|---|---|---|
| 63 | Resume | native Back |
| 64 | Invite / Copy code | clipboard copy of the room code (Y, Ctrl+C post the same); scene `lc_party_copied` / `lc_party_copy_failed` for 6 s |
| 65 | Propose load | refused (placeholder until round 14): the item is authored disabled and never enabled; `lc_party_propose_soon` (host) / `lc_party_propose_host_only` |
| 66 | Leave / rail Quit | opens a confirmation (none outside a session: the native quit runs at once) (`..._leave_host` says "End for everyone", `..._leave_guest`) |
| 67 | the one shared Yes | completes whichever confirmation is open (the single Yes item is reachable by pad/keyboard from Cancel in both). Leave: queues the native `mainmenuexit` through the console once (`0x55c410`, the route Quit uses); after it Back/Resume still reach the native Back and a 4 s timeout clears the latch with a log line, so the screen never freezes if the quit does not run. Kick: asks the transport once (same as 69) |
| 69 | Kick Yes | accepted but no longer authored (67 is the shared Yes); only with the Kick confirmation open |
| 71..74 | Kick card 1..4 | opens the Kick confirmation (host only, remote admitted card only) |
| 68, 70, >74 | reserved | refused everywhere (the authored-command queue boundary leaves them as plain native text) |
| 6 / 8 | Back / dismiss | close an open confirmation, else (6) native Back |

The mailbox takes exactly one action per tick, bound to the menu object that posted it (a stale entry never reaches a
re-opened menu), and the Party intents are refused on every other screen while the other screens' intents are refused here.

Focus: it is moved only after the frame's bindings (a hidden/disabled item cannot hold it). Opening a confirmation focuses
Cancel; every close (Cancel, Back, Yes, a stale or refused Yes, a dropped target, a refused Leave) returns to the item that
opened it when it is still usable, else Invite, else the current page.

## 3. Roster, hero, notices (data from `online_room::party`, nothing invented)

Cards are the native participants by session index (the host is 0), then broker-only pending members (`pending` scene, never
a Kick target), at most four. Facts: name (native, display allowlist), host/you, phase words and tone (`tone_good` in lobby /
ready / playing, `tone_warn` connecting, checking, waiting, receiving, loading, `tone_idle` otherwise), RTT with `to_host`,
route, build badge, `hero_fallback` ("Hero assigned at start") or `hero_next` ("Hero at the next area") for
waiting/receiving members. The notice bar shows the first non-local waiting (`lc_party_waiting`) or receiving
(`lc_party_receiving` + percentage, never produced by this build yet) member; its text nodes are hidden with the scene. There
is no AREA data (no source is wired); LC does not author an area line.

## 4. Kick: the agreed departure, not a pre-game leave

`gns_transport::remove_member(index)` (host only, a running zone, a remote participant with a bound live link; never index
0 or this PC) declares the participant's link gone through `declare_gone`, the one loss path every vanished participant
takes: the cut frame is then agreed on every PC (`vanished_links`, the lost-inputs/lost-ready exchange, the other guests end
their own link to it), and the removed PC's session ends as if its connection had been lost. It never calls the native
pre-game removal `0x6133a0` mid-frame. Departure once: the model marks the index until the roster stops listing it, the
transport finds no surviving link on a repeat, a refused request clears the mark so the host may retry.

What is not here (a new peer contract **needs a rule id**, none chosen): a "removed by the host" notice to the removed PC
(it sees a lost host connection and its native dialog), a Kick ban or in-session consent, a guest-side vote. Kick does
not prevent a re-join with the code (public admission stays open, 0.6).

## 5. Leave

Guest Leave and host Leave both end in the native quit route after an inline confirmation; the room/session end is the
round-6 lifecycle (`native_session_cleared`, the deferred room leave), not a new path. Host Leave ends the game for everyone
in 0.6 (party-continues and recovery are 0.7).

## 6. Authority and refusals (host fixtures)

Model (`party_model`) and screen tests cover: Kick only by the host in a running session, never the host/own/pending/empty
card, confirmation needed, a confirmation whose target index was reused or vanished is dropped (Yes then removes nobody), a
second Yes or a re-Kick before the roster moves does nothing, a refused transport request is retryable, Leave Yes without its
confirmation and after the first Yes does nothing, Propose and the reserved intents do nothing, everything but the
confirmation buttons is disabled while one is open, menu generation resets on a new menu object, Back closes a
confirmation before it leaves.

## 7. Live procedure (G/V, coordinator scheduled; nothing here was launched)

Three PCs (host A, guests B, C), candidate with this DLL and LC build with the party probe, `[Test] NoActivate=1`,
`_rules-2026-10-24.md` for every launch.

1. Pause in a running campaign on each PC, open Party from the pause menu with pad, keyboard and mouse. The Players row
   reads "Party", the native Players page never appears, one click = one action. Rail: Team opens the native team menu and
   Back returns; Options behaves as from the pause menu; QUIT GAME shows the Leave confirmation (host: "End for everyone") and
   only then runs the native quit; Codex opens the codex. Both confirmations (Leave and Kick) are completed by pad and by keyboard
   (Cancel -> left -> Confirm).
2. Back / Resume from Party: record whether the game resumes or the pause menu returns (then one more Back); neither may
   double-fire, lose focus or leave a menu without input.
3. While A sits in Party the match continues for B and C (footer text is true). Roster, phases, ping, invite code stay live.
4. Invite (button, Y, Ctrl+C) copies `XXXX-XXXX` (paste it elsewhere); clipboard failure shows the failed scene only.
5. Host Kick of B from Party: confirmation names B; Yes: on every PC B leaves at the same frame, no desync, no stuck wait;
   A's roster drops B once, C's too. Expect B to stay on A's roster as a pending "Connecting" card for about 10 s (until its watchdog) after the removal. Check the logs on all three PCs: each says "removed by the host" (A) / the agreed loss, then "agreed" and never "NOT AGREED", and there is no desync for at least 2 minutes. Repeat with a Kick issued just before an area exit. A second Yes, or Kick again before B vanishes, does nothing. B sees its native lost-host
   dialog and can join again with the code.
6. Guest Leave from C: confirmation, then the native quit; A and B continue, the roster agrees. Host Leave from A: the
   "End for everyone" confirmation; B and C get the native host-lost dialog; no room is left advertised.
7. A pending joiner D (Waiting for the next area) shows the notice bar and "Hero at the next area"; Kick is not offered
   for D's pending card.
8. Propose load: visibly unavailable, not focusable.

## 8. Open

Resume return target (above); the Codex route; no AREA line; Party row offline shows an empty roster; the removed PC gets
no dedicated notice (needs a peer contract and a rule id).

## Notes for the first co-op beta
A removed player gets no dedicated notice (no new peer contract, no new rule id): they see the native lost-connection dialog after about 10 s. Kick is not a ban.
