"""Complete online-menu host matrix. Outputs only under the supplied directory."""
from pathlib import Path
import argparse
import json
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("test", type=Path)
    parser.add_argument("exe", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    binary, retail, out = args.test.resolve(), args.exe.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parent.parent
    count = len(re.findall(r'\{0x', (root / "src/online_menus_guards.hpp").read_text()))
    modes = ["on", "off", "zero", "base", "capacity", "assets", "bad-marker", "missing-file", "name31", "name32", "blank-name", "rollback", "protect", "flush", "retain",
             "fail-network", "fail-session", "fail-campaign", "fail-persist"] + [f"guard{i}" for i in range(count)]
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
