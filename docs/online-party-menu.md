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
their own link to it), and the removed PC ends its own session as a native Kick on the host's removal notice (below). It never calls the native
pre-game removal `0x6133a0` mid-frame. Departure once: the model marks the index until the roster stops listing it, the
transport finds no surviving link on a repeat, a refused request clears the mark so the host may retry.

**The removed PC is told (0.6 preview fix F, gameplay rule 64 OnlineRemovalNotice, `src/online_removal_core.hpp`).**
Before `declare_gone`, `remove_member` asks the room layer (`online_room::removal_start`) to send the member the removal
notice, link kind 9 on its room tunnel (unreliable, like kinds 7/8; handled by `online_room::poll` before the tunnel
policy). 35 bytes: `kind u8 = 9`, `wire u8 = 1`, `code[8]` (the room code), `host key[8]`, `target key[8]` (the removed
member), `index u8` (its native index, 1-3), `heard u32` (the newest Party report sequence, kind 7, the host accepted from
that member on this connection: `online_party_core::model::heard`), `serial u32` (the host's removal serial; resends repeat
it). The host sends the first one at once and repeats it every 250 ms **on the room connection it removed the member from**
until that connection closes (the member left the room after its session ended) or is replaced (a re-joined member on a new
connection is never sent the old Kick: review F finding 2). It gives up after 10 s of the member being responsive (a Party
report heard within 6 s; a member stalled in a zone load sends none, so its stall does not count; one host tick counts at
most 100 ms, so a host stall does not count either), or after 60 s in all, and then closes that connection itself with the
close text `removed by the host`. The removed PC reads that close text on its authenticated host connection as the same
removal (latch set, never a host loss), so even a member that heard no notice ends with "The host removed you from the
game.", never a silent solo session. A notice waits while the host has no report from the member on this connection (after
a reconnect). `remove_member` logs which of these applied (sent, already pending, no room, no connection, direct link).

The member accepts a notice only when all of these hold (`online_removal_core::receiver`, every refusal counted and logged,
none changes state): it arrived on the tunnel whose authenticated identity (the GNS certificate the project CA signed) is
the room's host, this PC does not host, and the host key is the room's (`not the host`); the room code is this PC's room
(`room`); the target is this PC (`target`); `heard` is above this PC's report sequence at the moment this connection to the
host came up and not above the newest report this PC sent (on any connection - the sequence is per process) (`stale` / `future`: an earlier connection's, an earlier session's or a
replayed notice, or one the host of this connection cannot have produced); this PC has not accepted one in this room yet
(`repeat`, the resends); this PC is a guest of a native session at that index (`session`). Accepted (once per room): the room
layer's host-loss tracker is told the coming connection end is not a host loss, the latch `online_room::removed_by_host()`
is set, and the next transport tick hands the session the retail removal notice **0x2b for this PC's own index, reason
0x10ce, flag 1** - exactly what the host's native lobby Kick sends - through the bound link to the host
(`gns_transport::deliver_removal`, after the receive validation of 0x2b; retried every tick while this PC is a guest of a
native session, a slow delivery logged once after 5 s; the host's repeats meanwhile are ignored, logged once). The retail handler 0x612f60 removes this PC as a
native Kick does and the 0.6 end path (fix E) shows the Hub notice **"The host removed you from the game."** instead of the
host-left panel; the session-end wrapper also accepts the latch (`broker_api::removed_by_host`) when the session ends
another way after the notice. The others keep playing on the agreed cut (unchanged). A peer without rule 64 is refused at
admission (rule agreement), so no session mixes the two behaviours.

Still not here: a Kick ban or in-session consent, a guest-side vote. Kick does not prevent a re-join with the code (public
admission stays open, 0.6). A re-joined PC's new connection has a new floor, so a notice of the old connection still on the
wire is `stale`, and the host never sends the old Kick on the new connection (it ends with the old one).

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
   A's roster drops B once, C's too. Expect B to leave A's roster within about a second (it ends its own session on the notice and leaves the room; it no longer waits out a watchdog). Check the logs on all three PCs: each says "removed by the host" (A) / the agreed loss, then "agreed" and never "NOT AGREED", and there is no desync for at least 2 minutes. Repeat with a Kick issued just before an area exit. Repeat with C on a slow or lossy link to A (B now leaves C within a round trip, possibly before the host's cut messages for B reach C: C must still say "agreed" for B and never desync), and with B in a zone load at the Kick (B ends with the notice after the load; A must not log "did not leave"). A second Yes, or Kick again before B vanishes, does nothing. B (fix F, rule 64) sees the native kick dialog within about
   a second, then the Hub with the red line "The host removed you from the game." and NO "THE HOST LEFT" panel; its log says
   "removal notice from the host ... accepted (rule 64" then "the host removed this PC from the game"; A's log "removal
   notice sent to ..." then "the removed member ... left". B can join again with the code.
6. Guest Leave from C: confirmation, then the native quit; A and B continue, the roster agrees. Host Leave from A: the
   "End for everyone" confirmation; B and C get the native host-lost dialog; no room is left advertised.
7. A pending joiner D (Waiting for the next area) shows the notice bar and "Hero at the next area"; Kick is not offered
   for D's pending card.
8. Propose load: visibly unavailable, not focusable.

## 8. Open

Resume return target (above); the Codex route; no AREA line; Party row offline shows an empty roster.

## Notes for the first co-op beta
A removed player sees the native kick dialog and then "The host removed you from the game." on the Hub (rule 64; every PC of the session needs this build). Kick is not a ban.


Party captures authored mouse actions before the native item walk, so informational text cannot cover Confirm or the Quit rail action. Mouse activations and keyboard/pad Back enter the same bounded, ordered queue. A successful Kick followed by Esc closes Party once; stale confirmation commands do not displace later input. Returning through the native menu history removes the current Party entry before Back.

The lobby and Party campaign descriptions use the host's native game-info settings. The saved-campaign flag is game-info `+0x3d` (session `+0x3e1`), set on the native load path at `0x6082e6`; a guest's local selected-save pointer is not a host setting. Party's `lc_campaign_summary` is a builder-authored DATA node.

Host setup visibility chips also own their mouse activation before the native walk: clicking Private or Public selects that chip immediately, and clicking the selected chip does nothing. Keyboard/pad activation retains its toggle behavior.
