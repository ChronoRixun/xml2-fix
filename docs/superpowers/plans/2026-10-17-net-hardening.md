# Co-op Network Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the D1–D12 defensive-review findings in the requested GNS/room paths and upgrade the DLL broker client to protocol 2 without changing offline behavior or the gameplay agreement.

**Architecture:** Keep game-independent policy in the existing core headers and exercise it in native tests. Add one focused Schannel wrapper for broker TLS, then integrate bounded queues, ordered room addressing, logging/key protections, and protocol-2 requests in the existing transport modules. No new third-party dependency and no VERSION, CONTENT_VERSION, rule ID, or agreement-revision change.

**Tech Stack:** C++20, Win32 x86, Schannel/SSPI, CryptoAPI, BCrypt, GameNetworkingSockets 1.6.1, CMake/MSVC, native C++ tests, Python gate scripts.

**Spec:** `<private task brief>`; inputs `<private defensive review>` and `<broker checkout>/SPEC.md` (version 2).

## Global Constraints

- Work only in `<working directory>` and the designated progress file; no push, PR, tag, release, or external service changes.
- Base is `feature/join-in-progress-2` at `9a467a9`; deliver on `feature/net-hardening` with commits ending `Co-Authored-By: Kimi K3 <noreply@moonshot.ai>`.
- No game launch is planned. If one becomes necessary, all slot, registry, path, and maintainer-safety rules apply first.
- Offline (`Host`/`JoinCode`/`Transport` absent) must retain the 108-byte protocol-4 capability record and 0.2.3f off-wire behavior.
- Protocol 4 and rule IDs 1–24 stay unchanged; this task adds no gameplay rule and does not change framing revisions.
- Use only the existing GNS package at `<GNS package>`; add no downloads or copied SDK code.
- D3 per-opcode rules remain unapplied unless `<validation report>` supplies them; only the validation hook is mandatory now.

## Review Focus

- A full queue must close only the offending tunnel/link and release every owned GNS message (Task 1 and Task 4 overflow tests).
- Member departure must not renumber survivors; a rejoin or later join must not collide with a live address (Task 2 roster tests).
- TLS pin verification must happen before any challenge/auth JSON is processed, and wrong/malformed pins must fail closed (Task 3 tests).
- A broker close/room_closed event must prevent new admission while leaving established P2P connections and native peer timeout semantics intact (Task 5 integration checks).
- Empty, non-printable, maximum-sized, and boundary-plus-one payloads must never reach native dispatch or unbounded logs (Tasks 1, 2, and 6 boundary tests).

---

### Task 1: Byte-bounded transport queues and receive-validation hook

**Files:**
- Modify: `src/gns_transport_core.hpp`, `src/gns_transport.cpp`
- Test: `test/gns_transport_test.cpp`

**Interfaces:**
- Produces: `max_game_body = 0x4e0`, `max_datagram_body = 1500`; `outbox::enqueue(...) -> bool`; `outbox::pending_bytes()`; `inbox(count, byte_cap)` and `inbox::bytes()`; `set_opcode_length_validator` / `validate_opcode_length` hook before dispatch.
- Consumes: Existing `message`, `parse`, `outbox::flush`, and `inbox::drain` ordering contracts.

- [ ] Add failing tests for 2 MiB outbound overflow (including coalesce accounting), 1 MiB inbound overflow and drain/clear recovery, game body 0x4e0 accepted versus 0x4e1 rejected, datagram 1500 accepted versus 1501 rejected, and validator rejection before queueing.
- [ ] Build and run `xml2_gns_transport_test` to confirm the new assertions fail on the base implementation.
- [ ] Implement count+byte accounting and return false without partial queue mutation on overflow; make callers close the overflowing link. Enforce framing size limits and invoke the opcode+length hook before `inbox::push`.
- [ ] Re-run the test and confirm existing ordering, coalescing, deferral, backlog, and retail-guard checks still pass.
- [ ] Commit the bounded transport core and tests with the required trailer.

### Task 2: Ordered room policy, datagram safety, and protocol-2 core messages

**Files:**
- Modify: `src/online_room_core.hpp`
- Test: `test/online_room_test.cpp`

**Interfaces:**
- Produces: ordered `room_roster` with stable slot addresses (`host = 10.77.0.1`); `token_bucket`; bounded `broker_line_buffer` (16384 bytes including LF); fixed-size printable field logging; sender-address rewrite at datagram offset +13; v2 `auth_message`; `room_metadata` and protocol-2 host request generation.
- Consumes: Existing JSON parser/quote helpers and 16-hex identity format required by the broker.

- [ ] Add failing tests for host-first/stable/non-key-derived addresses, departures without renumbering, duplicate membership, token-bucket packet/byte limits, line lengths 16384/16385, bounded non-NUL/non-printable version fields, +13 address/port confinement, `LCB1-auth-v2\0`, and metadata JSON with server-derived `players` omitted.
- [ ] Run `xml2_online_room_test` and confirm the new assertions fail on the base.
- [ ] Implement the policy helpers with a vector-backed roster (not `std::set`) because broker join order determines stable addresses; preserve tombstoned slot assignments for the room lifetime.
- [ ] Re-run the test and verify existing base64, JSON, certificate, ICE, and RFC 8032 checks pass.
- [ ] Commit the room policy and tests with the required trailer.

### Task 3: Schannel protocol-2 transport and certificate trust tests

**Files:**
- Create: `src/broker_tls.hpp`, `src/broker_tls.cpp`, `test/broker_tls_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `broker_tls::client::handshake(SOCKET, hostname, spki_pin, error)`, `encrypt`, and incremental `decrypt`; SPKI SHA-256 extraction/pin comparison; public-CA + DNS hostname verification when no pin is configured.
- Consumes: An already-connected TCP socket and `[Online] BrokerPin` normalized to exactly 64 lowercase hex characters.

- [ ] Add a native test with an owned test certificate for exact SPKI-DER pin matching, wrong-pin rejection, malformed-pin rejection, and TLS version flags restricted to TLS 1.2+.
- [ ] Build and run `xml2_broker_tls_test` to establish the failing baseline.
- [ ] Implement the Schannel client: manual certificate validation, public-CA/hostname policy in unpinned mode, leaf SPKI pin in pinned mode, stream encryption, and buffered decryption with incomplete-record handling. Free certificate/context/output buffers on every path.
- [ ] Link only Windows system libraries (`secur32`, `crypt32`, `bcrypt`, `ws2_32`) and rerun the native test.
- [ ] Commit the TLS wrapper and tests with the required trailer.

### Task 4: Integrate tunnel/link caps, listener policy, dispatch, and rate limits

**Files:**
- Modify: `src/online_room.cpp`, `src/online_room.hpp`, `src/gns_transport.cpp`
- Test: `test/gns_transport_test.cpp`, `test/online_room_test.cpp`

**Interfaces:**
- Consumes: Task 1 queue/validation APIs and Task 2 roster/bucket/datagram helpers.
- Produces: `online_room::set_transport_bound(connection, bool)` so unbound tunnels retain only hello/hello_ack control messages.

- [ ] Add/extend regression checks for queue-admission policy: unbound game/keepalive/garbage dropped, handshake retained, and per-sender rate/size rejection before arrivals grow.
- [ ] Cap tunnel game bytes at 1 MiB, global arrivals at 256 KiB, per-link inbox at 1 MiB, strays at eight with one per remote IP, and per-tunnel reads at 64 messages per poll; close the offending tunnel/link and release queued messages on cap overflow.
- [ ] Rewrite received 0x18 +13 address/port to the sender tunnel and apply 200 datagrams/s plus 64 KiB/s token buckets.
- [ ] Disable the IP listener only in requested room mode while keeping P2P room ticking; cap strays in LAN mode. Add outbound overflow close and rate-limit every rare-opcode trace call site.
- [ ] Build and run both transport/room tests plus retail guard tests; verify no agreement values changed.
- [ ] Commit integration with the required trailer.

### Task 5: Protocol-2 broker session, room lifetime, and metadata integration

**Files:**
- Modify: `src/online_room.cpp`, `src/online_room.hpp`, `docs/online-rooms.md`
- Test: `test/online_room_test.cpp`, optional local protocol-2 smoke evidence under `evidence/`

**Interfaces:**
- Consumes: `broker_tls::client`, v2 auth/domain, line buffer, metadata request, roster, and signal bucket.
- Configures: `[Online] BrokerPin=<64 hex>` optional; unpinned means system-CA and hostname verification. Optional host metadata comes from documented `[Online] Room*` keys.

- [ ] Replace plaintext broker I/O with Schannel and process challenge `v:2` only after trust verification; reject other versions and malformed challenges. Bound decrypted lines and broker inbox/outbox bytes.
- [ ] Send `cert`, `turn`, and `host`/`resolve` after `auth_ok`; include known optional name/mode/zone/joinable/in-progress/version-token/max_players metadata and never send read-only `players`.
- [ ] Apply ordered host/resolve/peer_joined membership, 8192-byte decoded signal caps, 20 signals/s per member, and duplicate-live-connect-request refusal.
- [ ] On `room_closed` or broker loss, mark the room/admission unavailable without dropping established tunnels; normal `peer_left` retains its existing teardown.
- [ ] Build and run room/TLS tests. If a local exported broker can be started without external changes, run a pinned TLS + wrong-pin + v2 challenge smoke and retain sanitized logs; otherwise record it as unverified.
- [ ] Commit protocol-2 integration and documentation with the required trailer.

### Task 6: Key storage, release-only logging, and secret hygiene

**Files:**
- Create: `src/online_security.hpp`, `test/online_security_test.cpp`
- Modify: `src/online_room.cpp`, `src/gns_transport.cpp`, `src/net_impair.cpp`, `src/log.cpp`, `src/log.hpp`, `CMakeLists.txt`, `docs/online-rooms.md`, `docs/gns-transport.md`
- Test: `test/online_security_test.cpp`, `test/log_test.cpp`

**Interfaces:**
- Produces: atomic replace-file helper, explicit current-user DPAPI flags, secure certificate-blob erase, rate-limited logger API, one-generation 8 MiB log rotation.

- [ ] Add tests for atomic replacement/no temporary-file residue, current-user rather than local-machine DPAPI flags, secure erase, deterministic rate-gate behavior, and rotation at the 8 MiB boundary.
- [ ] Implement atomic unique temporary files with `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` for key/public files; refuse unreadable existing keys rather than silently regenerating; zero the GNS certificate blob after `SetCertificate`.
- [ ] Compile `OnlineCert`, `OnlineCertFile`, `OnlineIdentity`, GNS fake-network keys, and NetFake hooks out of Release (`NDEBUG`) builds. Redact join codes, strip broker-controlled control characters, bound the 0x18 version log field, and remove full key paths from logs.
- [ ] Add rate-limited rare/GNS debug logging and rotate `FIX_LOG_FILE` once at 8 MiB, bounding retained disk use.
- [ ] Build Release and run security/log tests; inspect that Release objects contain none of the removed dev-option strings and logs contain no full join code.
- [ ] Commit hardening and tests with the required trailer.

### Task 7: Full gates, OFF compatibility, review, and report

**Files:**
- Create: `scripts/run_native.py`, `scripts/run_startup.py`, `scripts/prove_off_wire.py` adapted only to this folder (or equivalent commands recorded in the report)
- Create: `<working directory>/REPORT.md`
- Modify: only defects found by gates/review

- [ ] Configure Win32 Release with `XML2_FIX_GNS_ROOT=<GNS package>`, build all targets, and run the complete native suite with the retail executable read-only.
- [ ] Run the isolated startup suite with registry export/import and byte-identical registry verification around it.
- [ ] Run the OFF capability/off-wire proof against the 0.2.3f reference for absent/empty/stock/0 Transport and FastTransfer compatibility; retain the 108-byte record hashes and logs in `evidence/`.
- [ ] Check `git diff --check`, review every finding D1–D12 and protocol-2 requirement against code and tests, and check again for the optional D3 table without inventing one.
- [ ] Commit final fixes/scripts with the required trailer. Write `REPORT.md` with finding → fix → test, exact commits/gates, unverified items, and no third-party downloads. Remove owned build/temp artifacts required by the disk rule while retaining repos, tests, scripts, evidence, and report.
