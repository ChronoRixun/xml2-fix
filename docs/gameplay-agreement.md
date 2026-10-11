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
| 3 | Master Mold controller (reserved for its owner) | owner supplies behavior revision and effective values, including verified off |
| 4 | Turret controller (reserved for its owner) | owner supplies behavior revision and effective values, including verified off |

IDs 5..65535 require coordination; 0 and duplicate IDs are refused. Entity layout revision describes token representation/sentinel/allocator semantics as well as capacities. Increment it whenever equal capacities/bit width can still encode or interpret tokens differently. A capacity raise must call the layout API with its successfully installed capacities and token width. Actor+generic must not exceed world (`<=`: the World keeps registrations beyond the two arenas; uint64 checked), and world must fit the token width. Entity-capacity layouts: revision 1 `{32,224,256,8}` (stock, also the verified capacity-off value), revision 3 `{32,768,1024,10}` (Phase A, contiguous tokens). Keep failed layouts unverified, even if the fallback is retail. The default is retail only when no entity-layout installer participates.

The export startup contributes the refusal and transport sites to the startup transaction, lets installers register effective state, then freezes the record at the very end. Add future installers before that finalization point. Registration after finalization is rejected and closes the gate; it never changes an advertised frozen record. No controller or capacity module edits transport code.

## Record and transport

Record maximum: eight entries (including layout), 172 bytes. Canonical little-endian header: DWORD `GCAP` (0x50414347), ushort protocol revision 2 (revision-1 peers, 0.2.0i and older, are refused), ushort entry count, ushort total bytes, ushort reserved zero. Each sorted entry is ushort rule ID, ushort revision, four DWORD effective values (20 bytes). All unused value words must have documented semantics, normally zero. No hashes or implicit enable flags substitute for values.

The entire record is appended to each original native body: request 0x18 (116 original bytes), host-info 0x1a (48), follow-up 0x1b (8), participant reply 0x1f (80). Original fields and LC24 stay intact. These opcodes use variable-length framing; reliable-envelope decoding normalizes body length before receiver dispatch. The largest augmented body is 288 bytes, within the native small-message queue limit. Wrappers keep copied header/body storage alive through synchronous native copies/broadcast iteration.

Receive rejects missing/unknown protocol, malformed bounds/count/order, unknown/missing/duplicate IDs, revision differences or any effective-value difference before native admission. Exact canonical equality is required, including disabled participating entries. An older single-boolean agreement record or an untagged older peer is refused.

## Receiver endpoint roles

| Opcode | Receiver | Native registration / setup | Refusal |
|---|---|---|---|
| 0x18 | Host/accepting endpoint | 0x6096a3 in 0x609660; joining sender 0x607cd0 | Disconnect refused peer and show dialog; preserve session |
| 0x1a | Host | 0x609cc3 in host setup 0x609f50 -> 0x609c80; joiner responds to 0x16 via 0x607770 | Disconnect refused peer and show dialog; preserve session |
| 0x1b | Joiner | 0x609dec in joiner setup 0x60a0a0 -> 0x609d80; host responds from 0x609970 | Native failure/cleanup and dialog |
| 0x1f | Joiner/receiving participant | 0x609ee2 in 0x609d80; host sender 0x608350 | Native failure/cleanup and dialog |
| 0x1d | Joiner | 0x609e3e in 0x609d80 | Native failure/cleanup and dialog, with no echo |

Host admission refusal never invokes shared-session cleanup, even if a timer exists. Joiner refusal invokes it even when its pending timer is absent. A timer is not an endpoint-role discriminator.

## Native refusal and failure handling

Patch acquisition records each distinct code page once; readback/cache verification and restoration preserve its original protection. Startup/socket exclusion remains closed until the entire record, transport and participating installer states verify.

Refusal immediately sends native incompatibility opcode 0x1d through the direct transport. Joining endpoints execute native failure handler 0x609ad0 and cleanup 0x6097f0, then the native dialog/menu path; active hosts show the incompatibility dialog and clear only the refused peer's native connected flag (0x6106f0), without destroying their existing participants. The early join entry rejects unverified local installation through the same native failure path, before setting up another attempt. A closed socket also presents the actionable native installation message. No generic timeout is used as the refusal mechanism. Native UI uses original authored text in this DLL; no localized game strings are shipped.

Game-free checks execute patched CALL/registration entries and retail failure/cleanup code loaded privately from a supplied executable, with transport/UI object boundaries isolated. Live direct-IP/LAN/lobby, reliable transport, cancellation/retry/rejoin and actual rendering remain integrator acceptance checks; automated native UI dispatch is not a rendered gameplay session.

## What protocol revision 2 does not include

The Phase A plan (docs/entity-capacity-phase-a-plan.md section 5) also designed a receive lifecycle: certificate
epochs per peer, a dispatch floor before every application callback, reliable/fragment record epochs, deferred
topology operations and a quiescent Node purge. Those 21 sites are not installed (plan section 10, deviation 17,
with the reasoning and the residual risk); the agreement is enforced at admission only.
