"""Independent shared-manager layout, legacy-write and cross-rule overlap gates.

Build xml2_style_manager_host first; set XML2_FIX_TEST_EXE for ledger verification.
No engine/game execution. Temporary plans stay in the selected work directory.
"""
from pathlib import Path
import argparse, os, re, subprocess
import w3_style_manager_ledger as ledger
ROOT=Path(__file__).resolve().parents[1]

def run(host, work, n, e):
 path=work/f'plan-{n}-{e}.tsv'
 subprocess.run([str(host),'--plan',str(n),str(e),str(path)],check=True)
 lines=[x.split() for x in path.read_text().splitlines()]
 return [(int(x[1],16),bytes.fromhex(x[2])) for x in lines if x[0]=='W'],[int(x[1],16) for x in lines if x[0]=='S'],[int(x[1],16) for x in lines if x[0]=='E']

def models(n,e):
 # Independent packing model: append the arrays in retail constructor order.
 # Initial tree header stays fixed (4 bytes within the +0x10 pool).
 off=4+n*16; ring=off;off+=(n+1)*4
 node=[ring,off,off+4,off+8,off+12];off+=12+((n+31)//32)*4
 node += [off]
 count=0x10+off;styles=count+4;bitmap=styles+n*0xec8
 iterator=bitmap+((n+31)//32)*4;aux=iterator+8
 part=[];off=e*0x54;part.append(off);off+=((e+31)//32)*4
 part.append(off);off+=(e+1)*4
 part += [off,off+4,off+8];off+=12
 part.append(off);off+=((e+31)//32)*4
 part.append(off);off+=4
 part.append(off);off+=e*4
 part += [off,off+4,(e+31)//32];off+=8
 return node+[count,styles,bitmap,iterator,iterator+4,aux,aux+off],part

def main():
 a=argparse.ArgumentParser();a.add_argument('--host',type=Path,default=ROOT/'native/bin/Release/xml2_style_manager_host.exe');a.add_argument('--work',type=Path);a.add_argument('--foreign-src',type=Path,default=ROOT/'src');args=a.parse_args()
 work=(args.work or ROOT/'native/style-manager-proof').resolve();work.mkdir(parents=True,exist_ok=True)
 old=subprocess.check_output(['git','show','69f5115:src/limits_rules.hpp'],cwd=ROOT,text=True)
 oldrows=re.findall(r'\{(0x[0-9a-f]+), "([0-9a-f]+)", (\d+), (\d+), (0x[0-9a-f]+), style_field::(\w+)\}',old)
 header,styles,events,aux=ledger.generate(os.environ['XML2_FIX_TEST_EXE'],True)
 assert [(int(a,16),b,int(c),int(d),int(e,16),f) for a,b,c,d,e,f in oldrows]==styles
 print('PASS generated 72 style rows match 69f5115 field for field')
 for n,e in [(19,1064),(32,1064),(64,4096),(127,8192)]:
  writes,s,v=run(args.host,work,n,e);sm,em=models(n,e)
  assert s==sm,(n,e,s,sm);assert v==em,(n,e,v,em)
 print('PASS independent retail-constructor packing models at 19/1064, 32/1064, 64/4096, 127/8192')
 for n in range(20,33):
  writes,s,_=run(args.host,work,n,1064)
  fields=dict(zip(['node_ring','node_ring_write','node_ring_read','node_ring_count','node_bitmap','node_count','count_or_styles_via_map','styles','bitmap','iterator_index','iterator_owner','auxiliary','manager_size'],s))
  fields.update(capacity=n,last_index=n-1,bitmap_via_styles=fields['bitmap']-fields['styles'],bitmap_via_map=fields['bitmap']-4)
  expected=[(int(va,16)+int(off),fields[f].to_bytes(int(size),'little')) for va,_,off,size,_,f in oldrows]
  for va,target in [(0x4ff7c2,0x12340000),(0x4ffa52,0x12340100)]:expected.append((va+1,((target-va-5)&0xffffffff).to_bytes(4,'little')))
  assert writes==expected,(n,writes,expected)
 print('PASS revision 1: exact 74-write plan unchanged for every N=20..32, E=1064')
 writes,_,_=run(args.host,work,64,1064)
 assert not any(va<=site+1<va+len(b) for va,b in writes for site in ledger.PARSER_ALLOCATIONS)
 print('PASS E=1064 keeps both parser allocation operands retail, including expanded styles')
 writes,_,_=run(args.host,work,64,4096)
 touched={va+i for va,b in writes for i in range(len(b))}
 own={'w3_style_manager_sites.hpp','style_manager_plan.hpp'};overlaps=[];guards=0
 for source in args.foreign_src.glob('*'):
  if source.suffix not in ('.hpp','.cpp') or source.name in own:continue
  text=source.read_text(encoding='utf-8')
  if source.name=='limits_rules.hpp':
   lo=text.index('// ---- Fighting/power styles');hi=text.index('// ---- The effect curve pool',lo);text=text[:lo]+text[hi:]
  # Includes nested guard records (rules 42/48) and operand ledgers (MoveTriggers).
  for m in re.finditer(r'\{\s*(0x[0-9a-fA-F]+)\s*,\s*"([0-9a-fA-F]+)"',text):
   va=int(m[1],16);b=m[2]
   if len(b)%2:continue
   guards+=1;hit=touched.intersection(range(va,va+len(b)//2))
   if hit:overlaps.append((source.name,hex(va),[hex(x) for x in sorted(hit)]))
 assert not overlaps,overlaps
 print(f'PASS guard-vs-site scan: {guards} foreign literal guards/sites, {len(touched)} shared-manager written bytes, zero overlaps (including rules 42/48 and MoveTriggers)')
 print(f'PASS ledger counts: {len(styles)} / {len(events)} / {len(aux)}')
if __name__=='__main__':main()
