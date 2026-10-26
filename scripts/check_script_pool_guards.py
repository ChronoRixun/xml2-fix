"""Scan every source hex guard against the W3-8 write ledger (no game needed)."""
from pathlib import Path
import json
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
header=(ROOT/'src/script_pool_sites.hpp').read_text(encoding='utf-8')
sites=[(int(a,16),len(bytes.fromhex(h))) for a,h in re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"',header)]
assert len(sites)==300
guard_rx=re.compile(r'\{\s*(0x[0-9a-fA-F]{5,8}|[A-Za-z_]\w*)\s*,\s*((?:"[0-9a-fA-F]+"\s*)+)')
const_rx=re.compile(r'\b([A-Za-z_]\w*)\s*=\s*(0x[0-9a-fA-F]{5,8})\b')
overlaps=[];count=0;files=0
for path in sorted((ROOT/'src').rglob('*')):
    if path.suffix not in ('.cpp','.hpp','.h','.c'):continue
    if path.name=='script_pool_sites.hpp':continue
    files+=1;text=path.read_text(encoding='utf-8');consts={k:int(v,16) for k,v in const_rx.findall(text)}
    for m in guard_rx.finditer(text):
        a=m.group(1);va=int(a,16) if a.startswith('0x') else consts.get(a)
        if va is None:continue
        size=len(''.join(re.findall(r'"([0-9a-fA-F]+)"',m.group(2))))//2;count+=1
        for start,n in sites:
            if va<start+n and start<va+size:
                overlaps.append(dict(file=str(path.relative_to(ROOT)),line=text.count('\n',0,m.start())+1,guard=hex(va),site=hex(start)))
result=dict(source_files=files,foreign_guards=count,script_sites=len(sites),overlaps=overlaps)
if '--json' in sys.argv:
    Path(sys.argv[sys.argv.index('--json')+1]).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n')
print(f'{"FAIL" if overlaps else "PASS"} every-src guard scan: {count} guards in {files} files, {len(sites)} ScriptPools sites, {len(overlaps)} overlaps')
for row in overlaps:print(row)
raise SystemExit(bool(overlaps))
