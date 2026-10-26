"""0.3.0 integration: ReceiveValidation + Transport=gns + FastTransfer + JoinInProgress install together in the synthetic
retail-image host (the GNS signature check must accept the receive-validation dispatch jump); no game is launched."""
from pathlib import Path
import subprocess, shutil, configparser, json, hashlib, os

R = Path(__file__).resolve().parent.parent
# XML2_FIX_UNION: a folder made by tools/union_host/prepare_union.py; XML2_FIX_OFFWIRE_REF: the 0.2.3f reference
# build's folder (dinput.dll + dinput.map); XML2_FIX_TEST_EXE: a retail XMen2.exe (read only); XML2_FIX_UNION_HOST:
# union_host.exe (default: this build tree's native-map/union/Release/union_host.exe).
U = Path(os.environ["XML2_FIX_UNION"])
REF = Path(os.environ["XML2_FIX_OFFWIRE_REF"])
RETAIL = os.environ["XML2_FIX_TEST_EXE"]
HOST = Path(os.environ.get("XML2_FIX_UNION_HOST") or R / "native-map/union/Release/union_host.exe")
out = R / "evidence/off-wire"
out.mkdir(parents=True, exist_ok=True)
(R / "reg").mkdir(exist_ok=True)
key = r"HKCU\Software\Activision\X-Men Legends 2"
records = {}

def capture(tag, dlldir, edit, rules):
    folder = R / "union" / ("wire-" + tag)
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copytree(U / "run/Data", folder / "Data", dirs_exist_ok=True)
    shutil.copyfile(dlldir / "dinput.dll", folder / "dinput.dll")
    rows = (dlldir / "dinput.map").read_text().splitlines()
    matches = [line for line in rows if "?capabilities@" in line and "@gameplay_agreement@@3Vrecord@gameplay_capabilities@@A" in line]
    assert len(matches) == 1, matches
    rva = int(matches[0].split()[-2], 16) - 0x10000000
    cfg = configparser.ConfigParser()
    cfg.optionxform = str
    cfg.read(U / "run023/xml2-fix.ini")
    cfg.remove_section("TestMode")
    cfg["Game"]["SaveFolder"] = "X-Men Legends (net030harden-off-wire)"
    for option in ("FastTransfer", "Host", "JoinCode", "Broker", "BrokerPin"):
        if cfg.has_option("Online", option):
            cfg.remove_option("Online", option)
    edit(cfg)
    with (folder / "xml2-fix.ini").open("w") as handle:
        cfg.write(handle)
    before = R / f"reg/wire-{tag}-before.reg"
    after = R / f"reg/wire-{tag}-after.reg"
    subprocess.run(["reg", "export", key, str(before), "/y"], check=True, capture_output=True)
    try:
        cmd = [HOST, folder / "dinput.dll", RETAIL, U / "engine", out / (tag + ".tsv"), hex(rva), out / (tag + ".bin")]
        completed = subprocess.run(list(map(str, cmd)), cwd=folder, capture_output=True, text=True, timeout=120)
        assert completed.returncode == 0, (tag, completed.returncode, completed.stdout[-500:], completed.stderr[-500:])
    finally:
        subprocess.run(["reg", "import", str(before)], check=True, capture_output=True)
        subprocess.run(["reg", "export", key, str(after), "/y"], check=True, capture_output=True)
        assert before.read_bytes() == after.read_bytes(), tag
    full_log = (folder / "xml2-fix.log").read_text()
    log = "\n".join(line for line in full_log.splitlines() if any(word in line for word in ("capability", "protocol 4", "Transport", "GNS", "FastTransfer"))) + "\n"
    (out / (tag + ".log")).write_text(log)
    assert f"108 bytes, protocol 4, {rules} rules" in log, (tag, [line for line in log.splitlines() if "capability" in line or "gns" in line.lower()])
    raw = (out / (tag + ".bin")).read_bytes()
    assert len(raw) == 108 and raw[:6] == b"GCAP\x04\x00"
    records[tag] = {
        "sha256": hashlib.sha256(raw).hexdigest(),
        "rules": rules,
        "dll_sha256": hashlib.sha256((folder / "dinput.dll").read_bytes()).hexdigest(),
        "gns_log": [line[15:] for line in log.splitlines() if "Transport" in line or "GNS" in line],
    }
    (out / (tag + ".tsv")).unlink(missing_ok=True)
    (out / (tag + ".bin")).unlink(missing_ok=True)
    return raw

def setter(fast=False, transport=None):
    def edit(cfg):
        if fast:
            cfg["Online"]["FastTransfer"] = "1"
        if transport is not None:
            cfg["Online"]["Transport"] = transport
    return edit


def jip(cfg):
    cfg["Online"]["FastTransfer"]="1"; cfg["Online"]["Transport"]="gns"; cfg["Online"]["JoinInProgress"]="1"
mine = R / "native-map/bin/Release"
# 32 rules since XML2 Fix 1.9.0-beta.1: run023's six non-stock [Limits] (ActorSlots, ResourceNames, ItemEnhancements,
# FightStyles, EffectCurves, MoveTriggers) are agreement rules 32-37 now (limits wave 1); 26 before.
capture("gns-jip", mine, jip, 32)
log = (R / "union/wire-gns-jip/xml2-fix.log").read_text()
for needle in ("receive validation installed", "Transport=gns installed", "JoinInProgress installed", "32 rules"):
    assert needle in log, needle
assert "refused" not in log.split("startup transaction")[-1].lower().replace("save refused", ""), "a subsystem refused"
print("combined install PASS (receive validation, GNS, FastTransfer, join in progress, 32 rules)")
