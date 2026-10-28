"""Production-DLL economy admission/installation gate; no game launches.

Arguments: union_host.exe DLL-directory retail.exe engine-directory output-directory.
The DLL directory must contain dinput.dll and its /MAP dinput.map. All copies,
invented sidecars and registry snapshots stay in the output directory. Discord
is disabled, the synthetic host uses a private desktop and the registry is
read/exported only (before/after must match).
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

from prove_w2_step3_off import symbols


def main():
    host, dll, retail, engine, output = [Path(p).resolve() for p in sys.argv[1:]]
    entries = symbols(dll / "dinput.map")

    def rva(prefix):
        found = [a for a, n in entries if n.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0] - 0x10000000

    capability = rva("?capabilities@")
    status = rva("?status@xml1_economy@@")
    describe = rva("?describe@record@gameplay_capabilities@@")
    output.mkdir(parents=True, exist_ok=True)
    # A deliberately malformed peer is sufficient: an unready local record
    # MUST return unavailable before inspecting any peer bytes (not malformed).
    peer = output / "probe-peer.bin"
    peer.write_bytes(bytes(108))
    key = r"HKCU\Software\Activision\X-Men Legends 2"
    for case in ("no-w2", "missing-sidecar", "pending-table"):
        folder = output / case
        folder.mkdir(exist_ok=True)
        shutil.copyfile(dll / "dinput.dll", folder / "dinput.dll")
        config = "[Game]\nXml1Economy=1\n[Discord]\nEnabled=0\n"
        if case != "no-w2":
            config += "[Save]\nFormat=lc\n[Limits]\nEntityCapacity=1\nSaveLayout=w2\n"
        (folder / "xml2-fix.ini").write_text(config, encoding="utf-8", newline="\n")
        if case == "pending-table":
            (folder / "Data").mkdir(exist_ok=True)
            (folder / "Data/x1economy.txt").write_text(
                "x1economy 1\ncount 0\nsha256 "
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n",
                encoding="ascii", newline="\n")
        elif case == "missing-sidecar":
            assert not (folder / "Data/x1economy.txt").exists(), "missing-file fixture already has a sidecar"
        before, after = folder / "registry-before.reg", folder / "registry-after.reg"
        assert subprocess.run(["reg", "export", key, str(before), "/y"], capture_output=True).returncode == 0
        env = os.environ.copy()
        env.update(UNION_PRIVATE_DESKTOP="1", UNION_STATUS_RVA=hex(status),
                   UNION_PEER_WIRE=str(peer), UNION_DESCRIBE_RVA=hex(describe))
        result = subprocess.run([
            str(host), str(folder / "dinput.dll"), str(retail), str(engine),
            str(folder / "writes.tsv"), hex(capability), str(folder / "wire.bin"), "108"],
            cwd=folder, env=env, capture_output=True, text=True,
            creationflags=0x08004000, timeout=180)
        (folder / "host.log").write_text(result.stdout + result.stderr, encoding="utf-8", newline="\n")
        assert result.returncode == 0, (case, result.returncode, result.stdout)
        assert subprocess.run(["reg", "export", key, str(after), "/y"], capture_output=True).returncode == 0
        assert before.read_bytes() == after.read_bytes(), "synthetic host changed the registry"
        assert "union agreement: result=1" in result.stdout, (case, "local record was not unavailable")
        log = (folder / "xml2-fix.log").read_text(errors="replace")
        expected = {"no-w2": "requires verified rule 47", "missing-sidecar": "line 1: cannot open sidecar",
                    "pending-table": "awaiting Data/items name resolution"}[case]
        assert expected in log, (case, expected, log)
        writes = {}
        for line in (folder / "writes.tsv").read_text().splitlines():
            fields = line.split("\t")
            if fields[0] == "W" and fields[1] == "XMen2.exe":
                writes[int(fields[2], 16)] = int(fields[4], 16)
        assert (0x47bb50 in writes) == (case == "pending-table"), (case, "constructor hook state")
        assert any(a in writes for a in range(0x4806ae, 0x4806b2)) == (case == "pending-table"), (case, "loader hook state")
        for site in (0x481170, 0x480dd8, 0x5a9b8d, 0x5d3ee9, 0x5d404b, 0x5d2639, 0x5d3720):
            assert (site in writes) == (case == "pending-table"), (case, hex(site), "prices hook state")
        for operand in (0x5a9cd8, 0x5a9d2c):
            assert any(a in writes for a in range(operand, operand + 4)) == (case == "pending-table"), (case, "sale fraction hook state")
        for site, length in ((0x5d39c0, 7), (0x5d3ba0, 6), (0x5d3c31, 6), (0x5d3c81, 6),
                             (0x5d3c97, 6), (0x5d3dd7, 5), (0x5d4135, 5), (0x5d3fda, 5)):
            assert any(a in writes for a in range(site, site + length)) == (case == "pending-table"), (case, hex(site), "digest hook state")
        for site, length in ((0x49e64b, 7), (0x46a1a0, 6), (0x5d2a55, 7), (0x5d2b18, 8),
                             (0x5d35d0, 9), (0x5d36c8, 15), (0x5d3a34, 5), (0x5d3b2a, 8),
                             (0x5d2b70, 5), (0x5d3ff0, 9), (0x5d4104, 5), (0x5d4119, 5),
                             (0x5d412e, 5), (0x480e20, 5)):
            assert any(a in writes for a in range(site, site + length)) == (case == "pending-table"), (case, hex(site), "shop hook state")
        assert "econ on; plan A; defs 0; digest " in result.stdout
        assert "; forge 0/G0; gen 0; refused 0" in result.stdout
        if case == "pending-table":
            assert "digest e3b0c442" in result.stdout
        print(f"PASS {case}: production DLL, hook state, closed agreement, status, unchanged registry", flush=True)
    print("Economy production startup: 3 scenarios passed")


if __name__ == "__main__":
    main()
