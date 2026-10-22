"""Prove OFF is byte-identical to 0.2.3f in the synthetic retail-image host; no game is launched."""
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
    # XML2 Fix 1.7.1 made [Game] StunRule=xml1 rule 18 revision 3 on purpose (0.2.5 OFF proof, rel-025 off_proof.py:
    # only rule 18's bucket differs); the transport's OFF claim is compared without that unrelated rule.
    if cfg.has_option("Game", "StunRule"):
        cfg.remove_option("Game", "StunRule")
        rules -= 1
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

mine = R / "native-map/bin/Release"
ref = capture("023f-off", REF, setter(), 23)
for transport in (None, "", "stock", "0"):
    raw = capture(f"gns-off-{transport}", mine, setter(transport=transport), 23)
    assert raw == ref, transport
    print("Transport", repr(transport), "= 0.2.3f record", records[f"gns-off-{transport}"]["sha256"], flush=True)
ref24 = capture("023f-fast", REF, setter(fast=True), 24)
raw = capture("gns-off-fast", mine, setter(fast=True), 24)
assert raw == ref24
print("FastTransfer on, Transport absent = 0.2.3f", records["gns-off-fast"]["sha256"], flush=True)
on = capture("gns-on", mine, setter(transport="gns"), 24)
assert on != ref
print("Transport=gns differs (24 rules)", records["gns-on"]["sha256"], flush=True)
on2 = capture("gns-on-fast", mine, setter(fast=True, transport="gns"), 25)
assert on2 != ref24
(R / "evidence/off-wire-compatibility.json").write_text(json.dumps({"method": __doc__, "records": records}, indent=1))
print("Transport OFF wire compatibility PASS")
