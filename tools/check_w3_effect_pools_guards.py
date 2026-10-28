"""Scan every src byte guard against the W3-5/6 generated spans (both directions).

python -B tools/check_w3_effect_pools_guards.py [--extra-src path/to/other/src]
The extra source tree can include the independently implemented integration rows.
Numeric and named constant addresses and concatenated hex strings are supported.
"""
import argparse
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
GUARD=re.compile(r'\{\s*(0x[0-9a-fA-F]{5,8}|[A-Za-z_]\w*)\s*,\s*((?:"[0-9a-fA-F]+"\s*)+)')
CONST=re.compile(r'\b([A-Za-z_]\w*)\s*=\s*(0x[0-9a-fA-F]{5,8})\b')
OWN={'effect_vector_sites.hpp','primitive_def_sites.hpp'}


def rows(text,constants):
    for m in GUARD.finditer(text):
        symbol,body=m.groups()
        addresses=[int(symbol,16)] if symbol.startswith('0x') else constants.get(symbol,[])
        size=len(''.join(re.findall(r'"([0-9a-fA-F]+)"',body)))//2
        for address in addresses:yield address,address+size,text.count('\n',0,m.start())+1


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--extra-src',action='append',type=Path,default=[])
    args=p.parse_args()
    own=[]
    for name in sorted(OWN):
        for lo,hi,line in rows((ROOT/'src'/name).read_text(),{}):own.append((name,lo,hi,line))
    scanned=0;overlaps=[];files=0
    for name,a,b,line in own:
        for other,c,d,otherline in own:
            if name<other and a<d and c<b:
                overlaps.append(f'{name}:{line} {a:#x}..{b:#x} overlaps {other}:{otherline} {c:#x}..{d:#x}')
    for source in [ROOT/'src',*args.extra_src]:
        paths=sorted(p for p in source.rglob('*') if p.suffix in ('.cpp','.hpp','.h'))
        global_constants={}
        for path in paths:
            for key,value in CONST.findall(path.read_text(encoding='utf-8')):
                global_constants.setdefault(key,set()).add(int(value,16))
        for path in paths:
            if path.name in OWN:continue
            files+=1;text=path.read_text(encoding='utf-8');constants=dict(global_constants)
            for key,value in CONST.findall(text):constants[key]={int(value,16)}
            for lo,hi,line in rows(text,constants):
                scanned+=1
                for name,a,b,ownline in own:
                    if lo<b and a<hi:overlaps.append(f'{path}:{line} {lo:#x}..{hi:#x} overlaps {name}:{ownline} {a:#x}..{b:#x}')
    for row in overlaps:print('FAIL',row)
    print(f'{"FAIL" if overlaps else "PASS"}: {files} source files, {scanned} byte guards/sites, {len(own)} W3 spans, {len(overlaps)} foreign overlaps')
    return bool(overlaps)


if __name__=='__main__':raise SystemExit(main())
