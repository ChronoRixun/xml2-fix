"""Run the native startup test on an isolated desktop and prove the game registry is byte-identical afterward."""
from pathlib import Path
import subprocess, sys

import os as _os

def _need(name):
    """Machine-specific inputs come from the environment (see scripts/README-gates.md); nothing local is hardcoded."""
    value = _os.environ.get(name)
    if not value:
        raise SystemExit(f"set {name} (see scripts/README-gates.md)")
    return value

R = Path(__file__).resolve().parent.parent
T = Path(_need("XML2_FIX_STARTUP_RUNNER_DIR"))
key = r"HKCU\Software\Activision\X-Men Legends 2"
logs = R / "logs"
logs.mkdir(exist_ok=True)
backup = logs / "startup-before.reg"
after = logs / "startup-after.reg"

def run(*args):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True, text=True)

run("reg", "export", key, backup, "/y")
try:
    result = run(sys.executable, T / "tools/run_isolated_native.py", R / "native/bin/Release/xml2_save_startup_test.exe", logs / "startup.log")
    print(result.stdout, end="")
finally:
    run("reg", "import", backup)
    run("reg", "export", key, after, "/y")
assert backup.read_bytes() == after.read_bytes(), "startup registry changed"
print("startup registry identical")
