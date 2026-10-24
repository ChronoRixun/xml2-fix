# Changelog

## 1.8.0 - <date TBD>

- **Online at 60 fps** (`[Online] TickRate = 60`; 1.8.0-beta.3): the stock game runs an online session at 30
  frames a second (`CClient::frame` writes a 1/30 s minimum frame while the online flag is set, 1/60 s otherwise).
  `60` replaces the online 1/30 with 1/60 (guarded, read back; `[Display] FrameRate`'s patch of the offline 1/60 in
  the same bytes accepts it). Lockstep runs one simulation step per frame with the slowest player's measured frame
  time as the step, so the game keeps its speed; upload per player roughly doubles (~11 KB/s). The rate is
  **gameplay-agreement rule 29 (TickRate)**, `{rate in effect, 0, 0, 0}`, registered whenever the key is there: a 60
  and a 30 PC refuse each other naming `29 TickRate`; a 60 request that cannot patch the game advertises 30. The
  lost-player records a survivor re-sends keep their length in seconds (64 at 30, 128 at 60). Absent = the game's
  30 and the released record; Legends Classic 0.3.0-beta.3 builds write `60` ([docs](docs/online-tick-rate.md)).
- **Bigger model pools, opt-in** (`[Engine] ModelPools = x4`; SPEC-runtime-limits 2.4): every model the game shows
  takes a handle and components from fixed pools (handles 692, IGB geometry 400, shader effects 432, animated actors
  32, particle clouds 40, ...). When one is full the model is silently not made - an effect, a prop or a HUD head goes
  missing (the beta 1 crash was the full IGB pool). `x4` moves the whole model aggregate (0x7bf6d0) into a block of
  the fix's own and raises handles and transforms to 4096, attributes 1024, shader effects 2048, IGB 2048, actors
  128, procedural geometry 1024 and particle clouds 128 (`x2`: half, clouds 80). Each particle cloud takes one of
  the particle systems of a second static manager (0x7dad78, 40 retail; with more clouds than systems the game reads
  through a NULL system at 0x578df3), so that manager moves and grows with the clouds; 753 fields of the model code are patched and
  36 imm8 sites and five bitmap clears move into caves, and the procedural-geometry destructor gets its own copy of
  the `bitset<220>::findNext` it shares with the FX templates (beta 3: under beta 2 that destructor read the helper's
  end value 220 as a live slot, and the attributes and procedural-geometry destructors cleared only the retail part of
  their bitmaps, so a zone change or exit with those pools in use could destroy items twice), all or nothing (on any byte that isn't the retail build's the game keeps its own
  pools and the log says so). A full pool refuses spawns as well as models, so online a raise is **gameplay-agreement
  rule 28 (ModelPools)**, `{installed, handles, IGB, actors}`, registered only when requested: peers must share the
  pools, and a 1.8.0-beta.1 peer and a beta.2 peer with `x4` refuse each other at admission (the beta.2 PC names
  `28 ModelPools`). Off by default in the DLL; Legends Classic 0.3.0-beta.2 builds write `x4`. The test pipe's status
  shows the nine live counts.
- **Quit no longer races the controller-enumeration workers** (retail `0x618c90`, manager `0xA6A7E4`): several
  enumeration workers can be alive after returning to the main menu, and each one cleared the shared manager and
  published "stopped" (`0xA6A7E1`) on exit while a peer was still enumerating - the peer then reloaded a null manager
  for its count call and faulted at `0x627375` (read of `0xC`), intermittently on Quit. Entry/exit accounting now lets
  only the last active worker clear the manager and acknowledge shutdown. Eleven short retail signatures guard the two
  sites; any mismatch logs its address and keeps the stock behaviour. See `docs/input-enumeration-shutdown.md`.
- **The team-combo popup no longer crashes the game** (owner report on 1.8.0-beta.1, Maintenance Garage hosting alone;
  `XMen2.exe+0x1a6848`): when two heroes land a combo the game shows both heroes' heads, and makes each head model
  from fixed model pools. With a pool full (the dump: 400 of 400 `CModelIGB`), the second head is never made, but
  the game still showed the popup and its draw called through the missing head. The popup is now hidden in that
  case, the way its own timeout hides it; the combo's XP and count are unchanged. Display only (no gameplay-agreement
  rule of its own). `[Game] ComboPopupGuard=0` keeps the game's own behaviour.
- **A frozen or silent player no longer splits the session** (0.3.0e, rule 25 revision 4): with three or more
  players, a player whose game froze (or whose network went silent) was removed by the host's own 10 s timeout
  without the survivors' agreed cut frame; the other joiner then waited for the host's part of the agreement, its
  own timeout ran out and it left ("You have lost connection to the host"). Now every way a player is lost - its
  connection closing, the host's timeout or lag-report removal, another player reporting it lost - takes the same
  path: every remaining PC exchanges the lost player's last inputs and removes it at one agreed frame, and the
  session carries on. The frozen player, when its game resumes, gets the normal "lost connection" message.
  0.3.0d peers are refused by name (`differs: 25`).
- **Hero switches survive level changes online** (0.3.0d, rule 26 revision 6): the solo-segment hand-back ran at
  every level change and gave each player the hero they had at the previous zone's start, undoing d-pad switches
  (and handing a dead hero back, which the game then passed on again). It now runs only inside a solo story segment
  and in the zone where it ends, with the heroes the players held when the segment started. 0.3.0c peers are refused
  by name (`differs: 26`) instead of desynchronizing.
- **A slow player is no longer dropped online** (rule 26 revision 6): the game's slow-player guard dropped whoever
  reported frames over 50 ms for 1,700 frames in a session - a joiner in the owner's three-PC test, the host itself
  on a busy PC (which ended the session for everybody). The warnings stay; the drop is gone. Switching onto a hero
  another player controls stays refused, as in the original game; switching to an AI hero works online.
- **Four-player Start Game no longer crashes:** each participant keeps its own mesh peer; the start race no longer rebinds two players to the same peer.

- **The release is built with online play:** the release workflow now builds GameNetworkingSockets 1.6.1 (pinned commit, the
  project CA and the cert-enforcement patch in `packaging/gns/`; protobuf and abseil from a pinned vcpkg) with
  `scripts/build-gns.ps1`, links it statically into `dinput.dll` and fails unless the DLL it publishes has GNS, protobuf and
  the CA pin in it and imports Windows system DLLs only (`scripts/check_release_dll.py`, run on the built DLL and on the one in
  the finished zip). Before, CI built without GNS: the same version number, no online rooms or drop-in. Nothing ships next to
  `dinput.dll`; the release zip now also carries `THIRD-PARTY-NOTICES.txt` (the licences of what is linked in).

- **Public game browser and join codes match builds by game version:** a room now carries the build's
  `[Online] GameVersion` (for example `LC33`) as its version token, exactly what the launcher's Browse games list
  compares with its own build; before, the token also held part of the content fingerprint and every listed game
  of the same build showed "Different game version" with Join off. A joiner of another build is still refused
  before it connects (`version_mismatch`).
- Rule 27 OnlineConversations (1.7.0) is the only conversation fix online; Legends Classic 0.3.0 writes it.
- **Online hardening** for room startup, message handling and logging.
- `online-status.json` (contract v1.5): a room that exists but takes no players right now (the broker's
  `room_not_joinable`) is the new error id `room_closed`; `room_full` now means only that all four places are taken.

- Online rooms for the launcher (contract v1.5): `[Online] Host` / `JoinCode` are read once at start and removed from `xml2-fix.ini`; `online-status.json` next to the game reports connecting / room / joined / in_game / ended / error (ids broker_unreachable, broker_rejected, code_not_found, code_expired, room_full, room_closed, version_mismatch, test_mode_on, tls_pin_mismatch, internal), written atomically; a room carries a version token so another build's joiner is refused before connecting; with a join code Join Game lists the room's game only - master server entries are left out until the session runs (no Connect by IP needed). [details](docs/online-status-file.md)
- Online rooms keep the broker's credentials between starts: the 24 h certificate (reused with more than an hour left) and the 6 h TURN relay credentials (reused with more than 30 minutes left, for the same broker and TURN secret; sealed with DPAPI for the Windows user because they hold the relay password, never logged). A connection that fails while cached TURN credentials are in use gets new ones from the broker once. `xml2-fix.log` says whether each came from the broker or this install's cache. A member's keepalive that overtakes its hello at the start is ignored instead of counted as a refused tunnel message, and refused tunnel messages are logged with the reason.

- **Solo story segments online** (`[Online] JoinInProgress=1`, rule 26 revision 6): a player without a hero in a
  segment stays connected and watches, with the message "Solo story segment - you rejoin when it ends"; when the
  party widens each player gets back the hero they had before (docs/join-in-progress.md).
- **Guest loss and freezes:** with three or more players, survivors exchange the lost player's last inputs
  and remove it at one agreed frame (rule 25 revision 4, framing 3). This includes a closed connection,
  the native host timeout and lag-report removal. A guest can join again by code at the next level boundary.
  A host drop or freeze still ends the session for joiners with "lost connection to the host"; no host migration.

- **Host alone and drop-in** (`[Online] JoinInProgress=1`): the host can Start Game with nobody else in the room and
  play; players are admitted mid-level and brought in at the next level change (rule-26 flow). When the last player
  leaves or vanishes the host keeps hosting (the native rule ended a session of two), so a replacement can drop in.
  The native slow-player counters start again with every restored session (a host that had played alone dropped
  itself right after the first bring-in).

## 1.7.1 - 2026-10-20

XML2 Fix 1.7.1 is the engine half of Legends Classic 0.2.5. Both co-op PCs need
the same version (gameplay rule 18 is now revision 3).

- **Bedroom item pictures draw in the first game's window:** with
  `[Game] PersonalItemPicture=window`, the personal-item menu draws its picture as
  a 256x128 window over the menu's panels (picture frame, text panel, Back bar),
  as the first game does, instead of stretching it over the whole screen. The
  fullscreen picture could only show an enlarged copy of itself behind the real
  one. Only PERSONAL_MENU's two position/size getters change (guarded, one log
  line on refusal); absent or `stock` keeps the fullscreen picture. Legends
  Classic enables it after a rebuild.
- **Heroes are re-stunned per hit, as in the first game** (issue #101): with
  `[Game] StunRule=xml1` (gameplay rule 18, now revision 3), every stun hit on
  a party hero requests the stun reaction. The original does this; X-Men
  Legends II only stunned on the hit that started its two-second stun effect,
  so hits during a stun, or right after getting up, were ignored. Both co-op
  PCs need the same revision. ([details](docs/stun-rule.md))

## 1.7.0 - 2026-10-20

XML2 Fix 1.7.0 is the engine half of Legends Classic 0.2.4.

- **Enemy resistance text is back:** with `[Game] ResistanceDisplay=xml1`, the
  target health bar shows the first game's physical, energy and mental labels
  and colors. Legends Classic enables this after a rebuild.
- This changes the display only. Combat resistances, saves and co-op rules stay
  the same. With the setting off or absent, the 1.6.0 display behavior remains.
  Ordinary X-Men Legends II installs keep the setting off.
- **Co-op no longer drops during conversations:** with
  `[Game] OnlineConversations=lockstep`, a conversation line that goes on by
  itself does so after the same game time on every PC, instead of when this
  PC's voice ends. The voice ending a moment apart on two PCs had put the games
  out of step and ended the session (for example during the mansion tour).
  Online, a chosen reply's voice is cut as the game has it; single-player is
  unchanged. Both PCs need XML2 Fix 1.7.0 and matching settings
  (gameplay agreement rule 27, revision 1);
  Legends Classic enables it after a rebuild.

## 1.6.0 - 2026-10-18

The engine half of Legends Classic 0.2.3: the first game's hazards, enemy combat timing and Danger Room, the Sentinel
flashback's effects, and a co-op that loads a saved campaign in seconds. For X-Men Legends II as it ships only the
network-message check and the plugin-dialog fix apply; everything else is off until a mod's `xml2-fix.ini` asks for it
(the port's builder sets it all).

**For everyone**

- **Malformed network messages are checked and dropped** (`[Online] ReceiveValidation`, on by default): the game
  checks the size and shape of every message it receives, per message type, before its handler runs, and drops one that
  doesn't fit instead of acting on it (a line in `xml2-fix.log`, rate-limited). Nothing changes on the wire, so it
  works with players who don't have it. Ordinary messages that carry extra bytes, and the game's own empty movie
  completion message, are not dropped.
- **No "Library appverifUI.dll could not be loaded" dialogs** when the game runs from `C:\`: the engine's plugin
  helper scanned the root of the current drive for plugins when no plugin folder is set, and loaded Windows' own
  leftover DLLs there. That one empty search now finds nothing; the game's real plugin folders are untouched.
- `[Debug] LoadLibraryTrace=1` lists the DLLs the game and its engine libraries load and the plugin-folder searches,
  for a bug report (off by default).

**New for mods (the port's builder sets all of these)**

- **Co-op *Load Saved Campaign* works and takes seconds.** The host's saved-campaign transfer could stall or end the
  session when the joiner's shared network message pools filled up. The receiving side now keeps room in all four
  pools, defers a datagram it can't store instead of acknowledging it, and ends a session that stays blocked for 30
  seconds with one clear log line. With `[Online] FastTransfer=1` (and `[Limits] EntityCapacity=1`) the 2 MiB campaign
  image is sent as data and zero-run records with up to 32 messages in flight and a larger rate budget: two real saves
  took about 3 and 1 seconds of transfer (15 and 5 seconds from click to playing) on a LAN, against about 160 seconds
  before. Both PCs must use it (rule **24**). ([details](docs/online-campaign-transfer.md)).
- **Connect by IP and the LAN search work for a mod's builds:** `[Online] GameVersion` now also reaches the answer a
  host gives to LAN searches and *Connect by IP*, so two builds with the same version list and admit each other that
  way (only the OpenSpy list worked before).
- **Hazards that hurt again:** `[Game] BoxHarmOrigin=xml1` (rule **21**) gives a boxed hazard the first game's
  knockback direction, and `[Game] HarmSingleChain=single` (rule **23**) keeps one repeat chain per touch fire, so a fire
  hits about once per 0.4 seconds and stops when you step out (before, re-entering one stacked up repeat events).
  ([box origin](docs/box-harm-origin.md), [single chain](docs/harm-single-chain.md)).
- **Crates that land on the floor:** `[Game] ObjectWorldImpact=xml1` (rule **17**): a landing with no physical target
  does no damage, as in the first game. ([details](docs/object-world-impact.md)).
- **Enemy AI revision 4 and party stun** (issue #101, progress): `[Game] NpcPowerRule=xml1` restores each enemy
  button's combo delay and clamps stale deadlines after a clock rewind or AI-slot reuse; `[Game] StunRule=xml1` (rule
  **18**, revision 2) gives party heroes the original stun playback speed and input recovery and discards a refused
  or interrupted stun. ([enemy AI](docs/npc-power-rule.md), [stun](docs/stun-rule.md)).
- **Danger Room:** `[Game] DangerRoomParty=xml1` (rule **19**) is the first game's four-hero fallback for a course
  that starts without a full party, and `[Game] DangerRoomHealthBars=xml1` (rule **22**) shows the health bar of a
  protected training robot; the protection and the course limits stay. ([details](docs/danger-room-courses.md)).
- **Sentinel flashback:** `[Game] Xml1LockOnEffects` restores the eye-scan, surface-hit and acquired-target effects and
  `[Game] Xml1LockOnAim=1` (rule **20**) aims by the animated head, as in the first game.
  ([details](docs/lockon-effects.md)).
- **Saved zones keep what they had:** an LC zone record can keep a verified identical prefix of its residents across a
  map change that only adds to the end, and the new ones start fresh; a missing, stale or ambiguous record is
  discarded as before. The save format is unchanged. ([details](docs/native-fresh-tail.md)).
- **Optional `[TestMode]`:** Forge access, F8 enemy clear, F9 party protection and F10 return to the nearest safe start
  for testers; both switches are off by default and refuse online hosting and joining.
  ([details](docs/test-mode.md)).

**Online**

- Rules **17** to **24** register only when a mod requests them; matching installs play together, and a missing,
  failed or different rule refuses the join with its name on both PCs.
- Gameplay agreement protocol 4 (the same 108-byte record): **1.5 and older peers are refused at join as another
  build, so both players need 1.6.0.**

<details><summary>Technical details</summary>

- `[Online] ReceiveValidation`: a guarded hook on the game's common message dispatch checks each message (from the
  datagram parser, the ordered drain and fragment reassembly, including reliable wrappers and padded records) against a
  per-type table of minimum and maximum sizes. Fixed-layout types whose handler ignores extra bytes accept any body
  from their minimum up to the game's 1285-byte cap; strict maxima stay only where a handler copies or indexes by
  length, and two types stay refused. The empty dynamically registered movie-completion notification is accepted. A
  drop line names the inner type of a wrapped message, its size, the violated bound and the first 16 bytes. A hook
  that can't verify its bytes leaves the game's own dispatch and says so in the log.
- The plugin-scan guard answers the plugin helper's one empty-repository search (`\*.*`, the root of the current
  drive) with "file not found" and logs one line; a pattern with a directory part is left alone. It is installed after
  the mod loader. `XML2FIX_PLUGIN_SCAN_GUARD=0` (environment) turns it off for development only.
- `[Online] GameVersion` also rewrites the game-info constructor's own version store (19 guarded bytes, in the same
  transaction, with readback); with the key unset the stock bytes stay. Lobby metadata only: no gameplay rule.
- Saved-campaign transfer: the receiver reserves room in the headers, large bodies, small bodies and queue nodes before
  it stores a datagram; an outgoing reply or fragment it can't fit is refused before anything is copied. A transient
  native marker carries the fresh tail of a retained zone record through the transfer; the durable LC format is
  unchanged.
- The authored-startup adapter `HarmLoopStartup` stays a developer option and still blocks online play.
  Disabled optional rules 17 to 22 are named in local refusal messages although they are absent from the OFF record.

</details>

## 1.5.0 - 2026-10-13

The engine half of Legends Classic 0.2.2: more of X-Men Legends I's own rules, each behind its own key. For X-Men
Legends II as it ships only the display, pad and cutscene-mouse fixes apply; everything else is off until a mod's
`xml2-fix.ini` asks for it (the port's builder sets it all).

**For everyone**

- **Exclusive fullscreen** asks only for a mode the adapter can show (a resolution it doesn't list falls back to the
  desktop's mode and refresh rate), *8x* anti-aliasing really asks for 8 samples, every mode lowers a request to the
  highest level the adapter supports, and a failed start no longer wipes the game's display settings in the fix's own
  modes.
- **Pads:** *Defaults 1/2/3* in the controls menu keep the pad layout; they used to erase player 1's pad bindings
  (#9).
- **The mouse in cutscenes** (`[Input] CutsceneMouse`, on by default) no longer outlines characters or clicks to move.

**New for mods (the port's builder sets all of these)**

- `[Game] NpcPowerRule=xml1`: **Enemy AI** (issue #101): enemies pay energy for powers, decide every 2 seconds, need at least half their energy, and use the first game's melee combos. Visibility uses the first game's geometry and query scheduling. Heroes still spend more time stunned than in the original; that part of #101 remains open. ([details](docs/npc-power-rule.md)).

- `[Game] RankCosts`, `LevelSkillPoints=xml1`, `LevelStatPoints=xml1`: X-Men Legends I's 2-point skill ranks and its
  2 skill / 8 stat points on every fifth level ([details](docs/rank-costs.md)). A campaign saved before these keys
  were installed gets the fifth-level points its heroes missed, once, on its first load.
- `[Game] Xml1Multipart`: breakable parts - Juggernaut's helmet, the Sentinels' arms ([details](docs/multipart.md)).
- `[Game] X1Leader`: the first game's leaders (their drop rule; the pain rule for their non-cold projectiles).
- `[Game] SoloMode`: the first game's solo mode at its gaps and bridges ([details](docs/solo-mode.md)).
- `[Game] DeathRule=xml1`: an entity dies on the hit that leaves it below 1 health
  ([details](docs/death-threshold.md)).
- `[Game] PackAmount=xml1`: health and energy packs restore a quarter, as in X-Men Legends I
  ([details](docs/pack-amount.md)).
- `[Game] PlacementCrush=xml1` and `DoubleHeal=0`: no crushes (and their hit sounds) while objects settle at a zone's
  load, no double heals ([details](docs/placement-crush.md)).
- `[Game] DangerRoomGate`, `RevivalCost=xml1` and the Xtraction services: Load, Danger Room, Healer and Forge at the
  first game's points, the main menu's Danger Room locked until Phoenix has joined, the first game's revival price
  ([details](docs/xtraction-shop.md)).
- `[Game] Xml1Turrets`: the first game's scan turrets scan, track and fire again
  ([details](docs/xml1-controllers.md)).
- `[Game] DeathSwitchClear`: when the controlled hero dies, the hero handed its controller stands still instead of
  finishing the walk its AI had started ([details](docs/death-switch.md)).
- `[Limits] ScriptFunctions`: the game's 320 script function names raised to 1024
  ([details](docs/script-functions.md)).

**Online**

- Each of these keys is a rule of the shared gameplay agreement (IDs 5-16). The agreement record is now a fixed
  108-byte digest (protocol revision 3), so a join no longer crashes the host once enough rules are on, and a refusal
  names the setting that differs on both PCs.
- A 1.4.0 player and a 1.5.0 player refuse each other at join, with the reason on both PCs: both need 1.5.0.

**Going back to 1.4.0**

- A save written by 1.5.0 is protected from 1.4.0: before replacing it, the older fix keeps it as `.lcsave.newer`,
  and when a different `.newer` copy is already there (after going back and forth) it refuses to save over that file,
  the profile included. To go back, put back the copy of your save folder made before the update.

<details><summary>Technical details</summary>

- `[Game] LevelSkillPoints` / `LevelStatPoints`: the catch-up grants each hero 1 skill and 4 stat points per multiple
  of 5 in (1, its processed level] and is marked by optional-feature bits 1 / 2 of the LC header, so it never happens
  twice; no format change. When the build ships `Data\lcsave\autogrowth-legacy.txt` (its heroes' autospend classes
  changed), the saved class bonus is re-based once the same way (old classes' growth out, current classes' in, comic
  rewards kept; header bit 3).
- `[Game] PlacementCrush=xml1`: an object being settled on the floor no longer crushes another, as in X-Men Legends I,
  which never settles anything. XMen2.exe settles every object at a zone's load and each object spawned in play 0.5 s
  after it appears; furniture the first game authored overlapping (the X-Mansion dormitories' desk chair under its
  desk) no longer plays two hit sounds from an empty room on entry. Other touches in play keep the game's rule.
- `[Game] DoubleHeal=0`: health and energy packs never heal double, as in X-Men Legends I, so they no longer sometimes
  play XML2's darker "double" sound; the stats screen's Body and Focus helps drop the double health / energy chance
  lines while it is in.
- `[Game] DeathRule=xml1`: X-Men Legends I's death threshold - an entity dies on the hit that leaves it below 1
  health, instead of only at 0, so an enemy no longer fights on at a fraction of a point with its death script and
  drops waiting. Eight comparisons of the damage functions move to the first game's line, and a killing hit leaves
  health at 0 as the first game's whole numbers always did, so XP and death drops follow.
- `[Game] Xml1Turrets` / `[Xml1Turrets]`: X-Men Legends I's scan turrets (the HAARP exterior missile tanks, the
  Arbiter / Weapon X tripod guns and the Hive tanks' flame turrets) with the first game's timing, aim spread, damage
  and effects; online peers must agree on every placement (rule 4).
- `[Limits] ScriptFunctions`: the game's 320 script function names, full with the fix's own functions, raised to
  1024 by default while `[Game] ForcedTeams` registers them (321 to 4096; 320 keeps the game's limit); every stock
  name keeps its place. `[Test] ScriptFunctionProbes` for testing it.
- Display: the exclusive-fullscreen mode, format, refresh and multisample choice is checked against the adapter's
  list; the 8x boot-switch index reads a private table; the two registry-wipe sites run only outside the fix's own
  modes; the guards are read-only pre-checks.
- Pads (#9): the *Defaults 1/2/3* preset tables get the pad layout; the game's cross-player rule (one key never drives
  two players) is kept.
- `[Input] CutsceneMouse`: the mouse check also tests the HUD's cinematic flag, so cutscenes that start without
  locking controls (X-Men Legends I's mission briefings) stop the mouse too.
- Online: the agreement's protocol revision 3 sends a header, the SHA-256 of the canonical rule list and a 64-byte
  rule summary (up to 256 rules, the same 108 bytes at any count); the largest queued body is 188 bytes, under the
  game's 300-byte message limit. With the full rule list on the wire (protocol 2) the join messages passed that
  limit, the game split them, and the host's large-message path dereferenced null; a fragment for a stream that was
  never opened is now dropped instead (an eight-byte guard in the startup transaction). Protocol-2 peers (1.4.0 and
  1.5 pre-releases) are refused as another build, both ways ([details](docs/gameplay-agreement.md)).
- `[Test] MultipartTrace`, `TurretTrace`, `SoloTrace`: diagnostic log lines for those features.

</details>

## 1.4.0 - 2026-10-12

The engine half of Legends Classic 0.2.0. For X-Men Legends II as it ships only the mouse fix applies; everything
else is off until a mod's `xml2-fix.ini` asks for it (the port's builder sets it all).

- The mouse reaches every menu row in a window Windows resized for display scaling, and a windowed game keeps its
  size when it changes monitors.
- `[Save] Format=lc`: the port's own save format; old saves converted once into `SaveLC`, the old folder untouched,
  `.bak` and `.newer` copies, refused saves keep the file on disk.
- `[Balance]`: X-Men Legends I's combat numbers, including whole-number damage rolls and melee-weapon bonuses.
- `[Limits] EntityCapacity=1`: 768 generic / 1024 World entities instead of 224 / 256, so crowded rooms keep every
  object; one startup warning, with advice that fits the cause, when it falls back to stock.
- `[Limits] MoveTriggers`: long attacks keep every sound and muzzle flash.
- `[Game] Xml1MasterMold` / `Xml1MoldStage`: X-Men Legends I's Master Mold fight, its stage kept in saves.
- `[Game] ExplosiveFirstHit`, `ClassicMainBackdrop`, `ClassicMainPanelBounds`, `ConfirmationRelease`,
  `XtractionShop`, `PersonalItemText` (on by default); late-act trivia kept; no crash on Quit with the classic backdrop.
- Online: games with these keys exchange their installed gameplay rules at join and refuse a mismatch, with the reason
  on both PCs; saving in co-op no longer stalls the game long enough to drop the connection.

### Details

- Restore opt-in XML1 Master Mold shield/core combat, original attack deadlines,
  platform actions, companion behavior and core tethers through native services.
  The builder supplies saved-stage metadata; disabled/refused installation retains
  the native encounter fallback; see `docs/xml1-controllers.md`.
- Register Master Mold (rule 3: installed, saved-stage bit range and a hash of the flag
  name) and the absent turret controller (rule 4, off) in the shared gameplay-capability record. Online co-op is lockstep, so every peer runs the
  controller; sessions use a peer-invariant hero instead of the local `_ACTIVE_HERO_`.
  Peers with different controller state are refused. Startup refusal names an
  unverified XML1 hook rollback instead of the network exclusion hook.
- Master Mold works with `[Limits] EntityCapacity=1`: its action-entity queries accept the installed query
  owner's entries, test busy queries by the owner's null result and bound results by the installed World.
- Added `[Limits] EntityCapacity` (port default 1, needs `[Save] Format=lc`): the entity domain grows from
  32 actor / 224 generic / 256 World handles to 32 / 768 / 1024 with contiguous ten-bit indices, so props,
  projectiles and triggers past the 224th are no longer refused without a word (docs/entity-capacity.md,
  docs/entity-capacity-phase-a-plan.md). Prepared owners (allocator backing, World, grid, query manager,
  physics frames, native save manager), the reviewed ledger of operand edits and relocations, the native
  save layout E10A2, generation retirement, refusal and reset-time telemetry, and the test pipe's
  `entity status` / `entity retire <slot>`.
- A requested `[Limits] EntityCapacity=1` that falls back to stock (a capacity-only mismatch) now tells the player
  once at startup - what failed and what it means (stock limits, raised saves not loadable, online closed) -
  instead of only logging it; the game then starts as before. The advice follows the cause: a missing
  `[Save] Format=lc` (the ini line, or `EntityCapacity=0`), an executable not at 0x400000 or with changed entity code
  (the unmodified retail `XMen2.exe`), owners already built (other loaders), or an internal failure (report it).
- The optional host refusal receiver and lobby notice join the startup transaction only when their own guards read
  back as retail and every byte they write lies inside those guards; the agreement test covers a receiver guard
  mismatch as well as a presentation one.
- One startup transaction (`patch_transaction`) now owns every code write of the shared gameplay
  agreement, the LC save seam, `[Limits] MoveTriggers` and the entity-capacity install: expectations are
  unioned and checked once, pages acquired once, writes verified by readback and rolled back on any
  failure; a rollback that cannot be certified refuses startup. The agreement's prepare/finalize became
  contributions plus a verified commit; the GCAP protocol advances to revision 2 and the entity layout rule
  to revision 3 for raised peers, so release-1.4 peers are refused at admission.
- LC saves carry required-feature bit 0 under the expanded layout; a retail-layout build refuses them with
  the reason, and a legacy save's twelve registered typed entity-id zone variables are migrated at the
  LC-to-native boundary when the build's typed registry verifies against the installed scripts.
- Both lcsave implementations take an explicit native layout descriptor (retail or E10A2) for every stream,
  record and transfer bound; outputs for the retail layout are byte-identical to before.
- Fix a crash on Quit with `ClassicMainBackdrop=1`: the Cerebro room's asset directory is no longer
  retained past the end of the main loop. The pin kept the room's environment-map shaders alive through
  igReleaseAlchemy, so libIGSg's exit-time destructor touched freed memory (libIGSg +0x6955e).
- Fix the mouse in the borderless and windowed modes when the window's client area isn't the
  game's screen size: the engine maps a mouse point to its menus against its resolution
  (0x5f9eb0: `[0xa09ffc]` x `[0xa0a000]`) while Direct3D stretches the picture over the client
  area, so a window Windows shrank on a DPI change (1280x720 dragged from a 125% monitor to a 100%
  one became 1023x575) hovered rows ever further below the cursor and never reached the last ones
  (the main menu's Quit, in-game menus alike). Mouse messages and ScreenToClient now carry the
  screen pixel the point is shown at, and a windowed window keeps its client size across a DPI
  change (WM_GETDPISCALEDSIZE / WM_DPICHANGED): the size it has, kept apart from the render
  resolution, so a window resized from outside keeps that size too. Test pipe
  `mouse X Y [move|down|up|click]`.
- `[Game] ClassicMainPanelBounds`: a move or press inside the focused main-menu row stays that
  row's. MAIN_MENU falls through to the generic handler there, whose label test (0x5c6490) sits
  half the label's local y offset higher and refocused the row below: the focus flipped on every
  move in a row's lower third, and a press there could move it before the release.
- Tell the host why a co-op join was refused, on server-browser and Connect-by-IP joins alike. The lobby
  draws the reason (different gameplay settings, the fix not loaded on one PC, another build) in place of its
  advice text for a minute; in play the HUD shows it briefly. No popup, menu or input change: the host keeps
  its session and players. Refusals now carry their reason in the native incompatibility message (0x1d);
  the host receives it even from a player who has only queried its address. Corrects 0x18 to the querying
  PC's game-info message (live wire order): an incompatible host is not listed and the host is told.
  The notice clears only on a confirmed admission (the game's 0x1b follow-up), and the receiver installs
  independently of the presentation, so a joiner's refusal is logged even where it cannot be drawn.

- Keep the LC startup veto on instruction-cache flush failures and every patch protection
  restoration failure, including preflight cleanup. Stop further writes on a failed flush
  and attempt all remaining protection restores before refusing startup.

- Refuse executable startup when required LC save/trivia hooks cannot be installed. Guard,
  allocation and patch-protection failures now reach the DLL startup veto with an explicit
  notice and log; background XInput initialization starts only after the required hooks verify.

- Persist late-act trivia with the builder's TRIV bind rule 2. Guarded native getter/setter hooks,
  capture and restore share compact bitmap positions while LC question identities stay unchanged.
  Legacy native imports still read their historical sparse positions; rule-1 builds keep that layout.
  Move per-act question cursors out of the native array whose late-act entries overlap answer bits;
  cold loads resume at the first unanswered identity without changing the LC format.

- Added `[Limits] MoveTriggers` (20..127, port default 64): preserves full gun bursts and authored model events
  through guarded handle side storage; native move layout and save data remain unchanged.
  Scope timing packets to enclosing invocations, guard the fixed native packet producer, retire timing
  cache entries with the actor arena and fall back to native nineteen on cache exhaustion.
  Share fallback timers during same-actor reentry so a trigger-started replacement move retains its future
  deadlines instead of dispatching stale timers; enclosing packet ownership stays separately scoped.

- Prepare and cache verified conversation identity before play so LC saves do not
  scan the conversation tree on the game/network thread. Changes during play
  refuse saving until restart; the save format and digest bytes are unchanged.
- Compose co-op provenance caching with conversation era maps: pin effective per-zone
  packages through the native mod lookup, including later-mod ranking winners.
  Watch package/order dependencies and refuse unsupported topology or lookup contexts.
  Capture and restore no longer read/decode packages on a gameplay frame.

- Correct native agreement receiver roles: rejecting client host-info packets preserves the host session;
  follow-up/rejection packets clean the joining endpoint independently of pending-timer state.

- Generalize online agreement to a finalized record of explicit effective gameplay values and entity/token
  layout, with a registration API for capacity and controller installers. Native refusal now presents
  an actionable message, sends the remote incompatibility response and clears pending joins immediately.
  Shared code pages restore their original protections ([API](docs/gameplay-agreement.md)).

- Bind the effective installed explosive first-hit rule to native host-info/join agreement.
  Refuse differing or unverified rules before admission without changing the release identity.
  Legacy peers without the tagged agreement are refused; ordinary XML2 without the opt-in stays native.

- Add opt-in `[Game] ExplosiveFirstHit=1`: explosions use the original game's fresh attack ID for
  the first-contact victim and native area victims. A guarded two-byte branch patch leaves stock
  settings, activation, damage records and saves unchanged ([details](docs/explosive-first-hit.md)).

- Add opt-in `ClassicMainBackdrop` to restore the original Cerebro room's diffuse textures.
  Guard engine exports and pass layouts; scope to known opaque main-room shaders, preserving
  reflection and logo lighting. Other rooms and unsupported layouts keep retail rendering.
- Require actual ownership by the room asset's loaded directory before correcting a shader;
  matching labels elsewhere no longer qualify. Log bridge allocation/protection failures.
- Add opt-in `ClassicMainPanelBounds`: full-width main-menu hit regions with disjoint row
  seams, preserving centered labels and the separate Online/Quit entries.

- Fix legacy imports whose equipped items were written in a different era from the campaign's talents.
  Translate hero and restore-bank equipment with the detected item era, including semantic dumps.
- Key talent-era evidence by actual stats index so restore-bank records belong to the correct hero.
  These corrections preserve the LC format and wire revision and match the Python reference.
- Keep the corpus loader/saver gate strict while recognizing documented legacy placement promotions.
  Expected ordinals come independently from unique names in the build's numbered placements;
  loader metadata is checked against build ordinals, names and placement indices. Synthetic negative
  controls reject wrong associations even when returned metadata agrees with the wrong saved states.
  All other semantic fields and retained payloads must still match.
