# Shared effective gameplay agreement

The release token (`[Online] GameVersion`, LC26 for builder 0.2.0, LC27 for 0.2.2) remains separate. LC builds and explicit ExplosiveFirstHit or DeathSwitchClear opt-ins require this protocol. Ordinary no-key XML2 without participating installers remains native. Compatibility uses actual installed values, never requested INI values alone.

## Registration API

Include `gameplay_agreement.hpp`. Call each registration once, after its installer has checked guards, written/read back its changes and established its effective state. Disabled hooks register their native/off value with `verified=true`; a requested but failed hook registers `verified=false`. Registration automatically enlists socket exclusion and native refusal handling, including for a non-LC capacity/controller mod with no ExplosiveFirstHit key. A failed registration poisons the record and closes online admission. Do not silently omit failed or disabled participating rules.

```cpp
bool register_rule(uint16_t id, uint16_t revision,
                   std::array<uint32_t,4> effective, bool verified);
bool register_entity_layout(uint16_t revision, uint32_t actors,
                            uint32_t generic, uint32_t world,
                            uint32_t token_bits, bool verified);
// The refusal and transport sites are a CONTRIBUTION to the one startup transaction (patch_transaction): this
// writes nothing; the transaction applies, verifies and publishes it together with the other installers.
bool contribute(patch_transaction::transaction& startup, HMODULE game);
// Freezes the canonical record once, after every participating installer registered (exports.cpp, end of install).
bool commit_verified();
// A participant that registers outside the startup transaction (no LC, no capacity) installs the same sites in a
// transaction of its own, then the same freeze.
bool prepare(HMODULE game);
bool finalize(HMODULE game);
```

IDs:

| ID | Meaning | Revision / values |
|---|---|---|
| 1 | Entity allocation/token layout | revision 1 defaults to `{32,224,256,8}`; `register_entity_layout` replaces this default once |
| 2 | Explosive first-contact attack ID | revision 1, `{installed ? 1 : 0,0,0,0}` |
| 3 | XML1 Master Mold controller | revision 1, `{installed ? 1 : 0, first_bit, width, FNV-1a-32(flag name)}` of the applied saved-stage binding (`Xml1MoldStage=flag:first_bit:width`, exact case); verified off `{0,0,0,0}`; requested but not installed is unverified ([details](xml1-controllers.md#online-play)) |
| 4 | XML1 turret controller | not requested: revision 1 verified `{0,0,0,0}` (as 1.4.0); requested and installed: revision 2 `{1, placements, FNV-1a-32 of the applied [Xml1Turrets] lines, 0}`; requested but not installed: unverified ([XML1 controllers](xml1-controllers.md)) |
| 5 | XML1 skill and stat points: rank costs and level points (`[Game] RankCosts`, `LevelSkillPoints`, `LevelStatPoints`, [details](rank-costs.md)) | revision 3, `{parts (1 RankCosts, 2 LevelSkillPoints, 4 LevelStatPoints), 7 or 0, 5 or 0, 2 \| 8 << 8 (skill / stat bonus points)}`; verified off `{0,0,0,0}`; a requested part not installed is unverified |
| 6 | XML1 breakable parts (`[Game] Xml1Multipart`, [multipart.md](multipart.md)) | revision 1, `{installed ? 1 : 0, part-logic revision (2), 0, 0}`; verified off `{0,0,0,0}`; requested but not installed is unverified |
| 7 | XML1 stats leader bit (`[Game] X1Leader`, the build's `Data\x1leader.txt`) | revision 2, `{installed ? 1 : 0, installed ? 1 : 0 (the first game's drop rule for listed actors), 0, 0}`; registered in an LC installation; verified off `{0,0,0,0}` without the key; requested but not installed (guards, sidecar, bridges) is unverified |
| 8 | XML1 solo mode | revision 1, `{installed ? 1 : 0, decision-rules revision (1), 0, 0}` with `[Game] SoloMode=1`; verified off `{0,0,0,0}` without the key; requested but not installed is unverified ([details](solo-mode.md)) |
| 9 | XML1 death threshold (`[Game] DeathRule=xml1`) | revision 1, `{installed ? 1 : 0,0,0,0}`; registered in an LC installation or when requested; requested but not installed is unverified ([details](death-threshold.md#multiplayer-agreement)) |
| 10 | The first game's Xtraction services and revival price | revision 2, `{services hook installed ? 1 : 0, service bits understood (0x3f: Forge, Healer, Load, Danger Room, Forge-by-campaign, no-Change-Team), online rule active (Load and campaign-gated options greyed, never removed, with other players) ? 1 : 0, RevivalCost=xml1 installed ? 1 : 0}`; `{0,0,0,0}` when neither is requested; a requested but failed install is unverified. Every peer builds the point menu from its own script run and the activator's input picks an index, and every peer debits the revival price, so both must be the same everywhere ([details](xtraction-shop.md#online-play)) |
| 11 | XML1 settle without crushes (`[Game] PlacementCrush=xml1`, [placement-crush.md](placement-crush.md)) | revision 1, `{installed ? 1 : 0,0,0,0}`; no key or `xml2`: verified `{0,0,0,0}`; `xml1` (or a value that is neither) not installed: unverified |
| 12 | XML1 packs without double heals (`[Game] DoubleHeal=0`, `src/double_heal_rules.hpp`) | revision 1, `{installed ? 1 : 0,0,0,0}`; no key or `1`: verified `{0,0,0,0}`; `0` not installed: unverified |
| 13 | XML1 pack amount (`[Game] PackAmount=xml1`) | revision 1, `{installed ? 1 : 0,0,0,0}`; not requested (no key, or `xml2`): verified off; requested but not installed (guards, protection, readback, an unknown value): unverified ([details](pack-amount.md)) |
| 14 | Script function limit (`[Limits] ScriptFunctions`) | revision 1, `{installed capacity,0,0,0}`: `{0,0,0,0}` with no key and no `[Game] ForcedTeams` (plain XML2 peers match), `{320,0,0,0}` for the game's own limit, `{N,0,0,0}` for an installed raise; a requested raise that didn't install is unverified and closes host/join ([details](script-functions.md#online-and-saves)) |
| 15 | Death hand-over clears the successor's AI move intent (`[Game] DeathSwitchClear`) | revision 1, `{installed ? 1 : 0,0,0,0}`; absent/off is verified `{0,0,0,0}`; requested but not installed is unverified and closes host/join ([details](death-switch.md)) |

IDs 1-4 date from 1.4.0 and 5-15 from 1.5.0 (the 0.2.2 wave); IDs 16..65535 require coordination; 0 and duplicate IDs are refused. Entity layout revision describes token representation/sentinel/allocator semantics as well as capacities. Increment it whenever equal capacities/bit width can still encode or interpret tokens differently. A capacity raise must call the layout API with its successfully installed capacities and token width. Actor+generic must not exceed world (`<=`: the World keeps registrations beyond the two arenas; uint64 checked), and world must fit the token width. Entity-capacity layouts: revision 1 `{32,224,256,8}` (stock, also the verified capacity-off value), revision 3 `{32,768,1024,10}` (Phase A, contiguous tokens). Keep failed layouts unverified, even if the fallback is retail. The default is retail only when no entity-layout installer participates.

The export startup contributes the refusal and transport sites to the startup transaction, lets installers register effective state, then freezes the record at the very end. Add future installers before that finalization point. Registration after finalization is rejected and closes the gate; it never changes an advertised frozen record. No controller or capacity module edits transport code.

## Record and transport

Protocol revision 3 (1.5.0) puts a fixed 108-byte record on the wire, whatever the number of rules: canonical
little-endian header DWORD `GCAP` (0x50414347), ushort protocol revision 3, ushort rule count (including the layout
rule), ushort total bytes (108), ushort reserved zero; then the SHA-256 of the canonical rule list (32 bytes); then a
64-byte rule summary. The canonical list is `GCAP`, ushort protocol 3, ushort count, then every rule sorted by ID in
the revision-2 entry encoding: ushort rule ID, ushort revision, four DWORD effective values. All unused value words
must have documented semantics, normally zero; the digest covers every ID, revision and value exactly, so no hash of
a rule's own (a turret list's FNV, say) substitutes for an effective value - a rule still registers the values. The
summary's byte b is zero when no rule has (ID - 1) mod 64 = b, else a non-zero FNV-1a fold of those rules' entries; it
never decides admission (the digest does) and only names the differing rules in refusal messages (exact for IDs 1-64).
Registration holds up to 256 rules (the record is the same 108 bytes at 1, 16, 64 or 256). Each startup logs the
canonical list (`online: rule N (key) revision R {a,b,c,d}`), since the wire no longer carries the values.

The record is appended to each original native body: request 0x18 (116 original bytes, 224 with the record), host-info
0x1a (48, 156), follow-up 0x1b (8, 116), participant reply 0x1f (80, 188). Original fields and the release token stay
intact. These opcodes use variable-length framing; reliable-envelope decoding normalizes body length before receiver
dispatch. The queued sends (host info 0x1a through 0x6077ad, participant reply 0x1f through 0x6085d8, both 0x610f30
mode 0 -> 0x60f620) stay one native message only up to 300 bytes (0x12c, checked at 0x60f6ef); a longer body takes
the large-message path (0x60f7bb: opcode 0x12 with a 6-byte header - stream byte, original opcode, length - then 0x13
fragments). An admission body must never take that path: the fragment receiver 0x6101b0 (registered for 0x12 and
0x13 at 0x6105d4/0x6105ec) returns at once for 0x12, and for a 0x13 fragment looks its stream up at +0x30b0; for a
joining peer's body no stream is open there, and it dereferences null at 0x61020c (whether and where the stream is
opened for an admitted peer was not traced). Protocol revision 2 put the whole entry list on the wire (12 + 20 bytes per rule): with
the fifteen rules of the 1.5 pre-release the 0x1a body was 360 bytes and a matching peer's join crashed the host
there (live, 2026-10-12). The largest queued body of revision 3 is the 188-byte 0x1f, 112 bytes under the limit; a
`static_assert` keeps it at least 64 under. The request 0x18 goes direct (0x60d9c0, a 0x514-byte buffer).

**Fragment guard (optional).** The 0x13 stream miss at 0x610200 jumps to a stub that logs the drop (the first eight)
and leaves through the handler's own drop exit 0x6102b6 instead of dereferencing null, so an older or modified peer's
oversized message (a 1.5 pre-release joiner's 360-byte 0x1a) no longer kills this PC; that joiner gets no answer and
times out natively. It joins the startup transaction as its own contribution (`gameplay_agreement.fragment_guard`,
3 `fragment_guards`) only when its guards read back as retail; otherwise one log line and the stock handler.

Receive rejects a missing or foreign record and any other protocol revision (2: 1.4.0 and the 1.5 pre-release; 1:
0.2.0i and older - they refuse revision 3 the same way, both ways), a malformed header, a body that is not exactly 108
bytes, and any digest difference, before native admission: nothing of a refused message reaches the native handler.
Exact canonical equality is required, including disabled participating entries. A different count or a rule present
on one side only is reported as an entries difference, anything else as a value difference (a revision difference
included); both are "different gameplay settings". The refusing PC names the rules whose summary differs in its log
line (`differs: 9 DeathRule`), in its 0x1d (below) and, on a joining PC, in the native dialog ("Incompatible gameplay
settings: DeathRule. ...").

## Receiver endpoint roles

| Opcode | Receiver | Native registration / setup | Refusal |
|---|---|---|---|
| 0x18 | Querying PC (Connect by IP, games list) | 0x6096a3 in 0x609660; the host answers this PC's direct or LAN query with it; 0x6090d0 files it in the games table (0x608c20/0x608d60, status 6) | Not filed; native dialog on this PC; reasoned 0x1d to the host; no join cleanup (none is pending) |
| 0x1a | Host | 0x609cc3 in host setup 0x609f50 -> 0x609c80; joiner responds to 0x16 via 0x607770 | Reasoned 0x1d, disconnect refused peer, non-modal host notice; preserve session |
| 0x1b | Joiner | 0x609dec in joiner setup 0x60a0a0 -> 0x609d80; host responds from 0x609970 | Reasoned 0x1d, native failure/cleanup and dialog |
| 0x1f | Joiner/receiving participant | 0x609ee2 in 0x609d80; host sender 0x608350 | Reasoned 0x1d, native failure/cleanup and dialog |
| 0x1d | Joiner | 0x609e3e in 0x609d80 | Native failure/cleanup and dialog, with no echo |
| 0x1d | Host | added beside host-info: 0x609cca (registration) / 0x6074fc (release), flags 3 | Disconnect that peer, non-modal host notice; no echo, no cleanup |

The live wire order (two instances, OpenSpy rig) settled 0x18: the host sends it in answer to the 53-byte query a joining PC sends to an address (Connect by IP) or broadcasts (games list); a join from the master-server list skips it and goes 0x14 -> 0x16 -> 0x1a. Earlier static notes that listed 0x18 as host-received were wrong.

Host admission refusal never invokes shared-session cleanup, even if a timer exists. Joiner refusal invokes it even when its pending timer is absent. A timer is not an endpoint-role discriminator.

## Native refusal and failure handling

Patch acquisition records each distinct code page once; readback/cache verification and restoration preserve its original protection. Startup/socket exclusion remains closed until the entire record, transport and participating installer states verify.

Refusal immediately sends native incompatibility opcode 0x1d through the direct transport, with a reason body (`GREF`, reason, three zero bytes; reason relative to the sender: 1 gameplay settings, 2 the sender's own installation is unverified, 3 the peer's record is missing or foreign). Protocol 3 appends eight bytes: the differing rule-summary buckets (bit b = bucket b) of a settings refusal, zero otherwise; older readers check only the first eight bytes, and the receiving PC names those rules from its own list. Native 0x609ad0 ignores the body; a bodiless or foreign 0x1d reads as unspecified. Joining endpoints execute native failure handler 0x609ad0 and cleanup 0x6097f0, then the native dialog/menu path; a querying PC (0x18) shows the native dialog only. The early join entry rejects unverified local installation through the same native failure path, before setting up another attempt. A closed socket also presents the actionable native installation message. No generic timeout is used as the refusal mechanism. Native UI uses original authored text in this DLL; no localized game strings are shipped.

## Host refusal notice

The host is told on both join routes, without a popup, menu change or input change, and keeps its session and players: only the refused peer's native connected flag clears (0x6106f0), as native refusals do.

- **Where the reason comes from.** The host refuses a joiner's 0x1a itself (server-browser joins), or the joining PC refuses the host's 0x18 game info first and says why with 0x1d (Connect by IP, games-list search). The host never registered 0x1d natively, and its dispatch (0x60dbe0: a handler runs when `flags & event+0x18 == event+0x18`) drops a flags-0 handler's messages from a peer that has only queried, so the host-only receiver is registered with flags 3, like native 0x14/0x18/0x19. It is added and released exactly with the host's 0x1a registration by wrapping the two CALLs at 0x609cca and 0x6074fc, and stays out of a context whose joiner setup owns 0x1d (+0x22ec).
- **In the host lobby** (current menu a CMenuHost, vtable 0x69f274): the lobby draws its advice paragraph every frame from `ONLINE_ADVICE` (0x5babc0, CALL 0x629bf0 at 0x5bac28). That one call is wrapped: for 60 seconds after a refusal, or until the next admission (the 0x1b follow-up retail sends only once the participant is taken - a matching record that the game then refuses, full session or no participant, keeps the notice) or the host closes, the lobby draws the notice paragraph there instead (what happened, then what to do), then its advice again. It survives cursor moves and player-list refreshes because nothing but the draw reads it.
- **In play** (no lobby on screen): the HUD's timed message (HUD vt+0x30 = 0x59cdb0, kind 3, as script `hudMessage` uses) shows the one-line reason for 8 seconds. Other menus are never written to; the log always records the notice.
- **Words.** Gameplay settings differ / the fix did not load correctly on their PC or on this PC / another build. Texts are in `gameplay_agreement_rules.hpp` (`host_notice_text`, `host_notice_detail`).

A searching PC whose settings differ refuses a LAN host's 0x18 at search time as well, so a LAN host can see the notice before that player picks the game; over the internet that broadcast does not reach hosts. Both parts are optional and independent: the receiver (4 `receiver_guards`: the two wrapped CALLs and the dispatcher's register/release) and the presentation (7 `presentation_guards`: HUD, CMenuHost vtable and draw, the advice lookup). Without the presentation, refusals - the host's own and a joiner's received 0x1d - are logged with their reason; without the receiver, only the host's own refusals are. Each part joins the startup transaction as a contribution of its own (`gameplay_agreement.host_receiver`, `gameplay_agreement.host_notice`) only after its guards read back as retail, so a mismatch leaves that part out without refusing the agreement.

Game-free checks execute patched CALL/registration entries, retail failure/cleanup code and retail host setup/teardown (0x609c80 / 0x6074b0) loaded privately from a supplied executable, with transport/UI/dispatcher object boundaries isolated. Live direct-IP/LAN/lobby, reliable transport, cancellation/retry/rejoin and actual rendering remain integrator acceptance checks; automated native UI dispatch is not a rendered gameplay session.

## What the agreement protocol does not include

The Phase A plan (docs/entity-capacity-phase-a-plan.md section 5) also designed a receive lifecycle: certificate
epochs per peer, a dispatch floor before every application callback, reliable/fragment record epochs, deferred
topology operations and a quiescent Node purge. Those 21 sites are not installed (plan section 10, deviation 17,
with the reasoning and the residual risk); the agreement is enforced at admission only.
