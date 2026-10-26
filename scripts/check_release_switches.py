"""Fail if a Release DLL retains network development-only option names."""
from pathlib import Path
import sys

path = Path(sys.argv[1] if len(sys.argv) > 1 else "native/bin/Release/dinput.dll")
data = path.read_bytes()
names = [
    "OnlineCert",
    "OnlineCertFile",
    "OnlineIdentity",
    "GnsFakePacketLoss",
    "GnsFakePacketLag",
    "NetFakeLoss",
    "NetFakeLag",
    "NetFakeJitter",
    "GnsDelayTo",
    "GnsDelayMs",
    "GnsDelayTrigger",
    "LostCutLegacy",
    "MeshDeferMs",
    "MeshReplyLegacy",
]
found = [name for name in names if name.encode() in data or name.encode("utf-16le") in data]
for name in found:
    print(f"FAIL release DLL retains development option {name}")
if not found:
    print(f"PASS no network development option strings in {path}")
raise SystemExit(bool(found))
