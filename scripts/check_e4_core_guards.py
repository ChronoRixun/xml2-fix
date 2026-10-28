"""Scan every DLL literal guard against the Stage 2 write ledger.

Known overlaps must either join the same raw startup expectation union, or use
the descriptor/observer guard composer. Every matched guard is checked against
the owned retail bytes; no unexplained overlap is accepted.
"""
import argparse
import json
from pathlib import Path
import re
import pefile
from e4_core_sites import generate

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True);p.add_argument('--db',required=True);p.add_argument('--out');a=p.parse_args()
    ledger=generate(a.exe,a.db);image=pefile.PE(a.exe).get_memory_mapped_image()
    spans=[(r['va'],r['va']+len(r['hex'])//2) for r in ledger['edits']]
    spans += [(r['va'],r['va']+len(r['head'])//2) for r in ledger['specials']]
    # Core always carries these observation detours. Team-free's two probes
    # are emitted manually by its replacement; rule16's one optional elision
    # remains included here so both requested and absent profiles are audited.
    observations=(ROOT/'src/e4_observe_sites.hpp').read_text(encoding='utf-8')
    observer_spans=set()
    for lo,point,hi,kind in re.findall(r'\{"[^"]+",0x([0-9a-f]+),0x([0-9a-f]+),0x([0-9a-f]+),kind::(\w+)',observations):
        if kind not in ('team_free','team_free_done'):observer_spans.add((int(lo,16),int(hi,16)))
    assert len(observer_spans)==112
    spans+=sorted(observer_spans)
    literal=re.compile(r'\{\s*(0x[0-9a-fA-F]{5,8}|[A-Za-z_]\w*)\s*,\s*((?:"[0-9a-fA-F]+"\s*)+)')
    constants=re.compile(r'\b([A-Za-z_]\w*)\s*=\s*(0x[0-9a-fA-F]{5,8})\b')
    same_union={'entity_capacity_sites.hpp','e4_observe_sites.hpp'}
    adapters={'npc_power_rules.hpp':'npc_power_rules.hpp','npc_semi_auto_rules.hpp':'npc_semi_auto.cpp','mold_controller.cpp':'mold_controller.cpp','xml1_turrets.cpp':'xml1_turrets.cpp'}
    overlaps=[];unknown=[];count=0;files=0
    for path in sorted((ROOT/'src').rglob('*')):
        if path.suffix not in ('.cpp','.hpp'):continue
        files+=1;text=path.read_text(encoding='utf-8');known={k:int(v,16) for k,v in constants.findall(text)}
        for match in literal.finditer(text):
            label=match[1];va=int(label,16) if label.startswith('0x') else known.get(label)
            if va is None:continue
            raw=bytes.fromhex(''.join(re.findall(r'"([0-9a-fA-F]+)"',match[2])));count+=1
            if not any(va<hi and lo<va+len(raw) for lo,hi in spans):continue
            assert image[va-0x400000:va-0x400000+len(raw)]==raw,('foreign guard does not describe retail',str(path),hex(va))
            if path.name in same_union:owner='same protected startup expectation union'
            elif path.name in adapters:
                implementation=(ROOT/'src'/adapters[path.name]).read_text(encoding='utf-8')
                assert 'actor_ai::guard(' in implementation,(path,'descriptor composer missing')
                owner='descriptor / certified observer guard composer'
            else:owner='UNEXPLAINED';unknown.append((path.name,hex(va)))
            overlaps.append(dict(file=path.relative_to(ROOT).as_posix(),line=text.count('\n',0,match.start())+1,va=hex(va),bytes=len(raw),owner=owner))
    report=dict(files=files,guards=count,core_operand_rows=len(ledger['edits']),observer_spans=len(observer_spans),overlaps=overlaps,unexplained=unknown)
    if a.out:Path(a.out).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(f'E4 guard scan: {files} files, {count} literal guards/sites, {len(overlaps)} accounted overlaps, {len(unknown)} unexplained')
    for item in unknown:print(item)
    assert not unknown,'unexplained core guard overlap'


if __name__=='__main__':main()
