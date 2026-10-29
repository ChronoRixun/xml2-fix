"""Generate the companion builder's authored Guard node and execute the native fixture.

No owned-input build, game, desktop input or network. The output stays in --out.
"""
import argparse
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--lc-root', type=Path, required=True)
ap.add_argument('--host', type=Path, required=True)
ap.add_argument('--exe', type=Path, required=True)
ap.add_argument('--out', type=Path, required=True)
args = ap.parse_args()
sys.path.insert(0, str(args.lc_root.resolve() / 'tools'))
import xmlb
from xml1build import guard_block

args.out.mkdir(parents=True, exist_ok=True)
node = guard_block.make_node()
assert not guard_block.problems(node)
content = args.out.resolve() / 'guardblocking.xmlb'
content.write_bytes(xmlb.encode(node))
result = subprocess.run([str(args.host.resolve()), str(args.exe.resolve()), str(content)],
                        capture_output=True, text=True, creationflags=0x08004000, timeout=90)
(args.out / 'native.log').write_text(result.stdout + result.stderr, encoding='utf-8')
print(result.stdout, end='')
raise SystemExit(result.returncode)
