# Shared effective gameplay agreement

The release token (`[Online] GameVersion`, LC26 for builder 0.2.0) remains separate. LC builds and explicit ExplosiveFirstHit opt-ins require this protocol. Ordinary no-key XML2 without participating installers remains native. Compatibility uses actual installed values, never requested INI values alone.

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
| 9 | XML1 death threshold (`[Game] DeathRule=xml1`) | revision 1, `{installed ? 1 : 0,0,0,0}`; registered in an LC installation or when requested; requested but not installed is unverified ([details](death-threshold.md#multiplayer-agreement)) |
| 10 | The first game's Xtraction services and revival price | revision 2, `{services hook installed ? 1 : 0, service bits understood (0x3f: Forge, Healer, Load, Danger Room, Forge-by-campaign, no-Change-Team), online rule active (Load and campaign-gated options greyed, never removed, with other players) ? 1 : 0, RevivalCost=xml1 installed ? 1 : 0}`; `{0,0,0,0}` when neither is requested; a requested but failed install is unverified. Every peer builds the point menu from its own script run and the activator's input picks an index, and every peer debits the revival price, so both must be the same everywhere ([details](xtraction-shop.md#online-play)) |
| 11 | XML1 settle without crushes (`[Game] PlacementCrush=xml1`, [placement-crush.md](placement-crush.md)) | revision 1, `{installed ? 1 : 0,0,0,0}`; no key or `xml2`: verified `{0,0,0,0}`; `xml1` (or a value that is neither) not installed: unverified |
| 12 | XML1 packs without double heals (`[Game] DoubleHeal=0`, `src/double_heal_rules.hpp`) | revision 1, `{installed ? 1 : 0,0,0,0}`; no key or `1`: verified `{0,0,0,0}`; `0` not installed: unverified |
| 14 | Script function limit (`[Limits] ScriptFunctions`) | revision 1, `{installed capacity,0,0,0}`: `{0,0,0,0}` with no key and no `[Game] ForcedTeams` (plain XML2 peers match), `{320,0,0,0}` for the game's own limit, `{N,0,0,0}` for an installed raise; a requested raise that didn't install is unverified and closes host/join ([details](script-functions.md#online-and-saves)) |
| 13 | XML1 pack amount (`[Game] PackAmount=xml1`) | revision 1, `{installed ? 1 : 0,0,0,0}`; not requested (no key, or `xml2`): verified off; requested but not installed (guards, protection, readback, an unknown value): unverified ([details](pack-amount.md)) |
| 8 | XML1 solo mode | revision 1, `{installed ? 1 : 0, decision-rules revision (1), 0, 0}` with `[Game] SoloMode=1`; verified off `{0,0,0,0}` without the key; requested but not installed is unverified ([details](solo-mode.md)) |

IDs 5..65535 require coordination (the 0.2.2 wave: 5 skill rank costs, 6 multipart parts, 7 leader, 8 solo mode, 9 death threshold, 10 Xtraction, 11 PlacementCrush, 12 DoubleHeal, 13 reserved for pack amounts, 14 script functions); 0 and duplicate IDs are refused. Entity layout revision describes token representation/sentinel/allocator semantics as well as capacities. Increment it whenever equal capacities/bit width can still encode or interpret tokens differently. A capacity raise must call the layout API with its successfully installed capacities and token width. Actor+generic must not exceed world (`<=`: the World keeps registrations beyond the two arenas; uint64 checked), and world must fit the token width. Entity-capacity layouts: revision 1 `{32,224,256,8}` (stock, also the verified capacity-off value), revision 3 `{32,768,1024,10}` (Phase A, contiguous tokens). Keep failed layouts unverified, even if the fallback is retail. The default is retail only when no entity-layout installer participates.

The export startup contributes the refusal and transport sites to the startup transaction, lets installers register effective state, then freezes the record at the very end. Add future installers before that finalization point. Registration after finalization is rejected and closes the gate; it never changes an advertised frozen record. No controller or capacity module edits transport code.

## Record and transport

Record maximum: sixteen entries (including layout), 332 bytes (twelve assigned IDs, 252 bytes, fit with room to spare). Canonical little-endian header: DWORD `GCAP` (0x50414347), ushort protocol revision 2 (revision-1 peers, 0.2.0i and older, are refused), ushort entry count, ushort total bytes, ushort reserved zero. Each sorted entry is ushort rule ID, ushort revision, four DWORD effective values (20 bytes). All unused value words must have documented semantics, normally zero. No hashes or implicit enable flags substitute for values.

The entire record is appended to each original native body: request 0x18 (116 original bytes), host-info 0x1a (48), follow-up 0x1b (8), participant reply 0x1f (80). Original fields and the release token stay intact. These opcodes use variable-length framing; reliable-envelope decoding normalizes body length before receiver dispatch. The queued sends (host info 0x1a through 0x6077ad, participant reply 0x1f through 0x6085d8, both 0x610f30 mode 0) pass the native small-message check at 0x60f6ef only up to 300 bytes (0x12c); a longer body takes the native large-message path (opcode 0x12 with a 6-byte header carrying the original opcode and length, 0x60f7bb). The request 0x18 goes direct (0x60d9c0, a 0x514-byte buffer). Up to 8 entries (1.4.0, 172 bytes) every queued body stayed under 300: the 0x1f reply was at most 252 bytes. With the twelve assigned IDs (252-byte record) the 0x1f reply is 332 bytes and the 0x1a info exactly 300; at the 16-entry maximum 0x1f is 412, 0x1a 380 and 0x18 448. Whether the 0x12 path delivers a reassembled 0x1f / 0x1a body to the hooked receivers with its full length is unverified: a two-peer host and join with a record of 12 or more entries must pass before such a record ships. Wrappers keep copied header/body storage alive through synchronous native copies/broadcast iteration.

Receive rejects missing/unknown protocol, malformed bounds/count/order, unknown/missing/duplicate IDs, revision differences or any effective-value difference before native admission. Exact canonical equality is required, including disabled participating entries. An older single-boolean agreement record or an untagged older peer is refused.

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

Refusal immediately sends native incompatibility opcode 0x1d through the direct transport, now with an 8-byte reason body (`GREF`, reason, three zero bytes; reason relative to the sender: 1 gameplay settings, 2 the sender's own installation is unverified, 3 the peer's record is missing or foreign). Native 0x609ad0 ignores the body; a bodiless or foreign 0x1d reads as unspecified. Joining endpoints execute native failure handler 0x609ad0 and cleanup 0x6097f0, then the native dialog/menu path; a querying PC (0x18) shows the native dialog only. The early join entry rejects unverified local installation through the same native failure path, before setting up another attempt. A closed socket also presents the actionable native installation message. No generic timeout is used as the refusal mechanism. Native UI uses original authored text in this DLL; no localized game strings are shipped.

## Host refusal notice

The host is told on both join routes, without a popup, menu change or input change, and keeps its session and players: only the refused peer's native connected flag clears (0x6106f0), as native refusals do.

- **Where the reason comes from.** The host refuses a joiner's 0x1a itself (server-browser joins), or the joining PC refuses the host's 0x18 game info first and says why with 0x1d (Connect by IP, games-list search). The host never registered 0x1d natively, and its dispatch (0x60dbe0: a handler runs when `flags & event+0x18 == event+0x18`) drops a flags-0 handler's messages from a peer that has only queried, so the host-only receiver is registered with flags 3, like native 0x14/0x18/0x19. It is added and released exactly with the host's 0x1a registration by wrapping the two CALLs at 0x609cca and 0x6074fc, and stays out of a context whose joiner setup owns 0x1d (+0x22ec).
- **In the host lobby** (current menu a CMenuHost, vtable 0x69f274): the lobby draws its advice paragraph every frame from `ONLINE_ADVICE` (0x5babc0, CALL 0x629bf0 at 0x5bac28). That one call is wrapped: for 60 seconds after a refusal, or until the next admission (the 0x1b follow-up retail sends only once the participant is taken - a matching record that the game then refuses, full session or no participant, keeps the notice) or the host closes, the lobby draws the notice paragraph there instead (what happened, then what to do), then its advice again. It survives cursor moves and player-list refreshes because nothing but the draw reads it.
- **In play** (no lobby on screen): the HUD's timed message (HUD vt+0x30 = 0x59cdb0, kind 3, as script `hudMessage` uses) shows the one-line reason for 8 seconds. Other menus are never written to; the log always records the notice.
- **Words.** Gameplay settings differ / the fix did not load correctly on their PC or on this PC / another build. Texts are in `gameplay_agreement_rules.hpp` (`host_notice_text`, `host_notice_detail`).

A searching PC whose settings differ refuses a LAN host's 0x18 at search time as well, so a LAN host can see the notice before that player picks the game; over the internet that broadcast does not reach hosts. Both parts are optional and independent: the receiver (4 `receiver_guards`: the two wrapped CALLs and the dispatcher's register/release) and the presentation (7 `presentation_guards`: HUD, CMenuHost vtable and draw, the advice lookup). Without the presentation, refusals - the host's own and a joiner's received 0x1d - are logged with their reason; without the receiver, only the host's own refusals are. Each part joins the startup transaction as a contribution of its own (`gameplay_agreement.host_receiver`, `gameplay_agreement.host_notice`) only after its guards read back as retail, so a mismatch leaves that part out without refusing the agreement.

Game-free checks execute patched CALL/registration entries, retail failure/cleanup code and retail host setup/teardown (0x609c80 / 0x6074b0) loaded privately from a supplied executable, with transport/UI/dispatcher object boundaries isolated. Live direct-IP/LAN/lobby, reliable transport, cancellation/retry/rejoin and actual rendering remain integrator acceptance checks; automated native UI dispatch is not a rendered gameplay session.

## What protocol revision 2 does not include

The Phase A plan (docs/entity-capacity-phase-a-plan.md section 5) also designed a receive lifecycle: certificate
epochs per peer, a dispatch floor before every application callback, reliable/fragment record epochs, deferred
topology operations and a quiescent Node purge. Those 21 sites are not installed (plan section 10, deviation 17,
with the reasoning and the residual risk); the agreement is enforced at admission only.
