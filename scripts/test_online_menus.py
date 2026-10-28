"""Complete online-menu host matrix. Outputs only under the supplied directory."""
from pathlib import Path
import argparse
import json
import re
import subprocess


def host_modes(root):
    count = len(re.findall(r'\{0x', (root / "src/online_menus_guards.hpp").read_text()))
    return ["r6-end-direct-guest", "r6-end-lobby-guest", "r6-end-quit-host", "r6-end-quit-guest", "r6-end-disconnect-host", "r6-end-disconnect-guest", "r6-end-loss-guest", "r6-end-quit-throw-host", "r6-end-quit-throw-guest", "r6-end-loss-throw-guest", "r6-end-prune-host", "r6-end-prune-guest", "r6-end-options", "r6-end-unmarked", "r6-end-id-order", "r6-nit-mixed", "r6-nit-same-host", "r6-nit-same-guest", "r6-nit-script-old", "r6-nit-setup-mixed", "r6-nit-script", "r6-nit-end", "r6-nit-end-guest", "r6-nit-tokens", "r6-nit-hosttokens", "r6-nit-discovery", "r6-nit-tunnel", "r6-generations", "r6-native", "r6-lobby-new", "r6-lobby-load", "r5-broker", "r5b-views", "r5-retail-lobby", "r4-retail-list", "r4-back-combos", "r4-unknown-kick", "r4-dismiss-invalid", "r4-refresh-paths", "r4-join-fallback", "r4-host-fallback", "r4-mouse", "r4-native-faults", "r4-no-marker", "follow-lifecycle", "follow-name-faults", "follow-keyboard", "follow-queue", "follow-clipboard", "follow-storage-ok", "follow-storage-readonly", "follow-storage-directory", "follow-storage-partial", "follow-storage-flush", "follow-storage-128", "follow-storage-129", "follow-storage-crlf", "r4-host", "r4-join", "r4-advice", "r4-advice-join", "r4-advice-notice", "r8-party-host", "r8-party-guest", "r8-party-intents", "r4-r7-cards", "r4-r7-cards-join", "r4-r7-bounds", "r4-negative", "r4-pool", "r4-copy", "review-binding", "review-entry", "review-no-prompt", "review-contracts", "review-exceptions", "asset-badalloc", "asset-filesystem", "install-retain", "install-add-fail", "r3", "r3-load", "r3-load-invalid", "r3-local-only", "on", "off", "zero", "base", "capacity", "fonts-off", "assets", "bad-marker", "missing-file", "name31", "name32", "blank-name", "rollback", "protect", "flush", "retain",
             "fail-network", "fail-session", "fail-campaign", "fail-persist"] + [f"guard{i}" for i in range(count)]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("test", type=Path)
    parser.add_argument("exe", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    binary, retail, out = args.test.resolve(), args.exe.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parent.parent
    modes = host_modes(root)
    results = []
    for mode in modes:
        result = subprocess.run([str(binary), str(retail), mode], cwd=out, capture_output=True,
                                text=True, timeout=30, creationflags=0x08000000)
        (out / (mode + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        results.append(dict(mode=mode, exit=result.returncode))
        print(mode, result.returncode, flush=True)
    (out / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    assert all(r["exit"] == 0 for r in results), results
    print(f"PASS {len(results)}/{len(results)} online menus host modes; no game launched")


if __name__ == "__main__":
    main()
