"""T1/T2: compare generated roles to the independent stage-0 audit, row for row.

The audit is an external input, not an input to the generator. Run with --audit.
"""
import argparse
import json
import re
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from entity_capacity_b_sites import layout


def oracle(text):
    expected = {}
    def record(pool, role, field, addresses):
        expected[(pool, role, field)] = {int(x,16) for x in re.findall(r'\b[0-9a-f]{6}\b',addresses)}
    cap = text[text.index('The following is the corrected'):text.index('For **A7**')]
    for p in ('A7','A8','A9'):
        line = next(x for x in cap.splitlines() if f'**{p} ' in x)
        cells = line.split('|')
        record(p,'capacity','*',cells[2]); record(p,'seed','last',cells[3])
    for p,nxt in [('A7','For **A9**'),('A9','For **A8**'),('A8','**The two meanings')]:
        block = text[text.index(f'For **{p}**'):text.index(nxt)]
        for line in block.splitlines():
            if not line.startswith('|') or 'sites' in line.lower() or '---' in line:
                continue
            cells=line.split('|'); label=cells[1].lower()
            field=next((f for f in ('constructed','ring','enqueue','dequeue','free','active','live','allocation') if f in label),None)
            if p=='A8':
                old=re.search(r'`([0-9a-f]+) ',label)
                if not old: continue
                v=int(old[1],16)
                field={0x904:'ring',0x988:'enqueue',0x98c:'dequeue',0x990:'free',0x994:'active',0x998:'live',0x99c:'outer_active' if 'outer' in label else 'generations',0x9a0:'adjacent_9a0',0x9a4:'adjacent_9a4',0xa1c:'mask',0xa20:'shift',0xa24:'adjacent_a24'}[v]
            if 'absolute' in label:
                record(p,'absolute','live' if 'live' in label else 'base',cells[2])
            elif field:
                record(p,'heap' if field=='allocation' else 'field','size' if field=='allocation' else field,cells[2])
    block=text[text.index('The trailing shadow-field'):text.index('Widen active clears')]
    field=None
    for line in block.splitlines():
        m=re.match(r'([0-9a-f]{3})\s+(.*)',line)
        if m:
            field='trailing_'+m[1]; record('A8','trailing',field,m[2])
        elif field and re.match(r'\s+[0-9a-f]{6}',line):
            expected[('A8','trailing',field)].update(int(x,16) for x in re.findall(r'\b[0-9a-f]{6}\b',line))
    for p,fields in {'A7':{'constructed':'4967b4','active':'4967cc,496812','constructed_indirect':'49505c'},'A8':{'active':'4b69a0,4b6bed'},'A9':{'constructed':'499ad5','active':'499aed','constructed_indirect':'497afc'}}.items():
        for field,addresses in fields.items(): record(p,'clear',field,addresses)
    record('A8','heap','size','4b6dce')
    record('AI','guard','team','50e1c2,50e217,50e3c8,50e3e6,50e4aa')
    return expected


def main():
    a=argparse.ArgumentParser(); a.add_argument('--audit',required=True); args=a.parse_args()
    data=json.loads((Path(__file__).parent/'actor_pools_sites.json').read_text())
    expected=oracle(Path(args.audit).read_text(encoding='utf-8'))
    actual={}
    for r in data['rows']:
        key=(r['pool'],r['role'],'*' if r['role']=='capacity' else r['field'])
        actual.setdefault(key,set()).add(r['va'])
    errors=0
    for key in sorted(expected.keys() | actual.keys()):
        missing=expected.get(key,set())-actual.get(key,set()); extra=actual.get(key,set())-expected.get(key,set())
        if missing or extra:
            print(key,'missing',[hex(x) for x in sorted(missing)],'extra',[hex(x) for x in sorted(extra)]); errors+=1
    for p,n in [('A7',10),('A8',27),('A9',10)]:
        assert sum(r['pool']==p and r['role']=='capacity' and r['size']==1 for r in data['rows'])==n, p
    assert sum(r['pool']=='A9' and r['role']=='absolute' for r in data['rows'])==9
    # Rebuild stock layouts from constructor offsets, including the reserved shadow dword.
    assert layout(32,0x6a8)==dict(constructed=0xd500,ring=0xd504,enqueue=0xd588,dequeue=0xd58c,free=0xd590,active=0xd594,live=0xd598,extent=0xd59c)
    assert layout(32,0x6c)==dict(constructed=0xd80,ring=0xd84,enqueue=0xe08,dequeue=0xe0c,free=0xe10,active=0xe14,live=0xe18,extent=0xe1c)
    assert layout(32,0x48,True)==dict(constructed=0x900,ring=0x904,enqueue=0x988,dequeue=0x98c,free=0x990,active=0x994,live=0x998,generations=0x99c,mask=0xa1c,shift=0xa20,extent=0xa24)
    assert not errors, f'{errors} differing audit rows'
    print('PASS T1: every audit field/role/address equals generated set; imm8 10/27/10; nine A9 absolutes')
    print('PASS T2: all stock fields match retail constructor layouts')


if __name__=='__main__': main()
