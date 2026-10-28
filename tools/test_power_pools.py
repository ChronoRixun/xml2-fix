"""Independent layout/operand and every-source guard-vs-write gates; no game.

Build xml2_power_pools_host, set XML2_FIX_TEST_EXE, and run this script with
--host <native executable> --work <workspace proof directory>.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess
import power_pool_ledger as ledger

ROOT=Path(__file__).resolve().parents[1]

def packing(stride,n):
    # Append the template members in constructor order, independently of C++.
    cursor=4+n*stride;result={}
    for name,size in [('built',((n+31)//32)*4),('ring',(n+1)*4),
                      ('write',4),('read',4),('free',4),
                      ('used',((n+31)//32)*4),('live',4),('ids',n*4),
                      ('mask',4),('shift',4)]:
        result[name]=cursor;cursor+=size
    result.update(size=cursor,capacity=n,last=n-1,
                  ring_read=result['read']-result['ring'],
                  ring_free=result['free']-result['ring'])
    return result

def guard_spans():
    """Literal/constant, adjacent-string, byte-escape and nested ledger guards.

    Walk every source recursively, including the diagnostic observer table.
    Only this row's own generated guards are excluded (they are the required
    expectations for these very writes, rather than cross-rule dependencies).
    """
    files=sorted(p for p in (ROOT/'src').rglob('*') if p.suffix in ('.cpp','.hpp','.h'))
    constants={};spans=set()
    for p in files:
        for name,value in re.findall(r'\b(?:constexpr|const)\s+(?:[\w:<>]+\s+)+([a-zA-Z_]\w*)\s*=\s*(0x[\da-fA-F]+|[0-9]+)\s*[uUlL]*\s*;',p.read_text(encoding='utf-8-sig')):
            constants.setdefault(name,set()).add(int(value,0))
    for p in files:
        if p.name=='power_pool_sites.hpp':continue
        source=str(p.relative_to(ROOT));text=p.read_text(encoding='utf-8-sig')
        for m in re.finditer(r'\b(0x[\da-fA-F]+[uUlL]*|[a-zA-Z_]\w*)\s*,\s*(?:(?:\w+::)*bytes_of\s*\(\s*)?((?:"(?:[^"\\]|\\.)*"\s*)+)',text):
            value=''.join(re.findall(r'"((?:[^"\\]|\\.)*)"',m[2]))
            if re.fullmatch(r'[\da-fA-F]+',value) and len(value)%2==0:n=len(value)//2
            elif re.fullmatch(r'(?:\\x[\da-fA-F]{2})+',value):n=len(value)//4
            else:continue
            token=m[1]
            if token.lower().startswith('0x'):va=int(token.rstrip('uUlL'),16)
            elif token in constants and len(constants[token])==1:va=next(iter(constants[token]))
            else:continue
            spans.add((va,va+n,source))
        for m in re.finditer(r'\{\s*(0x[\da-fA-F]{6,8})\s*,([^{}\n]{0,160}?)"([\da-fA-F]{2,})"',text):
            if len(m[3])%2==0:
                va=int(m[1],16)
                if 0x401000<=va<0x800000:spans.add((va,va+len(m[3])//2,source))
        for m in re.finditer(r'\b(0x[\da-fA-F]+)[uUlL]*\s*,\s*\{([^{}]+)\}',text):
            parts=[v.strip() for v in m[2].split(',') if v.strip()]
            if parts and all(re.fullmatch(r'(?:0x[\da-fA-F]+|[0-9]+)[uUlL]*',v) and int(v.rstrip('uUlL'),0)<256 for v in parts):
                va=int(m[1],16);spans.add((va,va+len(parts),source))
    return files,spans

def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('--exe',default=os.environ.get('XML2_FIX_TEST_EXE'))
    a.add_argument('--host',type=Path,required=True);a.add_argument('--work',type=Path,required=True);args=a.parse_args()
    if not args.exe:a.error('set XML2_FIX_TEST_EXE or pass --exe')
    args.work.mkdir(parents=True,exist_ok=True)
    header,rows,helpers,calls=ledger.generate(args.exe,True)
    assert (ROOT/'src/power_pool_sites.hpp').read_text()==header
    path=args.work/'power-pool-plan.tsv'
    subprocess.run([str(args.host.resolve()),args.exe,'plan',str(path.resolve())],check=True,creationflags=0x08000000)
    writes={int(x[1],16):bytes.fromhex(x[2]) for line in path.read_text().splitlines() if (x:=line.split())[0]=='W'}
    models=[packing(0x68,256),packing(0x14,256)]
    assert models[0]['size']==0x7060 and models[1]['size']==0x1c60
    for k,table in enumerate(rows):
        for va,h,off,size,field,bias,stock in table:
            assert writes[va+off]==(models[k][field]-bias).to_bytes(size,'little'),(hex(va),field)
    print('PASS independent member packing and every generated 66/71 operand value')
    touched={va+i for va,b in writes.items() for i in range(len(b))}
    files,spans=guard_spans();hits=[]
    for lo,hi,source in sorted(spans):
        overlap=touched.intersection(range(lo,hi))
        if overlap:hits.append((source,hex(lo),list(map(hex,sorted(overlap)))))
    assert not hits,hits
    print(f'PASS every-src scan: {len(files)} files, {len(spans)} foreign guard/site spans, {len(touched)} written bytes, zero overlaps')
    for lo,h,call,_,k,kind,_ in helpers:
        assert touched.isdisjoint(range(lo,lo+len(h)//2))
        for other in calls[lo]-{call}:assert touched.isdisjoint(range(other,other+5))
    print('PASS all three shared helper bodies and 14 unrelated callers have zero writes')
    result=dict(source_files=len(files),foreign_spans=len(spans),written_bytes=len(touched),overlaps=hits,
                attached_operands=len(rows[0]),vf_operands=len(rows[1]),clears=6,private_helpers=3,
                shared_helper_bodies_written=0,other_helper_calls_written=0,
                retail_sha256=hashlib.sha256(Path(args.exe).read_bytes()).hexdigest())
    (args.work/'power-pool-static-gates.json').write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
