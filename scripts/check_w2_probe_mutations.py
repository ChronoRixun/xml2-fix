"""W2 F1 mutation check: every probe-oracle fixture of lcsave_test (test_w2_api section (7)) must fail on a
deliberately broken representability probe and pass on the real code.

For each mutation the script edits one rule of the probe in the source tree, builds lcsave_test into BUILD, runs it,
records which "F1 ..." checks failed (and how many checks outside F1 failed too), and restores the file byte for byte
(it refuses to start on a dirty source file). Finally the unmutated tree is rebuilt and must pass.

    python scripts/check_w2_probe_mutations.py --build BUILD_DIR [--json OUT]

BUILD_DIR is a configured CMake build directory of this repository (Win32). Nothing outside the source files named
below and BUILD_DIR is written.
"""
import argparse, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (name, file, old text, new text, the F1 check names that must fail)
MUTATIONS = [
    ("enhancement records: the pre-W2 table bound ignored (512 -> 4096)", "src/lcsave/reverse.cpp",
     "const u32 records = 512;", "const u32 records = 4096;",
     ["F1 enhancement record 512 sets bit 1", "F1 enhancement record 600 sets bit 1"]),
    ("zone records: the target's record count not applied in probe mode", "src/lcsave/reverse.cpp",
     'if (m.zones.size() > native.record_count) fail("more zone records than the container holds");',
     'if (!pos.probe && m.zones.size() > native.record_count) fail("more zone records than the container holds");',
     ["F1 6 zone records set bit 1", "F1 8 zone records set bit 1"]),
    ("talents: a hero position >= M accepted on the target mapping", "src/lcsave/oldsave.cpp",
     "if (owner == 0 ? position >= m - 1 : position >= m)", "if (owner == 0 ? position >= m - 1 : position >= 2 * m)",
     ["F1 talent position 100 sets bit 1"]),
    ("talents: a shared id >= M - 1 accepted on the target mapping", "src/lcsave/oldsave.cpp",
     "if (owner == 0 ? position >= m - 1 : position >= m)", "if (owner == 0 ? position >= 2 * m - 1 : position >= m)",
     ["F1 shared talent id 99 sets bit 1"]),
    ("objectives: only objectives holding state are judged (not every registered one)", "src/lcsave/reverse.cpp",
     "if (found != t.p_obj.end() && found->second > 255)",
     'if (found != t.p_obj.end() && found->second > 255 && (o["counter"].as_int() || o["flags"].as_int()))',
     ["F1 a build registering 257 objectives marks a save with no state above 255"]),
]


def build_and_run(build):
    b = subprocess.run(["cmake", "--build", build, "--config", "Release", "--target", "lcsave_test", "--", "/m:4"],
                       capture_output=True, text=True, errors="replace")
    if b.returncode != 0:
        return None, b.stdout[-4000:]
    exe = os.path.join(build, "bin", "Release", "lcsave_test.exe")
    r = subprocess.run([exe, "--fuzz", "200"], capture_output=True, text=True, errors="replace")
    return r, r.stdout


def failures_of(text):
    return [line[6:] for line in text.splitlines() if line.startswith("FAIL: ")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--json")
    a = ap.parse_args()
    results = []
    ok = True
    for name, rel, old, new, expected in MUTATIONS:
        path = os.path.join(ROOT, rel)
        dirty = subprocess.run(["git", "-C", ROOT, "diff", "--quiet", "--", rel]).returncode
        if dirty:
            sys.exit(f"{rel} has uncommitted changes; refusing to mutate it")
        original = open(path, "rb").read()
        text = original.decode("utf-8")
        if text.count(old) != 1:
            sys.exit(f"{name}: the rule's text is not found exactly once in {rel}")
        try:
            open(path, "wb").write(text.replace(old, new).encode("utf-8"))
            r, out = build_and_run(a.build)
        finally:
            open(path, "wb").write(original)
        if r is None:
            results.append({"mutation": name, "built": False, "log": out})
            ok = False
            print(f"BUILD FAILED  {name}")
            continue
        fails = failures_of(out)
        f1 = [f for f in fails if f.startswith("F1 ")]
        caught = [e for e in expected if any(f.startswith(e) for f in f1)]
        killed = len(caught) == len(expected)
        ok &= killed
        others = [f for f in fails if not f.startswith("F1 ")]
        results.append({"mutation": name, "built": True, "killed": killed, "expected": expected, "f1_failures": f1,
                        "other_failures": len(others), "other_examples": others[:5]})
        print(f"{'KILLED' if killed else 'SURVIVED'}  {name}: {len(f1)} F1 check(s) failed, {len(others)} other check(s) failed")
        for f in f1:
            print(f"    {f[:160]}")
    r, out = build_and_run(a.build)
    clean = r is not None and r.returncode == 0
    m = re.search(r"F1 probe-oracle fixtures: (\d+) checks", out or "")
    print(f"unmutated: {'passed' if clean else 'FAILED'}; F1 checks {m.group(1) if m else '?'}")
    ok &= clean
    if a.json:
        json.dump({"mutations": results, "unmutated_passed": clean, "f1_checks": int(m.group(1)) if m else None}, open(a.json, "w"), indent=1)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
