"""Prepare the union host's folder for scripts/prove_off_wire.py and prove_combined_install.py (XML2_FIX_UNION).

Usage: python prepare_union.py <union folder> <game build folder> <base xml2-fix.ini>
  <game build folder>  an installed Legends Classic build: its engine DLLs (every *.dll except dinput.dll) go to
                       <union>/engine, its Data folder to <union>/run/Data (the DLL reads Data next to itself)
  <base xml2-fix.ini>  the 0.2.3f candidate's xml2-fix.ini (the OFF proof's reference configuration)
<union>/run023/xml2-fix.ini = the base ini with every gameplay switch of the 0.2.3 write-union run on, no [Test] or
[TestMode] section and its own save folder. Nothing outside <union> is written; the game build is only read."""
import configparser, shutil, sys
from pathlib import Path

union, build, base = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
assert (build / "XMen2.exe").exists() and (build / "Data").is_dir(), build
(union / "engine").mkdir(parents=True, exist_ok=True)
for dll in build.glob("*.dll"):
    if dll.name.lower() != "dinput.dll":
        shutil.copyfile(dll, union / "engine" / dll.name)
shutil.copytree(build / "Data", union / "run" / "Data", dirs_exist_ok=True)
cfg = configparser.ConfigParser(strict=False)
cfg.optionxform = str
cfg.read(base, encoding="utf-8")
for section in ("Test", "TestMode"):
    cfg.remove_section(section)
game = cfg["Game"]
game.update({"NpcPowerRule": "xml1", "StunRule": "xml1", "BoxHarmOrigin": "xml1", "HarmSingleChain": "single",
             "DangerRoomHealthBars": "xml1", "ObjectWorldImpact": "xml1", "DangerRoomParty": "xml1",
             "Xml1LockOnEffects": "invented/beam,invented/surface,invented/target", "Xml1LockOnAim": "1",
             "SaveFolder": "union-host"})
(union / "run023").mkdir(exist_ok=True)
with (union / "run023" / "xml2-fix.ini").open("w", encoding="utf-8") as handle:
    cfg.write(handle)
print("union prepared:", union)
