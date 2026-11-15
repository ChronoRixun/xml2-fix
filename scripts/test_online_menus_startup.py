"""Run the production DLL's startup gates in the repo's owned-PE union host.

No game entry point is executed. All output is below --out; engine/retail inputs
are read only. The host creates a private, never displayed desktop before loading
engine DLLs. This complements the invented services in online_menus_test.cpp.
"""
from pathlib import Path
import argparse
import configparser
import hashlib
import json
import os
import re
import shutil
import subprocess


HOOKS = {0x49FAAC: 5, 0x5F1BB0: 5, 0x6128AB: 5, 0x612D1F: 5, 0x612DDA: 5, 0x5F1F8B: 5, 0x60A095: 5, 0x5B9609: 7, 0x69F688: 4, 0x69F694: 4, 0x69F66C: 4, 0x5C971B: 6, 0x6A23D8: 4, 0x69E454: 4, 0x69E464: 4,
         0x69E438: 4, 0x69E440: 4, 0x5BC3D0: 5,
         0x69F284: 4, 0x69F34C: 4, 0x6A0348: 4, 0x6A1F80: 4, 0x6A1FAC: 4, 0x6A1FE0: 4, 0x69F290: 4, 0x69F358: 4,
         0x69F2A0: 4, 0x69F368: 4, 0x69F2AC: 4, 0x69F374: 4, 0x6A24AC: 4, 0x69F2A4: 4, 0x69F320: 4, 0x69F328: 4, 0x69F3E8: 4, 0x69F3F0: 4, 0x69F2F4: 4, 0x69F3BC: 4, 0x5BAD1F: 5, 0x5C88BE: 5, 0x6141E0: 5, 0x69F36C: 4, 0x69F314: 4, 0x69F3DC: 4, 0x69E49C: 4, 0x69EDEC: 4}
from check_online_menus_guards import anchor_writes
HOOKS.update({0x6A02C0: 4, 0x6A1148: 4, 0x69F2BC: 4, 0x69F384: 4})
HOOKS.update({0x41A460: 5, 0x60220E: 4, 0x5AF5A0: 6})
HOOKS.update({0x609AD9: 6})
HOOKS.update({0x69F07C:4,0x69F080:4,0x69F0A4:4,0x6A2564:4})
HOOKS.update(anchor_writes(Path(__file__).resolve().parent.parent))
REGISTRY = r"HKCU\Software\Activision\X-Men Legends 2"
# The online menus install only over a committed LegendsClassicFonts transaction (060p fix B): every mode that sets
# Menus also turns the fonts on with the owned private font family copied in (--fonts), except fonts-absent.
FONT_ASSETS = ("UI/fonts/lc_fonts_pc.XMLB", "UI/fonts/lc_fonts_hd.XMLB", *(f"UI/fonts/lc_online_{r}.XMLB" for r in ("title", "row", "body", "label")),
               *(f"Textures/fonts/lc_online_{r}.IGB" for r in ("title", "row", "body", "label")))


def main():
    parser = argparse.ArgumentParser(__doc__)
    for name in ("dll", "host", "retail", "engine", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--fonts", type=Path, required=True, help="owned build folder holding the private font family (UI/fonts, Textures/fonts)")
    parser.add_argument("--trace-only",action="store_true",help="only C0 startup diagnostic gates")
    args = parser.parse_args()
    fonts = args.fonts.resolve()
    assert all((fonts / name).is_file() for name in FONT_ASSETS), "--fonts lacks the private font family"
    dll, host, retail, engine, out = (getattr(args, n).resolve() for n in
                                     ("dll", "host", "retail", "engine", "out"))
    assert host.name.lower() == "union_host.exe", "only the synthetic union host may run"
    assert hashlib.sha256(retail.read_bytes()).hexdigest() == "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b"
    out.mkdir(parents=True, exist_ok=False)
    rows = []
    modes = ("navigation-slot-guard", "navigation-entry-guard",
        'data-resource-close-guard', 'data-getter-guard', 'data-base-getter-guard', 'data-host-close-guard',
        'data-join-close-guard', 'data-fallback-guard', 'data-setter-guard', 'frontend-intern-guard',
        'frontend-allocation-guard', 'off', 'zero', 'invalid',
        'on', 'capacity', 'lookup-refused', 'ui-guard',
        'native-guard', 'post-guard', 'seed-guard', 'queue-guard',
        'handler-guard', 'teardown-guard', 'script-guard', 'end-lobby-guard',
        'end-running-guard', 'tokenizer-guard', 'dispatch-guard', 'preset-code',
        'preset-host-name-invalid', 'name-only-invalid', 'fonts-absent', 'anchor-guard',
        'anchor-busy', 'pda-mouse-guard', 'advanced-back-guard', 'advanced-slot-guard',
        'advanced-close-guard', 'advanced-teardown-guard', 'remastered-on', 'remastered-draw-guard',
        'remastered-prefix-guard', 'remastered-disabled', 'remastered-invalid', 'remastered-components',
        'remastered-cap-invalid', 'remastered-trace', 'remastered-trace-component-off', 'remastered-trace-invalid',
        'remastered-trace-profile-off',
    )
    new_modes=("remastered-defaults","remastered-point","remastered-point-invalid")+tuple(f"remastered-ui-guard{i}" for i in range(16))
    modes=tuple(dict.fromkeys(modes+new_modes))
    for mode in modes:
        if args.trace_only and mode!="remastered-trace": continue
        folder = out / mode
        folder.mkdir()
        shutil.copyfile(dll, folder / "dinput.dll")
        ini = "[Discord]\nEnabled=0\n[Test]\nNoActivate=1\n"
        if mode != "off":
            value = "0" if mode == "zero" else "invalid" if mode == "invalid" else "1"
            ini += "[Online]\nMenus=" + value + "\n"
            if mode != "fonts-absent":
                ini = ini.replace("[Online]\n", "[Game]\nLegendsClassicFonts=1\n[Online]\n")
                for name in FONT_ASSETS:
                    (folder / name).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(fonts / name, folder / name)
        if mode == "capacity":
            ini += "[Limits]\nScriptFunctions=320\n"
        if mode in ("preset-code", "preset-host-name-invalid", "name-only-invalid"):
            ini += "Transport=gns\nBroker=broker.invalid:10020\n"
            if mode == "preset-code": ini += "JoinCode=ABCDEFGH\n"
            else:
                ini += "RoomName=" + "x" * 33 + "\n"
                if mode == "preset-host-name-invalid": ini += "Host=broker\n"
            ini += "[Save]\nFormat=lc\n"
        if mode.startswith("remastered-trace"):
            ini=ini.replace("NoActivate=1\n","NoActivate=1\nRemasteredTrace="+("true" if mode=="remastered-trace-invalid" else "1")+"\n")
        if mode.startswith("remastered-"):
            ini += "[Remastered]\n"+("" if mode=="remastered-defaults" else "Anisotropy=16\n")+"MSAASamples=" + ("3" if mode=="remastered-cap-invalid" else "8") + "\n"
            if mode!="remastered-components":
                ini += "Enabled=" + ("0" if mode in ("remastered-disabled","remastered-trace-profile-off") else "true" if mode=="remastered-invalid" else "1") + "\n"
        if mode.startswith("remastered-trace"):
            ini += "CenteredHud="+("0" if mode=="remastered-trace-component-off" else "1")+"\n"
        if mode.startswith("remastered-ui-guard"): ini += "CenteredHud=1\n"
        if mode in ("remastered-point","remastered-point-invalid"):
            ini += "AnisotropyOnPointArt="+("1" if mode=="remastered-point" else "true")+"\n"
        (folder / "xml2-fix.ini").write_text(ini, encoding="utf-8", newline="\n")
        before, after = folder / "before.reg", folder / "after.reg"
        subprocess.run(["reg", "export", REGISTRY, str(before), "/y"], check=True, capture_output=True)
        env = dict(os.environ, UNION_PRIVATE_DESKTOP="1", TEMP=str(folder), TMP=str(folder))
        for name in ("UNION_MUTATE_VA", "UNION_CODE_OUT", "UNION_IMAGE_OUT", "UNION_READONLY_VA"):
            env.pop(name, None)
        if mode == "lookup-refused":
            env["UNION_MUTATE_VA"] = "0x4d696a"
        elif mode in ("navigation-slot-guard", "navigation-entry-guard"):
            env["UNION_MUTATE_VA"] = "0x6a2564" if mode=="navigation-slot-guard" else "0x5da1a0"
        elif mode == "ui-guard":
            env["UNION_MUTATE_VA"] = "0x5d8570"
        if mode in ("native-guard", "post-guard", "seed-guard", "queue-guard", "handler-guard", "teardown-guard", "script-guard", "end-lobby-guard", "end-running-guard", "tokenizer-guard", "dispatch-guard"):
            env["UNION_MUTATE_VA"] = {"native-guard":"0x614290", "post-guard":"0x5b9609", "seed-guard":"0x610dc0", "queue-guard":"0x60a095", "handler-guard":"0x5f1bb0", "teardown-guard":"0x6128ab", "end-lobby-guard":"0x612d1f", "end-running-guard":"0x612dda", "tokenizer-guard":"0x55b670", "dispatch-guard":"0x55bf2d", "script-guard":"0x49faac"}[mode]
        if mode in ("anchor-guard", "anchor-busy"):
            env["UNION_MUTATE_VA"] = "0x587e40" if mode=="anchor-guard" else "0x7ef2c8"
        if mode=="pda-mouse-guard": env["UNION_MUTATE_VA"]="0x69edec"
        if mode.startswith("advanced-"):
            env["UNION_MUTATE_VA"] = {"advanced-back-guard":"0x6a1fe0", "advanced-slot-guard":"0x6a1f80", "advanced-close-guard":"0x6a1fac", "advanced-teardown-guard":"0x6192e0"}[mode]
        if mode.startswith("data-"):
            env["UNION_MUTATE_VA"] = {"data-resource-close-guard":"0x5af5a0", "data-getter-guard":"0x6a1148", "data-base-getter-guard":"0x6a02c0", "data-host-close-guard":"0x69f2bc", "data-join-close-guard":"0x69f384", "data-fallback-guard":"0x5c67f0", "data-setter-guard":"0x5c6850"}[mode]
        if mode.startswith("frontend-"):
            env["UNION_MUTATE_VA"] = "0x41a460" if mode=="frontend-intern-guard" else "0x60220e"
        if mode in ("remastered-draw-guard","remastered-prefix-guard"):
            env["UNION_MUTATE_VA"]="0x61939a" if mode=="remastered-draw-guard" else "0x619398"
        if mode.startswith("remastered-ui-guard"):
            env["UNION_MUTATE_VA"]=hex((0x621ca0,0x624520,0x621ea0,0x61dcab,0x61ff70,0x6a5804,0x625df0,0x61ed69,0x6260ac,0x626105,0x62611c,0x621d07,0x621d19,0x61fe7b,0x6a3be4,0x6a3be8)[int(mode.removeprefix("remastered-ui-guard"))])
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
        installed = mode in ("on", "preset-code", "preset-host-name-invalid", "name-only-invalid", "remastered-on", "remastered-disabled", "remastered-invalid", "remastered-components", "remastered-cap-invalid", "remastered-trace", "remastered-trace-component-off", "remastered-trace-invalid", "remastered-trace-profile-off")
        installed=installed or mode in new_modes
        for site, size in HOOKS.items():
            assert bool(changed.intersection(range(site, site + size))) == installed, (mode, hex(site))
        if mode.startswith("remastered-"):
            status_owned=mode in ("remastered-on","remastered-cap-invalid","remastered-trace","remastered-trace-component-off","remastered-trace-invalid")
            status_owned=status_owned or mode in new_modes
            assert bool(changed.intersection(range(0x61939a,0x61939f)))==status_owned, mode
            assert ("owns later renderer call 0x61939a" in log)==status_owned, mode
            if status_owned:
                assert log.index("74 patch writes and menu-anchors/512 committed atomically") < log.index("owns later renderer call 0x61939a")
                assert ("selective AF authored-path bridge active" in log)==(mode!="remastered-defaults")
            if mode in ("remastered-draw-guard","remastered-prefix-guard"):
                for site in (0x61f356,0x61f8a4,0x61f8be,0x61f667):
                    assert not changed.intersection(range(site,site+5)), (mode,hex(site))
            if mode=="remastered-cap-invalid": assert "invalid MSAASamples='3'" in log
            if mode=="remastered-defaults": assert "AF request=0 point-art=0; Beenox=0" in log
            if mode=="remastered-point": assert "point art=1" in log
            if mode=="remastered-point-invalid": assert "invalid AnisotropyOnPointArt='true'" in log and "point art=0" in log
            if mode.startswith("remastered-ui-guard"):
                assert "remastered UI: committed" not in log and "native layout retained" in log
            for site in (0x5eecf0,0x5ef2e0,0x597c90,0x5971a0,0x597200):
                assert not changed.intersection(range(site,site+5)),(mode,"dialogue changed",hex(site))
        if mode.startswith("remastered-trace"):
            assert ("centered-ui: C0 bounded read-only pilot" in log)==(mode=="remastered-trace"), mode
            ui_owned=mode not in ("remastered-trace-component-off","remastered-trace-profile-off")
            for site in (0x621ca0,0x61dcab,0x624520,0x621ea0):
                assert bool(changed.intersection(range(site,site+5)))==ui_owned,(mode,hex(site))
            assert ("remastered UI: committed" in log)==ui_owned, mode
        if installed:
            assert "script functions: 1024 names" in log
            assert "dispatcher lcOnlineIntent n(i) registered; 74 patch writes and menu-anchors/512 committed atomically" in log
        else:
            assert "dispatcher lcOnlineIntent" not in log
        if mode == "fonts-absent":
            assert "online menus: [Game] LegendsClassicFonts is not active (off or refused); rebuilt LC failure entry retained" in log
            assert "lc fonts: committed" not in log
        elif mode != "off":
            assert "lc fonts: committed" in log, (mode, log)
        if mode in ("on", "ui-guard"):
            # Menus alone (no ForcedTeams/explicit limit/LC assets) must enlist
            # rule 14 through the actual exports.cpp + script_functions.cpp path.
            assert "shared gameplay agreement sites published" in log
            assert re.search(r"rule 14 \(ScriptFunctions\) revision 1 \{1024,0,0,0\}", log)
        if mode in ("off", "zero", "invalid"):
            assert "script functions: 1024 names" not in log
            assert not re.search(r"rule 14 .*\{1024,0,0,0\}", log)
        if mode == "lookup-refused":
            assert "registration/capacity refused; rebuilt LC failure entry retained" in log
            assert "installation unverified; online remains blocked" in log
        if mode == "capacity":
            assert "registration/capacity refused; rebuilt LC failure entry retained" in log
            assert "script functions: 1024 names" not in log
        if mode in ("preset-code", "preset-host-name-invalid", "name-only-invalid"):
            assert "menu broker client prepared; room creation waits for an explicit intent" in log, (mode, log)
            assert not (folder / "online-status.json").exists(), "a launcher preset must not start status before controller acceptance"
            assert not (folder / "online-room.txt").exists()
            assert "GNS initialised on the game's thread" not in log, "startup must not initialize GNS for a menu preset"
            parsed = configparser.ConfigParser(interpolation=None)
            parsed.read(folder / "xml2-fix.ini")
            for key in ("Host", "JoinCode", "RoomName"):
                assert not parsed.has_option("Online", key), (mode, key)
            if mode != "preset-code": assert "invalid launcher RoomName discarded; Browse/Code remain available" in log
        rows.append(dict(mode=mode, exit=done.returncode, hooks_installed=installed,
                         registry_equal=True, private_desktop=True))
        print(mode, "PASS", flush=True)
    (out / "results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {len(rows)}/{len(rows)} production startup gates; no game launched")


if __name__ == "__main__":
    main()
