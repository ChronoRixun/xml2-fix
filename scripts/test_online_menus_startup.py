"""Run the production DLL's startup gates in the repo's owned-PE union host.

No game entry point is executed. All output is below --out; engine/retail inputs
are read only. The host creates a private, never displayed desktop before loading
engine DLLs. This complements the invented services in online_menus_test.cpp.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess


HOOKS = {0x5C971B: 6, 0x6A23D8: 4, 0x69E454: 4, 0x69E464: 4,
         0x69E438: 4, 0x69E440: 4, 0x5BC3D0: 5,
         0x69F284: 4, 0x69F350: 4, 0x69F290: 4, 0x69F358: 4,
         0x69F2A0: 4, 0x69F368: 4, 0x69F2AC: 4, 0x69F374: 4, 0x6A24AC: 4}
REGISTRY = r"HKCU\Software\Activision\X-Men Legends 2"


def main():
    parser = argparse.ArgumentParser(__doc__)
    for name in ("dll", "host", "retail", "engine", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    dll, host, retail, engine, out = (getattr(args, n).resolve() for n in
                                     ("dll", "host", "retail", "engine", "out"))
    assert host.name.lower() == "union_host.exe", "only the synthetic union host may run"
    assert hashlib.sha256(retail.read_bytes()).hexdigest() == "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b"
    out.mkdir(parents=True, exist_ok=False)
    rows = []
    for mode in ("off", "zero", "invalid", "on", "capacity", "lookup-refused", "ui-guard"):
        folder = out / mode
        folder.mkdir()
        shutil.copyfile(dll, folder / "dinput.dll")
        ini = "[Discord]\nEnabled=0\n"
        if mode != "off":
            value = "0" if mode == "zero" else "invalid" if mode == "invalid" else "1"
            ini += "[Online]\nMenus=" + value + "\n"
        if mode == "capacity":
            ini += "[Limits]\nScriptFunctions=320\n"
        (folder / "xml2-fix.ini").write_text(ini, encoding="utf-8", newline="\n")
        before, after = folder / "before.reg", folder / "after.reg"
        subprocess.run(["reg", "export", REGISTRY, str(before), "/y"], check=True, capture_output=True)
        env = dict(os.environ, UNION_PRIVATE_DESKTOP="1", TEMP=str(folder), TMP=str(folder))
        for name in ("UNION_MUTATE_VA", "UNION_CODE_OUT", "UNION_IMAGE_OUT", "UNION_READONLY_VA"):
            env.pop(name, None)
        if mode == "lookup-refused":
            env["UNION_MUTATE_VA"] = "0x4d696a"
        elif mode == "ui-guard":
            env["UNION_MUTATE_VA"] = "0x5d8570"
        try:
            done = subprocess.run([str(host), str(folder / "dinput.dll"), str(retail), str(engine),
                                   str(folder / "writes.tsv")], cwd=folder, env=env, capture_output=True,
                                  text=True, timeout=60, creationflags=0x08000000)
        finally:
            subprocess.run(["reg", "export", REGISTRY, str(after), "/y"], check=True, capture_output=True)
            assert before.read_bytes() == after.read_bytes(), "fixture changed the registry"
        (folder / "host.log").write_text(done.stdout + done.stderr, encoding="utf-8")
        assert done.returncode == 0, (mode, done.returncode, done.stdout, done.stderr)
        log = (folder / "xml2-fix.log").read_text(errors="replace")
        changed = set()
        for line in (folder / "writes.tsv").read_text().splitlines():
            cells = line.split("\t")
            if cells[0:2] == ["W", "XMen2.exe"]:
                changed.add(int(cells[2], 16))
        installed = mode == "on"
        for site, size in HOOKS.items():
            assert bool(changed.intersection(range(site, site + size))) == installed, (mode, hex(site))
        if installed:
            assert "script functions: 1024 names" in log
            assert "dispatcher lcOnlineIntent n(i) registered; 16 hooks committed atomically" in log
        else:
            assert "dispatcher lcOnlineIntent" not in log
        if mode in ("on", "ui-guard"):
            # Menus alone (no ForcedTeams/explicit limit/LC assets) must enlist
            # rule 14 through the actual exports.cpp + script_functions.cpp path.
            assert "shared gameplay agreement sites published" in log
            assert re.search(r"rule 14 \(ScriptFunctions\) revision 1 \{1024,0,0,0\}", log)
        if mode in ("off", "zero", "invalid"):
            assert "script functions: 1024 names" not in log
            assert not re.search(r"rule 14 .*\{1024,0,0,0\}", log)
        if mode == "lookup-refused":
            assert "registration/capacity refused; native online entry retained" in log
            assert "installation unverified; online remains blocked" in log
        if mode == "capacity":
            assert "registration/capacity refused; native online entry retained" in log
            assert "script functions: 1024 names" not in log
        rows.append(dict(mode=mode, exit=done.returncode, hooks_installed=installed,
                         registry_equal=True, private_desktop=True))
        print(mode, "PASS", flush=True)
    (out / "results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {len(rows)}/{len(rows)} production startup gates; no game launched")


if __name__ == "__main__":
    main()
