"""[Balance] at run time: the stat multiplier, might and rounding keys (and every other [Balance] key) actually install
when DllMain runs, in the synthetic retail-image host (tools/union_host; no game is launched), and the parity keys of
rule 41 (ReceiveRounding / DamageCap) and rule 16 revision 14 (items 19-22) write exactly their own sites.

Each case writes an xml2-fix.ini next to the DLL, runs union_host.exe (DllMain against the retail image laid out at
0x400000) and reads back every byte the DLL changed in XMen2.exe. Checked:
  - the candidate's [Balance] (the X-Men Legends I port's builder output) patches all ten [Balance] sites; the
    strength (0x453b6e), mind (0x453b67, 0x5e0afa) and might/rounding (0x453bc7) calls land inside dinput.dll,
    and the log names the applied numbers (0.1 strength, 0.1 mind, might 0.05, rounding xml1);
  - Rounding=xml1 alone sets up the same three functions;
  - [Balance] absent: none of them, and no rule 41 site;
  - ReceiveRounding=xml1 / DamageCap=xml1: their four sites (0x437a54, 0x437a75, 0x453bfd, 0x453c0c), one more
    agreement rule, nothing else changes;
  - with XML2_FIX_OFFWIRE_REF set to an older build's folder (dinput.dll), the same inis without the new keys write
    the same XMen2.exe addresses in both builds except rule 16's revision-14 (items 19-22) sites (when NpcPowerRule=xml1 is set).

Environment: XML2_FIX_UNION (tools/union_host/prepare_union.py output), XML2_FIX_TEST_EXE (retail XMen2.exe, read
only), XML2_FIX_UNION_HOST (default native-map/union/Release/union_host.exe), XML2_FIX_INI (the candidate's
xml2-fix.ini; default the union folder's run023/xml2-fix.ini with the port's [Balance] added), XML2_FIX_OFFWIRE_REF
(optional). Registry exported before and compared after every run."""
from pathlib import Path
import configparser, json, os, shutil, struct, subprocess

R = Path(__file__).resolve().parent.parent
U = Path(os.environ["XML2_FIX_UNION"])
RETAIL = os.environ["XML2_FIX_TEST_EXE"]
HOST = Path(os.environ.get("XML2_FIX_UNION_HOST") or R / "native-map/union/Release/union_host.exe")
INI = Path(os.environ.get("XML2_FIX_INI") or U / "run023/xml2-fix.ini")
REF = os.environ.get("XML2_FIX_OFFWIRE_REF")
MINE = R / "native-map/bin/Release"
KEY = r"HKCU\Software\Activision\X-Men Legends 2"
out = R / "evidence/balance-install"
out.mkdir(parents=True, exist_ok=True)
(R / "reg").mkdir(exist_ok=True)

PORT_BALANCE = {  # the X-Men Legends I port's builder output (candidate 0.7.0c)
    "EnemyHealthPerBody": "10", "HeroHealthPerBody": "2.5", "HeroStatScale": "4", "NativeStatHeroes": "ProfXAstral,ProfXGladiator",
    "StrengthBonus": "0.1", "MindBonus": "0.1", "MightBonus": "0.05", "PowerAttacks": "xml1", "HitChance": "xml1", "Rounding": "xml1",
    "DamageRolls": "xml1", "MeleeWeapons": "grso_powerbaton:1:15:131072|haarpsoldiermelee:1:9:128",
}
BALANCE_SITES = {0x4b89f7: 6, 0x453b6e: 5, 0x453b67: 5, 0x5e0afa: 5, 0x453bc7: 5, 0x4dc10f: 5, 0x44f684: 7, 0x453add: 5, 0x453a51: 5, 0x4dc37b: 9}
STAT_CALLS = (0x453b6e, 0x453b67, 0x5e0afa, 0x453bc7)  # strength, mind (hit, stats screen), might/rounding: DLL functions
PARITY_SITES = {0x437a54: 6, 0x437a75: 6, 0x453bfd: 4, 0x453c0c: 4}
RULE16_REV12 = {0x50fe63: 6, 0x5187c4: 6, 0x694f1e: 11, 0x511fb8: 4, 0x435b09: 5}


def base_ini():
    cfg = configparser.ConfigParser(strict=False, interpolation=None)
    cfg.optionxform = str
    cfg.read(INI, encoding="utf-8")
    for section in ("TestMode", "Test"):
        if cfg.has_section(section):
            cfg.remove_section(section)
    for option in ("FastTransfer", "Host", "JoinCode", "Broker", "BrokerPin", "Transport", "JoinInProgress"):
        if cfg.has_option("Online", option):
            cfg.remove_option("Online", option)
    for option in ("ClassicHud", "ClassicCodex", "ClassicHudPilot", "ClassicMainBackdrop", "ClassicMainPanelBounds"):
        if cfg.has_option("Game", option):
            cfg.remove_option("Game", option)
    if not cfg.has_section("Game"):
        cfg.add_section("Game")
    cfg["Game"]["SaveFolder"] = "X-Men Legends (balance-install proof)"
    if cfg.has_section("Balance"):
        cfg.remove_section("Balance")
    return cfg


MADE = []


def run(tag, dlldir, balance, npc_power=None):
    folder = (R / "union" / ("balance-" + tag)).resolve()
    if folder.exists():
        shutil.rmtree(folder)
    MADE.append(folder)
    shutil.copytree(U / "run/Data", folder / "Data")
    shutil.copyfile(dlldir / "dinput.dll", folder / "dinput.dll")
    cfg = base_ini()
    if balance is not None:
        cfg["Balance"] = balance
    if npc_power is not None:
        cfg["Game"]["NpcPowerRule"] = npc_power
    elif cfg.has_option("Game", "NpcPowerRule"):
        cfg.remove_option("Game", "NpcPowerRule")
    if npc_power is None and cfg.has_option("Game", "NpcSemiAuto"):
        cfg.remove_option("Game", "NpcSemiAuto")
    with (folder / "xml2-fix.ini").open("w", encoding="utf-8") as handle:
        cfg.write(handle)
    before, after = R / f"reg/balance-{tag}-before.reg", R / f"reg/balance-{tag}-after.reg"
    subprocess.run(["reg", "export", KEY, str(before), "/y"], check=True, capture_output=True)
    tsv = folder / "out.tsv"
    try:
        done = subprocess.run([str(HOST), str(folder / "dinput.dll"), RETAIL, str(U / "engine"), str(tsv)], cwd=str(folder), capture_output=True, text=True, timeout=180)
        assert done.returncode == 0, (tag, done.returncode, (done.stdout or "")[-800:])
    finally:
        subprocess.run(["reg", "import", str(before)], check=True, capture_output=True)
        subprocess.run(["reg", "export", KEY, str(after), "/y"], check=True, capture_output=True)
        assert before.read_bytes() == after.read_bytes(), tag + ": registry changed"
    modules, writes = {}, {}
    for line in tsv.read_text().splitlines():
        cells = line.split("\t")
        if cells[0] == "I":
            modules[cells[1].lower()] = int(cells[2], 16)
        elif cells[0] == "W" and cells[1].lower() == "xmen2.exe":
            writes[int(cells[2], 16)] = (int(cells[3], 16), int(cells[4], 16))
    log = (folder / "xml2-fix.log").read_text(errors="replace") if (folder / "xml2-fix.log").exists() else ""
    keep = [line for line in log.splitlines() if any(w in line.lower() for w in ("balance", "npc power", "semi-auto", "capability record", "unverified", "refus", "failed"))]
    (out / f"{tag}.log").write_text("".join(line + chr(10) for line in keep), encoding="utf-8")
    dll = (folder / "dinput.dll").read_bytes()
    pe = struct.unpack_from("<I", dll, 0x3c)[0]
    size = struct.unpack_from("<I", dll, pe + 0x50)[0]
    base = modules.get("dinput.dll")
    return {"writes": writes, "log": log, "dll": (base, size), "folder": folder}


IMAGE = Path(RETAIL).read_bytes()


def retail(va, size):
    pe = struct.unpack_from("<I", IMAGE, 0x3c)[0]
    count, optional = struct.unpack_from("<H", IMAGE, pe + 6)[0], struct.unpack_from("<H", IMAGE, pe + 20)[0]
    table = pe + 24 + optional
    for i in range(count):
        vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", IMAGE, table + 40 * i + 8)
        rva = va - 0x400000
        if vaddr <= rva and rva + size <= vaddr + max(vsize, rsize):
            return IMAGE[rptr + rva - vaddr: rptr + rva - vaddr + size]
    raise KeyError(hex(va))


def final(result, va, size):
    """The bytes at va after DllMain: the changed ones from the host's list, the others retail."""
    old = retail(va, size)
    return bytes(result["writes"][va + i][1] if va + i in result["writes"] else old[i] for i in range(size))


def written(result, va, size):
    return final(result, va, size) != retail(va, size)


def untouched(result, va, size):
    return not any(va + i in result["writes"] for i in range(size))


def call_target(result, va):
    raw = final(result, va, 5)
    rel = struct.unpack_from("<i", raw, 1)[0]
    return raw[0], (va + 5 + rel) & 0xFFFFFFFF


def unexplained(first, second, expected):
    """Addresses one run changed and the other didn't, outside `expected`. A byte of a pointer or rel32 operand both runs
    write can equal its retail byte in one of them only (the DLL's functions and allocated blocks move between builds
    and between runs): a differing byte next to one both wrote is that, not another site."""
    differ, both = set(first["writes"]) ^ set(second["writes"]), set(first["writes"]) & set(second["writes"])
    return sorted(v for v in differ if v not in expected and not any(v + d in both for d in (-4, -3, -2, -1, 1, 2, 3, 4)))


def rules(log):
    for line in log.splitlines():
        if "capability record verified" in line:
            return int(line.split("protocol")[1].split(",")[1].split()[0])
    return None


report = {}
candidate = run("candidate", MINE, dict(PORT_BALANCE))
base, size = candidate["dll"]
assert base, "dinput.dll not listed by the host"
for va, n in BALANCE_SITES.items():
    assert written(candidate, va, n), f"[Balance] site 0x{va:x} not patched"
for va in STAT_CALLS:
    opcode, to = call_target(candidate, va)
    assert opcode == 0xE8 and base <= to < base + size, f"0x{va:x}: call 0x{to:x} is not into dinput.dll [{base:#x}, {base + size:#x})"
log = candidate["log"]
for words in ("strength bonus 0.1 per point", "mind bonus 0.1 per point", "might multiplies every hit by 1 + 0.05 per rank", "Rounding=xml1", "10 sites patched"):
    assert words in log, ("missing log line", words)
for va, n in {**PARITY_SITES}.items():
    assert untouched(candidate, va, n), f"0x{va:x} patched without ReceiveRounding/DamageCap"
report["candidate"] = {"sites": len(BALANCE_SITES), "stat_calls_into_dll": len(STAT_CALLS), "rules": rules(log)}

only_rounding = run("rounding", MINE, {"Rounding": "xml1"})
for va in STAT_CALLS:
    assert written(only_rounding, va, 5), f"Rounding=xml1 alone: 0x{va:x} not patched"
for va in (0x4b89f7, 0x4dc10f, 0x44f684, 0x453add, 0x453a51, 0x4dc37b):
    assert untouched(only_rounding, va, BALANCE_SITES[va]), f"Rounding=xml1 alone patched 0x{va:x}"
report["rounding_only"] = "strength, mind (2), might/rounding patched; nothing else of [Balance]"

off = run("off", MINE, None)
for va, n in {**BALANCE_SITES, **PARITY_SITES}.items():
    assert untouched(off, va, n), f"[Balance] absent: 0x{va:x} patched"
report["off"] = {"rules": rules(off["log"]), "balance_writes": 0}

parity = run("parity", MINE, {**PORT_BALANCE, "ReceiveRounding": "xml1", "DamageCap": "xml1"})
for va, n in PARITY_SITES.items():
    assert written(parity, va, n), f"0x{va:x} not patched with ReceiveRounding/DamageCap"
allowed = {va + i for va, n in PARITY_SITES.items() for i in range(n)}
extra = sorted(set(parity["writes"]) - set(candidate["writes"]))
odd = unexplained(candidate, parity, allowed)
assert not odd, [hex(v) for v in odd]
cap_a, cap_b = struct.unpack("<I", final(parity, 0x453bfd, 4))[0], struct.unpack("<I", final(parity, 0x453c0c, 4))[0]
assert cap_a == cap_b and cap_a != 0x685bf4, "both cap operands point at the same new constant"
assert final(parity, 0x437a54, 6)[0] == 0xE8 and final(parity, 0x437a54, 6)[5] == 0x90 and final(parity, 0x437a75, 6)[0] == 0xE8 and final(parity, 0x437a75, 6)[5] == 0x90
opcode, to = call_target(parity, 0x437a54)
assert opcode == 0xE8 and not (base <= to < base + size), "the receive load stub is the module's own block"
r_on, r_off = rules(parity["log"]), rules(candidate["log"])
if r_on is not None and r_off is not None:
    assert r_on == r_off + 1, (r_on, r_off)
assert "ReceiveRounding=xml1" in parity["log"] and "DamageCap=xml1" in parity["log"]
report["parity"] = {"sites": sorted(hex(v) for v in PARITY_SITES), "rules": r_on, "extra_bytes": len(extra)}

LATE_PAD_BRIDGE = 0x61862A
GAMES_LIST_NOTICE = {0x5b9a2b: 5, 0x5b9ba7: 5, 0x69f8e4: 4}
if REF:
    ref = Path(REF)
    for tag, npc in (("noai", None), ("ai", "xml1")):
        old = run("ref-" + tag, ref, dict(PORT_BALANCE), npc)
        new = run("new-" + tag, MINE, dict(PORT_BALANCE), npc)
        differ = set(old["writes"]) ^ set(new["writes"])
        expected = {va + i for va, n in RULE16_REV12.items() for i in range(n)} if npc else set()
        # the late-controller fix (no ini key, on in every build since 5c3b920) bridges the Advanced Options handler
        # 0x618390 at 0x61862A with jmp rel32 + nop; the beta 7 reference has no such site
        expected |= {LATE_PAD_BRIDGE + i for i in range(6)}
        # the games-list refusal notice (082g; an optional part of the online agreement, no ini key): the two empty-row
        # string lookups of CMenuGamesList's refresh CALL its thunk and its update slot (vt+0x30) points at its wrapper;
        # the beta 7 reference has none of them
        expected |= {va + i for va, n in GAMES_LIST_NOTICE.items() for i in range(n)}
        assert final(new, 0x5b9a2b, 1)[0] == 0xE8 and final(new, 0x5b9ba7, 1)[0] == 0xE8, (tag, "games-list notice CALLs")
        bridge = final(new, LATE_PAD_BRIDGE, 6)
        assert bridge[0] == 0xE9 and bridge[5] == 0x90, (tag, bridge.hex())
        odd = unexplained(old, new, expected)
        assert not odd, (tag, [hex(v) for v in odd[:20]])
        if npc:
            assert final(new, 0x50fe63, 6) == bytes([0xEB, 4, 0x90, 0x90, 0x90, 0x90]) and final(new, 0x5187c4, 6) == bytes([0xEB, 4, 0x90, 0x90, 0x90, 0x90])
            assert final(new, 0x694f1e, 11) == b"dvanced" + bytes(4) and final(new, 0x511fb8, 4) == bytes([0x89, 0x7C, 0x24, 0x68])
            assert final(new, 0x435b09, 5) == bytes([0xE9, 0x44, 0, 0, 0]) and final(old, 0x50fe63, 6) == retail(0x50fe63, 6)
        report["vs_reference_" + tag] = {"addresses_old": len(old["writes"]), "addresses_new": len(new["writes"]), "differing": len(differ), "unexplained": 0}

for folder in MADE:
    shutil.rmtree(folder)
(out / "report.json").write_text(json.dumps(report, indent=1))
print(json.dumps(report, indent=1))
print("PASS balance install")
