# Agent guidance for XML2 Fix

These instructions apply throughout this repository, including automated pull-request reviews.

## Project and sources of truth

XML2 Fix is the Win32 C++ `dinput.dll` proxy for the player's own X-Men Legends II PC installation and Legends Classic builds. It supplies controller/display support, mods, save handling and engine/online fixes. Legends Classic's content builder and the Ultimate Legends launcher live in separate repositories; name companion dependencies explicitly.

Read `README.md`, `docs/BUILDING.md` and the affected subsystem's docs alongside its `src/` implementation and rule headers. For saves use `docs/save-format.md` and `docs/lcsave-library.md`; for online use `docs/gameplay-agreement.md`, `docs/gns-transport.md`, `docs/online-rooms.md`, `docs/online-menus.md` and `docs/join-in-progress.md`. `RELEASING.md` and `.github/workflows/build.yml` define packaging and stable/beta release policy.

## Content and privacy boundaries

Ship our code, original documentation, format facts and synthetic fixtures only. Never add game assets, decoded game data, game text, disassembly/decompiler listings, official logos, modified game executables, ownership bypasses, game-download links or personal paths/IPs/emails/tokens. Retail executable data needed by tests is supplied locally and read-only. Review `.content-guard-allow` changes individually; an exception must not excuse prohibited content.

## Implementation and review

- Verify retail bytes, addresses, bounds, calling conventions, register/stack preservation and object lifetimes before patching. Use the subsystem's guarded transaction and refusal behavior; a partial install or uncertified rollback must not be treated as success.
- Preserve unrequested/disabled behavior and the stock XML2 path. Do not make a test pass by weakening guards, changing gameplay policy or silently substituting XML2 behavior for an XML1 rule.
- Treat saves, catalogue bindings, mod load order, INI defaults and installed gameplay-agreement identities as compatibility contracts. Preserve player files. Review rejection paths and mixed-version peers, as well as the successful path.
- Online play is deterministic lockstep: one shared input frame drives one simulation step on every PC. Code that runs during a session must not draw synchronized RNG, advance simulation time, move the camera or act on local-only input outside that shared frame (including while a PC waits at the frame gate); a difference surfaces later as a checksum mismatch, not at the cause. Any change to what peers must agree on needs a new gameplay-agreement identity so mixed versions are refused instead of desyncing.
- Online rooms use GNS plus the broker. Review admission, stale callbacks, disconnect/leave/rejoin, late join and host-loss behavior; distinguish same-PC/LAN, internet and relayed coverage. A no-GNS DLL can build and pass basic tests while lacking release online capabilities.
- Review final diffs with callers/consumers. Report concrete defects with file/line, trigger and consequence; distinguish hypotheses and report missing validation. Avoid speculative findings and unrelated rewrites.

## Validation

Use the matching MSVC generator, Win32 and static CRT described in `docs/BUILDING.md`. Release-capable builds need the pinned GNS package selected by `XML2_FIX_GNS_ROOT`; a cold dependency build downloads inputs. On a shared desktop use Idle priority and serial MSBuild (`/m:1 /nr:false`).

CI runs these checks from the root (native binaries require the Release build):

```
python scripts/test_release_metadata.py
python scripts/test_run_native.py
python scripts/test_check_build_paths.py
build/bin/Release/xml2_test.exe
build/bin/Release/lcsave_test.exe
```

Native tests must also work without a console window: the native runner uses `CREATE_NO_WINDOW`, and the synthetic keyboard pipe checks skip only the console-specific cooperative-level setup when `GetConsoleWindow()` is null. Do not add a focus or console dependency to headless tests.

CI's two native entry points are not the full local matrix. `scripts/run_native.py` runs additional suites, some against legally owned retail inputs; read `scripts/README-gates.md` for required `XML2_FIX_*` environment variables and logs. Keep synthetic tests separate from retail-image/installer proofs and live gameplay. Do not claim a skipped check passed. Game, clipboard and desktop tests require authorization and disposable copies; preserve saves and registry state and stop owned processes afterward.

Release validation checks the actual DLL and unpacked package with `scripts/check_release_dll.py`, including version, online capability, CA pin, imports and embedded local paths. Keep the pinned inputs under `packaging/gns/`, notices, hashes and the legacy `ultimate-legends.json` manifest contract intact. Source-location trimming must cover dependency libraries too. Windows autocrlf must not rewrite byte-sensitive test or hash-pinned inputs; add scoped `.gitattributes` rules when needed.

Keep changes scoped; update affected docs/tests and disclose companion requirements. Leave release versions, tags, agreement identity assignments and publishing to the maintainer unless explicitly tasked. A review does not authorize commits, pushes, merges, releases, deployment or announcements.
