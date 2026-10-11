"""V-TBD (number assigned at merge): run the actual pinned capture/restore regression."""
from pathlib import Path
import argparse
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=root.parent / "xbuild")
parser.add_argument("--config", default="Release")
args = parser.parse_args()
exe = args.build.resolve() / "bin" / args.config / "lcsave_conversation_packages_test.exe"
if not exe.is_file():
    parser.error("build lcsave_conversation_packages_test before running this gate")
wiring = subprocess.run([__import__("sys").executable, str(root / "test/test_capture_hook_wiring.py")], timeout=30)
if wiring.returncode:
    raise SystemExit(wiring.returncode)
result = subprocess.run([str(exe)], cwd=exe.parent, timeout=90)
raise SystemExit(result.returncode)
