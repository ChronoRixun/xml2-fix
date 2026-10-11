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

## Native refusal and failure handling

Patch acquisition records each distinct code page once; readback/cache verification and restoration preserve its original protection. Startup/socket exclusion remains closed until the entire record, transport and participating installer states verify.

Refusal immediately sends native incompatibility opcode 0x1d through the direct transport. Joining endpoints execute native failure handler 0x609ad0 and cleanup 0x6097f0, then the native dialog/menu path; active hosts show the incompatibility dialog and clear only the refused peer's native connected flag (0x6106f0), without destroying their existing participants. The early join entry rejects unverified local installation through the same native failure path, before setting up another attempt. A closed socket also presents the actionable native installation message. No generic timeout is used as the refusal mechanism. Native UI uses original authored text in this DLL; no localized game strings are shipped.

Game-free checks execute patched CALL/registration entries and retail failure/cleanup code loaded privately from a supplied executable, with transport/UI object boundaries isolated. Live direct-IP/LAN/lobby, reliable transport, cancellation/retry/rejoin and actual rendering remain integrator acceptance checks; automated native UI dispatch is not a rendered gameplay session.
