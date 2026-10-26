# Shared effective gameplay agreement

The release token (`[Online] GameVersion`, LC26 for builder 0.2.0, LC27 for 0.2.2) remains separate. LC builds and explicit ExplosiveFirstHit or DeathSwitchClear opt-ins require this protocol. Ordinary no-key XML2 without participating installers remains native. Compatibility uses actual installed values, never requested INI values alone.

Protocol **4** adds the [native fresh-tail transfer contract](native-fresh-tail.md).
The agreement retains protocol 3's 108-byte framing, but rejects protocol 3 and
older before admission because those readers lose fresh appended residents.
No new gameplay rule ID is assigned for this transport change.

## Registration API

Include `gameplay_agreement.hpp`. Call each registration once, after its installer has checked guards, written/read back its changes and established its effective state. Disabled hooks register their native/off value with `verified=true`; a requested but failed hook registers `verified=false`. Registration automatically enlists socket exclusion and native refusal handling, including for a non-LC capacity/controller mod with no ExplosiveFirstHit key. A failed registration poisons the record and closes online admission. Do not silently omit failed participating rules. Requested-only rules 17..22 omit their entry when absent, empty or explicitly native, preserving the released OFF digest.

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
| 10 | The first game's Xtraction services and revival price | revision 2, `{services hook installed ? 1 : 0, service bits understood (0x3f: Forge, Healer, Load, Danger Room, Forge-by-campaign, no-Change-Team), online rule active (Load and campaign-gated options greyed, never removed, with other players) ? 1 : 0, RevivalCost=xml1 installed ? 1 : 0}`; `{0,0,0,0}` when neither is requested; a requested but failed install is unverified. Revision 3, same values, whenever `[Game] RevivalCost` is requested (1.8.0: the first game's group price 200 x G^2, G = min(5, highest roster level / 10 + 1)); revision 2 with `RevivalCost=xml1` (1.5.0 to 1.8.0-beta.7) charged 200 x level^2, and the two refuse each other naming `10 Xtraction services/RevivalCost`; without the key the entry stays the released revision 2. Every peer builds the point menu from its own script run and the activator's input picks an index, and every peer debits the revival price, so both must be the same everywhere ([details](xtraction-shop.md#online-play)) |
| 11 | XML1 settle without crushes (`[Game] PlacementCrush=xml1`, [placement-crush.md](placement-crush.md)) | revision 1, `{installed ? 1 : 0,0,0,0}`; no key or `xml2`: verified `{0,0,0,0}`; `xml1` (or a value that is neither) not installed: unverified |
| 12 | XML1 packs without double heals (`[Game] DoubleHeal=0`, `src/double_heal_rules.hpp`) | revision 1, `{installed ? 1 : 0,0,0,0}`; no key or `1`: verified `{0,0,0,0}`; `0` not installed: unverified |
| 13 | XML1 pack amount (`[Game] PackAmount=xml1`) | revision 1, `{installed ? 1 : 0,0,0,0}`; not requested (no key, or `xml2`): verified off; requested but not installed (guards, protection, readback, an unknown value): unverified ([details](pack-amount.md)) |
| 14 | Script function limit (`[Limits] ScriptFunctions`) | revision 1, `{installed capacity,0,0,0}`: `{0,0,0,0}` with no key and no `[Game] ForcedTeams` (plain XML2 peers match), `{320,0,0,0}` for the game's own limit, `{N,0,0,0}` for an installed raise; a requested raise that didn't install is unverified and closes host/join ([details](script-functions.md#online-and-saves)) |
| 15 | Death hand-over clears the successor's AI move intent (`[Game] DeathSwitchClear`) | revision 1, `{installed ? 1 : 0,0,0,0}`; absent/off is verified `{0,0,0,0}`; requested but not installed is unverified and closes host/join ([details](death-switch.md)) |
| 16 | XML1 power use, melee combos, hit reactions, AI visibility and the opt-in weapon brain (`[Game] NpcPowerRule=xml1`, optional `[Game] NpcSemiAuto=xml1` + `[NpcSemiAuto]` rows) | revision 14 when requested, `{installed ? 1 : 0, installed ? 1 : 0, weapon brain installed ? 1 : 0, weapon source fingerprint}` (words 2-3 zero without `NpcSemiAuto`; with it, word 3 is the first four bytes of the weapon table's SHA-256 and the whole digest is rule 16's detail, see "Rule details"); revision 4 added combo press/completion timing, revision 10 (1.8.0-beta.6, released) spends the combo steps, power re-checks and attack decisions that come due while a hit reaction holds the NPC and carries the officer weapon brain, revision 11 (1.8.0-beta.7) also runs the brain through locked moves (no attack-state input-lock exit), blocks only counted melee misses as XML1 (power attacks never rolled, no `block_chance` roll) and carries the schema 3 weapon brain (every gun family); revision 14 (beta 8) adds the picker / power-decision lock tests for every caller, XML1's Advanced Sentinel and Juggernaut classes, no Danger Room reaction gate (items 19-22) and the schema 4 trace contract with the XML1 character bullet ray; revision 11 (1.8.0-beta.7) and the unreleased candidates 12 (items 19-22 alone) and 13 (the bullet ray alone) are refused along with revision 10; revisions 5-9 were unreleased candidates (5 withdrawn role experiment / first reaction build, 6-9 officer weapon candidates) and are refused like 3 and 4; word 1 advertises XML1's visibility geometry and query scheduling; registered in an LC installation or when requested; absent/`xml2` retains released revision 3 with verified `{0,0,0,0}`; requested but not installed (guards, protection, readback, an unknown value, `NpcSemiAuto` without the power rule or valid rows) is unverified and closes host/join ([details](npc-power-rule.md), [weapon brain](npc-semi-auto.md)) |
| 18 | Original party-hero stun timing, reaction ownership and per-hit dispatch (`[Game] StunRule=xml1`) | revision 3, `{installed ? 1 : 0,0,0,0}`; registered only when requested; absent/`xml2` omits the entry to preserve the released OFF digest; requested but not installed is unverified ([details](stun-rule.md)) |
| 40 | XML1 boss brains (`[Game] Xml1Bosses=<list>`, [details](xml1-controllers.md#shared-boss-dispatcher)) | revision 1, `{installed boss bits (1 dopples, 2 avalanche, 4 pyro, 8 mystique, 16 shadowking, 32 native marrow, 64 magneto, 128 blob), FNV-1a-32 of "name:revision;" over the installed bosses, option bits (1 = the dopples' bossfull flag is written, `[Game] Xml1BossFull`, default 1), 0}`; Avalanche revision 2, others revision 1; registered only when the list names a boss (absent: no entry, the released OFF digest); a listed boss that is unknown or did not install is unverified and closes host/join |

IDs 1-4 date from 1.4.0 and 5-16 from 1.5.x (the 0.2.2 wave); 17 is ObjectWorldImpact; 18 is StunRule (both register only when requested). IDs 19 (DangerRoomParty) and 20 (Xml1LockOnAim) register only when requested, revision 1, values `{installed ? 1 : 0,0,0,0}`. Failed requests are unverified. IDs 21 (BoxHarmOrigin) and 22 (DangerRoomHealthBars) also register only when requested, revision 1, with verified installed values. The HUD rule covers unproved dirty-stat refresh/clamp timing parity. ID 23 (HarmSingleChain) registers only when requested, revision 1, verified installed values. Rule 24 (FastTransfer, `[Online] FastTransfer=1`, revision 1, values `{1,32,300,64}`: installed, window, record size, zero-run minimum) registers only when requested with verified installed values; the builder default is on. A mismatch is refused naming FastTransfer in both directions. IDs 25 and 26 are assigned to the 0.3.0 online work and are not registered by 1.7. Rule 27 (OnlineConversations, `[Game] OnlineConversations=lockstep`, revision 1, values `{1, AutoAdvance installed, ReplyVoices installed, 0}`) registers only when requested (an invalid value registers unverified); online, a marked conversation line advances after |timeDelay| s of game time on every peer and a chosen reply's voice is cut, because a line change draws from the shared random generator; the builder default is on. Rule 28 (ModelPools, `[Engine] ModelPools=x4` or `x2`, revision 1, values `{installed ? 1 : 0, model handles, IGB models, actors}` as installed, e.g. `{1,4096,2048,128}` for `x4`) registers only when a raise is requested; absent/`off` adds no entry (the released record); a requested raise that did not install, or an unknown value, registers unverified and closes host/join. A full model pool refuses spawns, so the pools are lockstep state; XML2 Fix 1.8.0-beta.1 (no rule 28) and an `x4` beta.2 peer refuse each other naming `28 ModelPools` on the beta.2 PC (beta.1 says "a rule this PC does not have"). Legends Classic 0.3.0-beta.2 writes `x4` ([model-pools.md](model-pools.md#co-op)). `[Game] ComboPopupGuard` (on by default since 1.8.0-beta.2) is display only - it hides a team-combo popup that lacks a head, the combo's XP and count unchanged - and registers no rule. Rule 29 (TickRate, `[Online] TickRate=60` or `30`, revision 1, values `{rate in effect,0,0,0}`) registers whenever the key is present; absent = the game's own 30 and no entry (the released record). Lockstep runs every participant's frames together (one simulation step, one input record per player, a checksum every 30 frames), so a 60 and a 30 PC refuse each other naming `29 TickRate` on both PCs; a PC without the key says `29 TickRate (disabled on this PC)`, and an XML2 Fix 1.8.0-beta.2 PC "a rule this PC does not have". A 60 request whose patch did not install advertises 30 (it plays with 30 PCs). Legends Classic 0.3.0-beta.3 writes `60` ([online-tick-rate.md](online-tick-rate.md)). Rule 30 (WorldBackfaces, `[Game] WorldBackfaces=xml1` with its required `[WorldBackfaces]` zone list, revision 2, values `{installed ? 1 : 0, 1 (scoped), zones listed, 0}` and the list's SHA-256 as the rule's detail, see "Rule details") registers only when requested; absent, empty, `xml2` or `off` adds no entry (the released record); an unknown value, a missing or malformed list or a failed install registers unverified and closes host/join. With it actor and entity movement sweeps meet only the level hull's front faces in the listed zones, as X-Men Legends I does, so where actors stop is lockstep state: an `xml1` PC and a PC without the key, or with another zone list, refuse each other naming `30 WorldBackfaces`; revision 1 (the unscoped private candidate 0.7.0a) is refused by revision ([world-backfaces.md](world-backfaces.md)). **Older DLLs name only the rules they know:** the refusal carries rule-summary bucket bits, never names, so a 1.8.0-beta.6 PC refused by a beta 7 PC over rule 30 says "a rule this PC does not have" (its log: `30 a rule this PC does not have`) while the beta 7 PC names `30 WorldBackfaces`; nothing a newer DLL sends can teach an older one a name (acceptable: the refusal itself is right, and the log shows the ID). The one-byte summary can also collide and omit a name while the digest still refuses. Rule 31 (IGBCache, `[Limits] IGBCache` 201..2048, revision 1, values `{installed ? 1 : 0, IGB records, resource names, 0}` as installed, e.g. `{1,1024,2048,0}`) registers only when a raise is requested; absent, `200` or an invalid value adds no entry (the released record); a requested raise that did not install registers unverified and closes host/join. A full cache refuses a zone's map IGB (the game reloads the previous zone) and every model past it, so the cache is lockstep state: a raised PC and a PC without the key refuse each other naming `31 IGBCache` ([igb-cache.md](igb-cache.md#online)). IDs 32..37 cover the independent resource limits described below; Rule 41 (ReceiveRounding/DamageCap, `[Balance] ReceiveRounding=xml1` / `DamageCap=xml1`, revision 1, values `{receive stages installed ? 1 : 0, cap installed ? 32000 : 0, 0, 0}`) registers only when either key is requested; absent/`xml2` adds no entry (the released record); an unknown value or a failed install registers unverified and closes host/join. Every peer computes every hit, so XML1's integer receive stages and 32,000 cap are lockstep state ([balance.md](balance.md#the-receive-side-and-the-cap-receiverounding-damagecap-gameplay-rule-41)). Rule 42 (shared handlers: `[Game] SkatingRule`, `TeleportLandingRule`, `PopupRule`, `PopupLandRule`, `ChargeRule`, `NightcrawlerRule`, `GambitRule` = `xml1`) is the requested XML1 shared-handler selection, revision 1, values `{installed_mask,0,0,0}` ([keys and tests](shared-handler-rules.md)); absence/native omits it and invalid/failed requests are unverified. Rule 38 (LiftRule, `[Game] LiftRule=xml1`, revision 2, values `{installed ? 1 : 0,0,0,0}`, candidate) registers only when requested; absent, empty, `xml2` or `off` adds no entry; an unknown value or a failed install registers unverified and closes host/join. With it, objects that carry `xml1heaviness` are lifted by X-Men Legends I's rule (Might rank 3 lifts the first game's heaviness 4), and revision 2 restores thrown impact, carried throw damage and telekinesis limits. Revision 2 refuses revision 1 in both directions, naming LiftRule; these decisions are lockstep state ([lift-rule.md](lift-rule.md)). Rule 39 (Xml1WallCling, `[Game] Xml1WallCling=xml1`, revision 1, values `{installed ? 1 : 0,0,0,0}`, candidate, not in a release yet) registers only when requested; absent, empty, `xml2` or `off` adds no entry (the released record); an unknown value or a failed install registers unverified and closes host/join. It gives the acrobat moveset's two wall-cling moves their X-Men Legends I handlers, so where a hero goes after a jump is lockstep state ([xml1-cling.md](xml1-cling.md)). ID 44 is ActorAI (`[Limits] ActorPools=124`, [actor-pools.md](actor-pools.md)), 45 Xml1GrabCarry and 46 SharedHandlersC (below); all three register only when requested. IDs 47..65535 require coordination; 0 and duplicate IDs are refused. Entity layout revision describes token representation/sentinel/allocator semantics as well as capacities. Increment it whenever equal capacities/bit width can still encode or interpret tokens differently. A capacity raise must call the layout API with its successfully installed capacities and token width. Actor+generic must not exceed world (`<=`: the World keeps registrations beyond the two arenas; uint64 checked), and world must fit the token width. Entity-capacity layouts: revision 1 `{32,224,256,8}` (stock, also the verified capacity-off value), revision 3 `{32,768,1024,10}` (Phase A, contiguous tokens). Keep failed layouts unverified, even if the fallback is retail. The default is retail only when no entity-layout installer participates.

The export startup contributes the refusal and transport sites to the startup transaction, lets installers register effective state, then freezes the record at the very end. Add future installers before that finalization point. Registration after finalization is rejected and closes the gate; it never changes an advertised frozen record. No controller or capacity module edits transport code.

## Record and transport

Protocol revision 4 retains the fixed 108-byte record introduced in revision 3 (1.5.0), whatever the number of rules: canonical
little-endian header DWORD `GCAP` (0x50414347), ushort protocol revision 4, ushort rule count (including the layout
rule), ushort total bytes (108), ushort reserved zero; then the SHA-256 of the canonical rule list (32 bytes); then a
64-byte rule summary. The canonical list is `GCAP`, ushort protocol 4, ushort count, then every rule sorted by ID in
the revision-2 entry encoding: ushort rule ID, ushort revision, four DWORD effective values. All unused value words
must have documented semantics, normally zero; the digest covers every ID, revision and value exactly, so no hash of
a rule's own (a turret list's FNV, say) substitutes for an effective value - a rule still registers the values. The
summary's byte b is zero when no rule has (ID - 1) mod 64 = b, else a non-zero FNV-1a fold of those rules' entries; it
never decides admission (the digest does) and only names the differing rules in refusal messages (exact for IDs 1-64). Past 64 a bucket
holds several IDs (b, b+64, ...); a refusal message then names every local rule of a differing bucket, including
ones whose own values are equal (e.g. rule 1 alongside a changed rule 65): read such a name as "possibly this one".
The 16 rules of 1.5.x share no bucket.
Registration holds up to 256 rules (the record is the same 108 bytes at 1, 16, 64 or 256). Each startup logs the
canonical list (`online: rule N (key) revision R {a,b,c,d}`), since the wire no longer carries the values.

**Rule details.** A rule whose effective state does not fit its four words registers a detail with it
(`register_rule(..., verified, &detail)`): the SHA-256 of its complete canonical data, defined by that rule's own
document. The four words stay registered as always (a detail never replaces them). When at least one rule has a
detail, the canonical list continues after the last entry with DWORD `GDTL` (0x4c544447), ushort detail count, then
every detail sorted by rule ID: ushort rule ID, the 32 digest bytes. A record without details hashes exactly the
protocol 4 list above, so every OFF and released digest is unchanged, and no protocol revision or wire change is
needed: the 108-byte record still carries only the digest, which now covers the details. A detail also joins its
rule's summary bucket, so a detail-only difference still names the rule (advisory). At most 16 details; more poison
the record. Each startup logs every detail (`online: rule N detail sha256 <64 hex>`). The only detail so far is rule
16's weapon table (1.8.0-beta.7, [npc-semi-auto.md](npc-semi-auto.md)): beta 7's first build had folded the rows into
a 32-bit FNV in word 3, which let two different tables agree.

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
there (live, 2026-10-12). The largest queued body of revisions 3 and 4 is the 188-byte 0x1f, 112 bytes under the limit; a
`static_assert` keeps it at least 64 under. The request 0x18 goes direct (0x60d9c0, a 0x514-byte buffer).

**Fragment guard (optional).** The 0x13 stream miss at 0x610200 jumps to a stub that logs the drop (the first eight)
and leaves through the handler's own drop exit 0x6102b6 instead of dereferencing null, so an older or modified peer's
oversized message (a 1.5 pre-release joiner's 360-byte 0x1a) no longer kills this PC; that joiner gets no answer and
times out natively. It joins the startup transaction as its own contribution (`gameplay_agreement.fragment_guard`,
3 `fragment_guards`) only when its guards read back as retail; otherwise one log line and the stock handler.

Receive rejects a missing or foreign record and any other protocol revision (3: 1.5.0 and early 1.6 candidates; 2: 1.4.0 and the 1.5 pre-release; 1:
0.2.0i and older - they refuse revision 4 the same way, both ways), a malformed header, a body that is not exactly 108
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


### 1.6 integration

Rule 17 is ObjectWorldImpact, revision 1, effective `{installed ? 1 : 0, 0, 0, 0}`; rule 18 is StunRule, revision 3. Both register only when requested. Rule 16 uses revision 14 when requested and released revision 3 when OFF. A failed request remains unverified. `[TestMode]` registers no gameplay rule and refuses online independently. The 108-byte record remains unchanged; protocol 4 adds the native fresh-tail contract above.


### Danger Room party and Sentinel aiming

`[Game] DangerRoomParty=xml1` restores XML1's four-hero fallback for incomplete
non-solo course parties. Rule 19 revision 1 uses `{installed ? 1 : 0,0,0,0}`.
Absent, empty or `xml2` omits the entry; any failed request remains unverified.

`[Game] Xml1LockOnAim=1` uses the animated head for lock-on targeting. Rule 20
revision 1 uses the same installed-state values. It verifies only after valid
effect paths, supported dependencies and all five transactional writes succeed.
Effects alone with aiming OFF omit rule 20; invalid aiming values are failed
requests. OFF bytes remain unchanged. Both peers name disabled known rules in
refusal messages. Protocol 4, 108-byte framing and durable saves are unchanged.

### Candidate 0.2.3e settings (rules 21-23)

| [Game] key | XML1 builder | Agreement |
|---|---|---|
| BoxHarmOrigin | xml1 | 21 revision 1, requested only |
| DangerRoomHealthBars | xml1 | 22 revision 1, requested only |
| HarmSingleChain | single | 23 revision 1, requested only |

Absent, empty, off or xml2 omits a rule. Invalid or failed requested installs remain unverified. Both peers must match. HUD dirty-stat refresh/clamp timing parity remains unproved. Versions and content identity remain unchanged from 0.2.3c.

`[Online] GameVersion` (the LC release token, `LC29`) is lobby metadata, not gameplay state: since candidate 0.2.3g it is also written into the game-info answer's first version field (0x61571c), which changes no gameplay-agreement rule, protocol or record byte.


Resource-loading limits also participate independently of ModelPools and IGBCache. Requested/effective
non-stock `[Limits] ResourceNames`, `ActorSlots`, `ItemEnhancements`, `FightStyles`, `EffectCurves`, and
`MoveTriggers` use named rules 32 through 37 respectively (revision 1, `{effective capacity,0,0,0}`).
A requested capacity that failed to install invalidates admission. Stock/absent limits contribute no
rule, preserving the released bytes. In particular ResourceNames=450 versus 2048 refuses by name even
when IGBCache is absent or 200 on both PCs. Rules 31 and 28 retain their existing encodings.

Rule 45 (`Xml1GrabCarry`, `[Game] Xml1GrabCarry=xml1`, candidate) is requested-only, revision 2, values
`{installed ? 1 : 0,7,0,0}`. Feature mask 7 includes ground grab (1), the pickup/recovery lifecycle (2), and
flying-charge collision (4). Carry/upkeep and flight admission remain deferred. The earlier ground-only
candidate (revision 1, mask 1) refuses this contract in both directions. OFF adds no entry; invalid settings and refused installs
are unverified. Both peers name rule 45 when the request differs. See [xml1-grab-carry.md](xml1-grab-carry.md).

Rule 43 (`Xml1RogueDecide`, `[Game] Xml1RogueDecide=xml1`, candidate) is requested-only, revision 1, values
`{installed ? 1 : 0,0,0,0}`. OFF adds no entry and preserves the released record; invalid or failed requests
are unverified and close host/join. Both enabled and disabled peers name the key in refusals. It restores
Rogue's victim/no-target power decision and source tag-1 energy dispatch; the companion builder must emit
the decide and drain nodes together ([xml1-rogue-decide.md](xml1-rogue-decide.md)).

Rule 44 (`ActorAI`, `[Limits] ActorPools=124`, E4 stage 1) is requested-only, revision 1, values
`{32,4,124 | 124<<16,1}` (actors/controllers 32, teams 4, path/shadow and delayed-damage pools 124, stage 1).
Absent adds no entry and writes nothing; any other value, or a refused install, is unverified and closes
host/join. Outside the game image (not at 0x400000) the key is ignored and adds no entry, as the `[Limits]`
capacities. See [actor-pools.md](actor-pools.md).

Rule 46 (SharedHandlersC), revision 1, is the independently selected wave-C mask `{installed_mask,0,0,0}`. It registers only when requested; missing/native keys preserve the OFF record, and invalid or failed requests are unverified. See [handler C keys and verification](shared-handler-c-rules.md). ID 46 is coordinator-assigned and does not replace rule 42 or rule 18.


Rule 45 carry extension: revision 3, values `{installed ? 1 : 0, 15, 0, 0}`,
requested only. Bit 8 adds flying carry, XML1 flight admission and upkeep to
revision 2's mask 7. Installation requires published AnimEnums and XML1 LiftRule;
a refusal is unverified and closes host/join. OFF still has no rule45 entry.
Revision 2 and revision 3 refuse each other. See `xml1-grab-carry.md` for the
required companion content switch (`XML1_FLIGHT_UPKEEP=True`).

Rule46 additionally assigns bit512 to `TelekinesisRule` and bit1024 to `GuardDecideRule`, each requested-only and dependent on installed rule45. Existing mask bits and revision1 are unchanged; new selections refuse older/native selections through their installed-mask values. See [TK/guard](tk-guard.md).

Rule 49 (`CharSounds`, `[Limits] CharSounds=127`) is revision 1, values `{127,0,0,0}` after its atomic install.
Absent or 16 adds no entry and writes nothing; other values and failed installs close host/join with an
unverified request. A mismatched peer is refused by name in both directions. See [char-sounds.md](char-sounds.md).

Rule 51 (`MotionPool`, `[Limits] MotionPool=22528`) is revision 1, `{22528,0,0,0}` after verified installation.
Absent/500 contributes no entry; invalid or refused requests close admission. It includes bounded owner clip
append/read behavior and retains native handle masking. See [motion-pool.md](motion-pool.md).

Rule 50 (`ResidentStats`, `[Limits] ResidentStats=320`) revision 1 advertises `{320,22,320,1}` after verified installation. Outer and NPC residency rise together; hero payloads stay22. Absent/31 leaves the agreement unchanged; a failed requested install closes online admission.
