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
b = Path(os.environ.get("XML2_FIX_TEST_BIN_DIR", R / "native/bin/Release"))
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
    "xml2_roll_test", "xml2_dr_reward_test",
    "xml2_gns_transport_test", "xml2_confirmation_release_test", "xml2_objective_text_test",
    "xml2_save_limits_test", "xml2_item_capacity_test", "xml2_entity_grid_test", "xml2_entity_world_test", "xml2_entity_frames_test",
    "xml2_entity_query_test", "xml2_entity_transfer_test", "xml2_gameplay_agreement_test",
    "xml2_rank_costs_test", "xml2_multipart_install_test", "xml2_script_functions_test", "xml2_script_pools_test",
    "xml2_death_switch_test", "xml2_world_impact_test", "xml2_stun_rule_test",
    "xml2_test_mode_test", "xml2_lockon_effects_test", "xml2_box_harm_test",
    "xml2_harm_single_chain_test", "xml2_world_backfaces_test", "xml2_harm_loop_startup_test", "xml2_reliable_receive_test",
    "xml2_campaign_sender_test", "xml2_receive_validation_test", "xml2_join_in_progress_test",
    "xml2_combo_heads_test", "xml2_input_worker_test", "xml2_model_pools_test", "xml2_model_pools_lifecycle_test",
    "xml2_npc_combo_timing_test", "xml2_x1_leader_test", "xml2_solo_mode_test", "xml2_resistance_display_install_test",
    "xml2_npc_semi_auto_test", "xml2_npc_query_scheduler_test", "xml2_shared_handlers_test",
    "xml2_xml1_cling_test", "xml2_lift_rule_test", "xml2_xml1_controllers_test", "xml2_dopple_xbe_test",
    "xml2_xml1_grab_carry_test", "xml2_xml1_grab_carry_install_test", "xml2_xml1_air_carry_test", "xml2_shared_handlers_c_test", "xml2_tk_guard_test",
    "xml2_anim_enums_test", "xml2_char_sounds_test", "xml2_motion_pool_test", "xml2_resident_stats_test", "xml2_resident_payload_test",
    "xml2_pool_counter_observers_test", "xml2_particle_scratch_test", "xml2_effect_templates_test", "xml2_effect_pools_test", "xml2_fight_moves_host",
    "xml2_power_pools_host", "xml2_balance_parity_test", "xml2_e4_observe_host",
    "xml2_pad_hotplug_test",
    "xml2_pad_hotplug_live_test",
}
# Second argument: the XBE (beta 7: the scheduler suite requires it, the weapon brain compares the original callbacks).
with_xbe = {"xml2_npc_semi_auto_test", "xml2_npc_query_scheduler_test", "xml2_shared_handlers_test", "xml2_shared_handlers_c_test", "xml2_dopple_xbe_test", "xml2_tk_guard_test", "xml2_balance_parity_test"}
skip = {
    "xml2_test", "lcsave_durable_parity", "lcsave_test", "xml2_save_startup_test", "lcsave_zone_records",
    "xml2_danger_room_party_install_test", "xml2_lockon_effects_install_test",
    "xml2_danger_room_healthbars_install_test", "xml2_speaker_label_install_test", "xml2_classic_hud_install_test",
    "xml2_codex_back_install_test", "xml2_resistance_display_install_test", "xml2_economy_test",
    # argument-taking tools run by their own gates: W2's G3 real-saves run (--list/--data/--registry/--out) and the
    # E4 actor-pools host (scripts/test_actor_pools.py); without arguments both only print their usage (exit 2)
    "lcsave_real_saves", "actor_pools_host",
    "xml2_e4_core_host", # its boundary/fault/encoding matrix runs through test_e4_core.py below
}
for exe in sorted(b.glob("*.exe")):
    name = exe.stem
    if name not in skip:
        cmds.append(([str(exe), EXE] + ([XBE] if name in with_xbe else [])) if name in with_exe else [str(exe)])
cmds.append([str(b / "lcsave_test.exe"), "--fuzz", "2000"])
cmds += [[str(b / "xml2_script_pools_test.exe"), EXE, mode] for mode in ("invalid", "owner", "guard", "functions")]
cmds.append([str(b / "xml2_x1_leader_test.exe"), "economy"])
vectors = Path(_need("XML2_FIX_LEGACY_VECTORS_DIR"))
for directory in sorted(p for p in vectors.iterdir() if p.is_dir() and (p / "cases.tsv").exists()):
    cmds.append([str(b / "lcsave_durable_parity.exe"), str(directory)])
cmds += [[str(b / "xml2_danger_room_party_install_test.exe"), mode] for mode in ("on", "off", "invalid", "base", "guard0", "guard1", "guard2", "guard3")]
cmds += [[str(b / "xml2_lockon_effects_install_test.exe"), EXE, mode] for mode in ("on", "raised", "off", "visual", "no-effects", "bad-paths", "empty-effects", "effects-off", "both-off", "invalid", "base", "guard0", "guard1", "guard2", "guard3", "guard4", "dependency")]
cmds += [[str(b / "xml2_danger_room_healthbars_install_test.exe"), mode] for mode in ("on", "off", "xml2", "invalid", "base", "guard0", "guard1")]
# Install-mode suites that take the retail exe and a mode (each exits non-zero without both).
cmds += [[str(b / "xml2_speaker_label_install_test.exe"), EXE, mode] for mode in ["rule", "on", "off"] + [f"guard{i}" for i in range(7)]]
cmds += [[str(b / "xml2_codex_back_install_test.exe"), EXE, mode] for mode in ["on", "off"] + [f"guard{i}" for i in range(8)]]
cmds += [[str(b / "xml2_resistance_display_install_test.exe"), EXE, mode] for mode in ["on", "on-native", "off", "xml2", "invalid", "base"] + [f"guard{i}" for i in range(6)]]
_hud = (R / "test/classic_hud_install_test.cpp").read_text(encoding="utf-8")
_hud_sites = len(re.findall(r"0x[0-9a-fA-F]+", re.search(r"sites\[\]\s*\{(.*?)\};", _hud, re.S)[1]))
cmds += [[str(b / "xml2_classic_hud_install_test.exe"), EXE, mode] for mode in ["on", "off", "zero", "named-off", "legacy", "invalid", "base", "assets", "alias", "stale"] + [f"guard{i}" for i in range(_hud_sites)]]
# Suites run once per mode beside the exe: their logs need the mode in the tag (else the second run overwrites the first).
installer_modes = {"xml2_xml1_air_carry_test", "xml2_pad_hotplug_test", "xml2_pad_hotplug_live_test"}
for fixture in ("xml2_pad_hotplug_test", "xml2_pad_hotplug_live_test"):
    cmds += [[str(b / (fixture + ".exe")), EXE, mode] for mode in ("1", "0")]
cmds.append([str(b / "xml2_xml1_grab_carry_install_test.exe"), EXE, "both"])
cmds += [[str(b / "xml2_xml1_rogue_decide_install_test.exe"), mode] for mode in ("both","raised")]
cmds += [[str(b / "xml2_xml1_air_carry_test.exe"), EXE, mode] for mode in ("both", "raised")]
cmds.append([str(b / "xml2_shared_handlers_c_test.exe"), EXE, XBE, "--off-proof"])
mode_suites = {"xml2_effect_templates_test": (
    "primitives", "off", "stock", "invalid", "on", "base", "built", "guard", "foreign-model", "allocation", "protect",
    "prim-on", "prim-raised", "prim-stock", "prim-invalid", "prim-built", "prim-guard", "prim-allocation", "prim-protect")}
# Integration (w3/integrated): one set of per-mode tags; the air carry suite stays in it.
installer_modes |= set(mode_suites)
for name,modes in mode_suites.items():
    cmds += [[str(b / (name + ".exe")), EXE, mode] for mode in modes]
cmds += [[str(b / "xml2_effect_pools_test.exe"), EXE, mode] for mode in ("refuse-vector", "refuse-primitive")]
installer_modes |= {"xml2_effect_pools_test"}
# 0.4.2 integration: the XML1 economy fixture runs once per mode (loot/digest's matrix), tagged by mode.
economy_modes = ("A", "1roll", "prices", "shop", "bag", "digest", "off", "zero", "prerequisite", "sidecar", "missingfile", *(f"guard{i}" for i in range(34)), "bag-pre-guard", "prices-pre-guard", "shop-pre-guard", "digest-pre-guard", "legacy-count", "legacy-pick", "rollback", "rollback-failed", "unresolved")
cmds += [[str(b / "xml2_economy_test.exe"), EXE, mode] for mode in economy_modes]
installer_modes |= {"xml2_economy_test"}

if (b / "xml2_e4_core_host.exe").exists():
    cmds.append([sys.executable,"-B",str(R / "scripts/test_e4_core.py"),"--host",str(b / "xml2_e4_core_host.exe"),"--exe",EXE,"--out",str(out / "e4-core")])

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
    if len(cmd)>2 and cmd[2]==str(R / "scripts/test_e4_core.py"):tag="e4-core-driver"
    if len(cmd) > 1 and cmd[1].startswith("--"):
        tag += cmd[1]
    if "durable_parity" in tag:
        tag += "-" + Path(cmd[1]).name
    if len(cmd) > 1 and cmd[1] == EXE:
        tag += "+exe"
    if XBE in cmd:
        tag += "+xbe"
    if len(cmd)>1 and tag.removesuffix("+exe").endswith("_install_test") and cmd[-1] not in (EXE, XBE):
        tag += "-" + cmd[-1]
    if Path(cmd[0]).stem in installer_modes and len(cmd)>2:
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
