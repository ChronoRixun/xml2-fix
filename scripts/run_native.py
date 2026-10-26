"""Run the branch's native tests without launching the game. Retail-exe tests receive XMen2.exe read-only."""
from pathlib import Path
import subprocess, json, re, os, sys

import os as _os

def _need(name):
    """Machine-specific inputs come from the environment (see scripts/README-gates.md); nothing local is hardcoded."""
    value = _os.environ.get(name)
    if not value:
        raise SystemExit(f"set {name} (see scripts/README-gates.md)")
    return value

R = Path(__file__).resolve().parent.parent
b = R / "native/bin/Release"
out = R / "logs/native"
LIST_ONLY = "--list" in sys.argv  # print the command matrix; nothing runs, nothing is created or deleted
EXE = _need("XML2_FIX_TEST_EXE")
# The owner's original-Xbox default.xbe, read only: the weapon brain and AI query scheduler suites execute XML1's
# own routines from it next to XMen2.exe's (their exact-comparison halves need it).
XBE = _need("XML2_FIX_TEST_XBE")
env = os.environ.copy()
env["XML2_FIX_TEST_EXE"] = EXE
env["XML2_FIX_TEST_GAME_DIR"] = _need("XML2_FIX_TEST_GAME_DIR")
env["TEMP"] = env["TMP"] = str(R / "temp")
stale = b / "xml2-fix.ini"

src = (R / "test/xml2_test.cpp").read_text(encoding="utf-8", errors="replace")
flags = sorted(set(re.findall(r'"(--[a-z0-9-]+-rules)"', src)))
cmds = [[str(b / "xml2_test.exe"), flag] for flag in flags]
if "--plain" in sys.argv:
    cmds.append([str(b / "xml2_test.exe")])
with_exe = {
    "xml2_gns_transport_test", "xml2_confirmation_release_test", "xml2_objective_text_test",
    "xml2_entity_grid_test", "xml2_entity_world_test", "xml2_entity_frames_test",
    "xml2_entity_query_test", "xml2_entity_transfer_test", "xml2_gameplay_agreement_test",
    "xml2_rank_costs_test", "xml2_multipart_install_test", "xml2_script_functions_test",
    "xml2_death_switch_test", "xml2_world_impact_test", "xml2_stun_rule_test",
    "xml2_test_mode_test", "xml2_lockon_effects_test", "xml2_box_harm_test",
    "xml2_harm_single_chain_test", "xml2_world_backfaces_test", "xml2_harm_loop_startup_test", "xml2_reliable_receive_test",
    "xml2_campaign_sender_test", "xml2_receive_validation_test", "xml2_join_in_progress_test",
    "xml2_combo_heads_test", "xml2_input_worker_test", "xml2_model_pools_test", "xml2_model_pools_lifecycle_test",
    "xml2_npc_combo_timing_test", "xml2_x1_leader_test", "xml2_solo_mode_test", "xml2_resistance_display_install_test",
    "xml2_npc_semi_auto_test", "xml2_npc_query_scheduler_test",
}
# Second argument: the XBE (beta 7: the scheduler suite requires it, the weapon brain compares the original callbacks).
with_xbe = {"xml2_npc_semi_auto_test", "xml2_npc_query_scheduler_test"}
skip = {
    "xml2_test", "lcsave_durable_parity", "lcsave_test", "xml2_save_startup_test",
    "xml2_danger_room_party_install_test", "xml2_lockon_effects_install_test",
    "xml2_danger_room_healthbars_install_test", "xml2_speaker_label_install_test", "xml2_classic_hud_install_test",
    "xml2_codex_back_install_test", "xml2_resistance_display_install_test",
}
for exe in sorted(b.glob("*.exe")):
    name = exe.stem
    if name not in skip:
        cmds.append(([str(exe), EXE] + ([XBE] if name in with_xbe else [])) if name in with_exe else [str(exe)])
cmds.append([str(b / "lcsave_test.exe"), "--fuzz", "2000"])
vectors = Path(_need("XML2_FIX_LEGACY_VECTORS_DIR"))
for directory in sorted(p for p in vectors.iterdir() if p.is_dir() and (p / "cases.tsv").exists()):
    cmds.append([str(b / "lcsave_durable_parity.exe"), str(directory)])
cmds += [[str(b / "xml2_danger_room_party_install_test.exe"), mode] for mode in ("on", "off", "invalid", "base", "guard0", "guard1", "guard2", "guard3")]
cmds += [[str(b / "xml2_lockon_effects_install_test.exe"), EXE, mode] for mode in ("on", "off", "visual", "no-effects", "bad-paths", "empty-effects", "effects-off", "both-off", "invalid", "base", "guard0", "guard1", "guard2", "guard3", "guard4", "dependency")]
cmds += [[str(b / "xml2_danger_room_healthbars_install_test.exe"), mode] for mode in ("on", "off", "xml2", "invalid", "base", "guard0", "guard1")]
# Install-mode suites that take the retail exe and a mode (each exits non-zero without both).
cmds += [[str(b / "xml2_speaker_label_install_test.exe"), EXE, mode] for mode in ["rule", "on", "off"] + [f"guard{i}" for i in range(7)]]
cmds += [[str(b / "xml2_codex_back_install_test.exe"), EXE, mode] for mode in ["on", "off"] + [f"guard{i}" for i in range(8)]]
cmds += [[str(b / "xml2_resistance_display_install_test.exe"), EXE, mode] for mode in ["on", "on-native", "off", "xml2", "invalid", "base"] + [f"guard{i}" for i in range(6)]]
_hud = (R / "test/classic_hud_install_test.cpp").read_text(encoding="utf-8")
_hud_sites = len(re.findall(r"0x[0-9a-fA-F]+", re.search(r"sites\[\]\s*\{(.*?)\};", _hud, re.S)[1]))
cmds += [[str(b / "xml2_classic_hud_install_test.exe"), EXE, mode] for mode in ["on", "off", "zero", "named-off", "legacy", "invalid", "base", "assets", "alias", "stale"] + [f"guard{i}" for i in range(_hud_sites)]]

if LIST_ONLY:
    # The command matrix only: for checking the runner against another gate's matrix. Everything above only reads.
    for cmd in cmds:
        print(" ".join(cmd))
    raise SystemExit(0)
out.mkdir(parents=True, exist_ok=True)
(R / "temp").mkdir(exist_ok=True)
if stale.exists():
    stale.unlink()
    print("deleted a stale xml2-fix.ini next to the tests")
results = []
for cmd in cmds:
    tag = Path(cmd[0]).stem
    if len(cmd) > 1 and cmd[1].startswith("--"):
        tag += cmd[1]
    if "durable_parity" in tag:
        tag += "-" + Path(cmd[1]).name
    if len(cmd) > 1 and cmd[1] == EXE:
        tag += "+exe"
    if XBE in cmd:
        tag += "+xbe"
    if tag.removesuffix("+exe").endswith("_install_test") and cmd[-1] not in (EXE, XBE):
        tag += "-" + cmd[-1]
    if len(cmd) == 1 and tag == "xml2_test":
        tag += "-plain"
    log = out / (tag + ".log")
    with log.open("w") as handle:
        completed = subprocess.run(cmd, stdout=handle, stderr=subprocess.STDOUT, cwd=b, env=env, timeout=900, creationflags=0x08000000)
    text = log.read_text(errors="replace").strip().splitlines()
    last = text[-1] if text else ""
    skipped = any("SKIPPED" in line for line in text)
    results.append(dict(tag=tag, exit=completed.returncode, last=last, skipped=skipped))
    print(f'{completed.returncode:>3} {"SKIP " if skipped else ""}{tag} :: {last[:150]}', flush=True)
    if Path(cmd[0]).stem == "xml2_log_test":
        for suffix in ("", ".old"):
            (b / ("log-test.log" + suffix)).unlink(missing_ok=True)
leftover = [p.name for p in b.iterdir() if p.suffix.lower() in (".ini", ".log", ".txt")]
print("files left in bin:", leftover)
(out / "results.json").write_text(json.dumps(results, indent=1))
failed = sum(1 for result in results if result["exit"])
print("total", len(results), "failed", failed, "with SKIPPED", sum(1 for result in results if result["skipped"]))
raise SystemExit(bool(failed))
