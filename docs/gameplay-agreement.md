# Shared effective gameplay agreement

The release token (LC24 here) remains separate. LC builds and explicit ExplosiveFirstHit opt-ins require this protocol. Ordinary no-key XML2 without participating installers remains native. Compatibility uses actual installed values, never requested INI values alone.

## Registration API

Include `gameplay_agreement.hpp`. Call each registration once, after its installer has checked guards, written/read back its changes and established its effective state. Disabled hooks register their native/off value with `verified=true`; a requested but failed hook registers `verified=false`. Registration automatically enlists socket exclusion and native refusal handling, including for a non-LC capacity/controller mod with no ExplosiveFirstHit key. A failed registration poisons the record and closes online admission. Do not silently omit failed or disabled participating rules.

```cpp
bool register_rule(uint16_t id, uint16_t revision,
                   std::array<uint32_t,4> effective, bool verified);
bool register_entity_layout(uint16_t revision, uint32_t actors,
                            uint32_t generic, uint32_t world,
                            uint32_t token_bits, bool verified);
bool prepare(HMODULE game);   // early native refusal handling only
bool finalize(HMODULE game);  // called once AFTER ALL gameplay installers
```

IDs:

| ID | Meaning | Revision / values |
|---|---|---|
| 1 | Entity allocation/token layout | revision 1 defaults to `{32,224,256,8}`; `register_entity_layout` replaces this default once |
| 2 | Explosive first-contact attack ID | revision 1, `{installed ? 1 : 0,0,0,0}` |
| 3 | Master Mold controller (reserved for its owner) | owner supplies behavior revision and effective values, including verified off |
| 4 | Turret controller (reserved for its owner) | owner supplies behavior revision and effective values, including verified off |

IDs 5..65535 require coordination; 0 and duplicate IDs are refused. Entity layout revision describes token representation/sentinel/allocator semantics as well as capacities. Increment it whenever equal capacities/bit width can still encode or interpret tokens differently. A capacity raise must call the layout API with its successfully installed capacities and token width. Actor+generic must equal world, and world must fit the token width. Keep failed layouts unverified, even if the fallback is retail. The default is retail only when no entity-layout installer participates.

The export startup prepares refusal handling early, lets installers register effective state, then freezes the record at the very end. Add future installers before that finalization point. Registration after finalization is rejected and closes the gate; it never changes an advertised frozen record. No controller or capacity module edits transport code.

## Record and transport

Record maximum: eight entries (including layout), 172 bytes. Canonical little-endian header: DWORD `GCAP` (0x50414347), ushort protocol revision 1, ushort entry count, ushort total bytes, ushort reserved zero. Each sorted entry is ushort rule ID, ushort revision, four DWORD effective values (20 bytes). All unused value words must have documented semantics, normally zero. No hashes or implicit enable flags substitute for values.

The entire record is appended to each original native body: request 0x18 (116 original bytes), host-info 0x1a (48), follow-up 0x1b (8), participant reply 0x1f (80). Original fields and LC24 stay intact. These opcodes use variable-length framing; reliable-envelope decoding normalizes body length before receiver dispatch. The largest augmented body is 288 bytes, within the native small-message queue limit. Wrappers keep copied header/body storage alive through synchronous native copies/broadcast iteration.

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

A searching PC whose settings differ refuses a LAN host's 0x18 at search time as well, so a LAN host can see the notice before that player picks the game; over the internet that broadcast does not reach hosts. Both parts are optional and independent: the receiver (4 `receiver_guards`: the two wrapped CALLs and the dispatcher's register/release) and the presentation (7 `presentation_guards`: HUD, CMenuHost vtable and draw, the advice lookup). Without the presentation, refusals - the host's own and a joiner's received 0x1d - are logged with their reason; without the receiver, only the host's own refusals are.

Game-free checks execute patched CALL/registration entries, retail failure/cleanup code and retail host setup/teardown (0x609c80 / 0x6074b0) loaded privately from a supplied executable, with transport/UI/dispatcher object boundaries isolated. Live direct-IP/LAN/lobby, reliable transport, cancellation/retry/rejoin and actual rendering remain integrator acceptance checks; automated native UI dispatch is not a rendered gameplay session.
