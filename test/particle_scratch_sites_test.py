"""All-src literal guard and region overlap gate for W3-9 (read-only)."""
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from generate_particle_scratch_sites import ROOT, derive, render

rows = derive(Path(sys.argv[1]), True)
assert (ROOT/'src/particle_scratch_sites.hpp').read_text(encoding='utf-8') == render(rows)
spans = [(va, va+len(raw)) for _, va, raw in rows]
literal = re.compile(r'0x([0-9a-fA-F]{6,8})\s*,\s*(?:patch_transaction::bytes_of\(\s*)?"([0-9a-fA-F]+)"')
regions = re.compile(r'\{\s*"[^"\n]+"\s*,\s*0x([0-9a-fA-F]{6,8})\s*,\s*0x([0-9a-fA-F]{6,8})\s*,')
overlaps=[]
guards=areas=0
for file in sorted((ROOT/'src').rglob('*')):
    if not file.is_file() or file.name in ('particle_scratch_sites.hpp', 'particle_scratch.cpp'):
        continue
    text=file.read_text(encoding='utf-8', errors='replace')
    for match in literal.finditer(text):
        guards+=1
        lo=int(match[1],16); hi=lo+len(match[2])//2
        if any(lo < b and a < hi for a,b in spans):
            overlaps.append((str(file.relative_to(ROOT)),hex(lo),hex(hi)))
    for match in regions.finditer(text):
        areas+=1
        lo,hi=(int(x,16) for x in match.groups())
        if any(lo < b and a < hi for a,b in spans):
            overlaps.append((str(file.relative_to(ROOT)),hex(lo),hex(hi),'region'))
# Integration (w3/integrated): the pool diagnostics' eleven per-operation particle observers sit on the same count
# calls. They stand down when W3-9 is installed (pool_counters.cpp: the bytes are no longer retail) and hook the retail
# bytes when it is not, so these are the only accepted pairs: one per span, from pool_counter_sites.hpp.
standing=[o for o in overlaps if o[0].endswith('pool_counter_sites.hpp')]
assert len(standing)==len(spans) and all(any(lo < int(o[2],16) and int(o[1],16) < hi for o in standing) for lo,hi in spans), standing
overlaps=[o for o in overlaps if o not in standing]
assert not overlaps, overlaps
print(f'PASS all-src guard-vs-site scan: {guards} literal guards, {areas} regions; zero overlaps with eleven W3-9 spans')
