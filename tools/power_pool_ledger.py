"""Derive W3-10/11 operand/cave/helper ledgers from the owned retail PE.

No game execution. --verify scans all text for helper callers, layout-bearing
operands outside the selected pool bodies, and incoming edges into clear caves.
"""
from pathlib import Path
import argparse, hashlib, json, os, re
import capstone as cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM
import pefile

ROOT=Path(__file__).resolve().parents[1]
SHA='146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b'
# Half-open template bodies, and the incoming this pointer's offset from outer.
BODIES=[
 [(0x5380d0,0x538132,4),(0x538450,0x538614,4),(0x538630,0x538678,4),
  (0x538680,0x5386cf,0),(0x5386d0,0x53875b,4),(0x538810,0x538836,0),
  (0x538840,0x538944,4),(0x538990,0x5389dd,0)],
 [(0x54c130,0x54c2f4,4),(0x54c320,0x54c368,4),(0x54c370,0x54c3bf,0),
  (0x54c3c0,0x54c40a,4),(0x54c410,0x54c43c,'ring'),(0x54c440,0x54c47a,4),
  (0x54c540,0x54c566,0),(0x54c570,0x54c657,4),(0x54c690,0x54c6dd,0)]]
FIELDS=['built','ring','write','read','free','used','live','ids','mask','shift','size']
VALUES=[dict(zip(FIELDS,[0x3404,0x3414,0x3618,0x361c,0x3620,0x3624,0x3634,0x3638,0x3838,0x383c,0x3840])),
 dict(zip(FIELDS,[0xf04,0xf1c,0x1220,0x1224,0x1228,0x122c,0x1244,0x1248,0x1548,0x154c,0x1550]))]
CAPS=[{0x5380fe,0x538127,0x538476,0x5384bd,0x53854b,0x538608,0x53863f,0x5386a0,0x5388c1},
 {0x54c149,0x54c189,0x54c19e,0x54c1ee,0x54c217,0x54c2e8,0x54c32f,0x54c390,0x54c41d,0x54c5f1}]
LAST=[{0x538117,0x5385c9,0x5388de},{0x54c207,0x54c2a9,0x54c60e}]
# Clear only MOV stores. Preserving registers/flags also preserves the original
# zero-valued source registers, pointer registers, and interleaved epilogues.
CAVES=[[(0x538558,8,'esi'),(0x53857b,11,'ecx'),(0x5385a3,11,'eax')],
 [(0x54c1a7,17,'esi'),(0x54c23b,17,'ecx'),(0x54c266,17,'ecx')]]
HELPERS=[(0x403ca0,0x403d39,0x538546,0,0),(0x403f20,0x403f4c,0x538709,0,1),
 (0x54c060,0x54c0f9,0x54c199,1,0)]

def generate(exe,verify=False):
 raw=Path(exe).read_bytes();assert hashlib.sha256(raw).hexdigest()==SHA,'wrong retail executable'
 pe=pefile.PE(data=raw);md=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32);md.detail=True
 def data(lo,hi):return pe.get_data(lo-0x400000,hi-lo)
 def decode(lo,hi):
  ins=list(md.disasm(data(lo,hi),lo));assert sum(i.size for i in ins)==hi-lo,hex(lo);return ins
 rows=[[],[]];caves=[[],[]];helpers=[];known=set()
 def row(i,offset,width,field,bias,value):
  assert int.from_bytes(i.bytes[offset:offset+width],'little')==value,(hex(i.address),field)
  known.add((i.address,offset));return (i.address,i.bytes.hex(),offset,width,field,bias,value)
 for k,bodies in enumerate(BODIES):
  n=[128,192][k]
  for lo,hi,bias in bodies:
   for i in decode(lo,hi):
    for o in i.operands:
     if o.type==X86_OP_MEM and i.disp_size:
      table={v-bias:f for f,v in VALUES[k].items()} if bias!='ring' else {0x308:'ring_read',0x30c:'ring_free'}
      if o.mem.disp in table:
       rows[k].append(row(i,i.disp_offset,i.disp_size,table[o.mem.disp],0 if bias=='ring' else bias,o.mem.disp))
     if o.type==X86_OP_IMM:
      field='capacity' if i.address in CAPS[k] else 'last' if i.address in LAST[k] else 'size' if i.mnemonic=='push' and o.imm==VALUES[k]['size'] else None
      if field:rows[k].append(row(i,i.imm_offset,i.imm_size,field,0,o.imm))
  assert {r[0] for r in rows[k] if r[4]=='capacity'}==CAPS[k]
  assert {r[0] for r in rows[k] if r[4]=='last'}==LAST[k]
  for va,size,reg in CAVES[k]:
   assert all(i.mnemonic=='mov' and i.operands[0].type==X86_OP_MEM for i in decode(va,va+size))
   caves[k].append((va,data(va,va+size).hex(),reg))
 for lo,hi,call,k,kind in HELPERS:
  hr=[]
  for i in decode(lo,hi):
   for o in i.operands:
    if o.type==X86_OP_IMM and o.imm in ([128,192][k],):hr.append(row(i,i.imm_offset,i.imm_size,'capacity',0,o.imm))
    if o.type==X86_OP_MEM and o.mem.disp in (0x208,0x20c):hr.append(row(i,i.disp_offset,i.disp_size,'ring_read' if o.mem.disp==0x208 else 'ring_free',0,o.mem.disp))
  ins=decode(lo,hi)
  assert all(not i.group(cs.CS_GRP_CALL) and (not i.group(cs.CS_GRP_JUMP) or lo<=i.operands[0].imm<hi) for i in ins),'non-local helper relocation'
  helpers.append((lo,data(lo,hi).hex(),call,data(call,call+5).hex(),k,kind,hr))
 calls={lo:set() for lo,*_ in HELPERS}
 if verify:
  text=next(s for s in pe.sections if s.Name.startswith(b'.text'))
  scan=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32);scan.skipdata=True
  incoming=[];outside=[]
  # Large relocated displacements are selective enough for a global census;
  # aliases with small inner offsets are covered by the full pool-body ledger.
  needles={v-b for t in VALUES for v in t.values() for b in (0,4)}
  for va,size,mn,ops in scan.disasm_lite(text.get_data(),0x400000+text.VirtualAddress):
   b=data(va,va+size)
   if mn not in ('call','jmp') and not mn.startswith('j') and not any(x.to_bytes(4,'little') in b for x in needles):continue
   i=next(md.disasm(b,va),None)
   if not i:continue
   for o in i.operands:
    if o.type==X86_OP_IMM and (i.group(cs.CS_GRP_JUMP) or i.group(cs.CS_GRP_CALL)):
     if o.imm in calls:calls[o.imm].add(va)
     for group in CAVES:
      for start,length,_ in group:
       if start<o.imm<start+length:incoming.append((hex(va),hex(o.imm)))
    if o.type==X86_OP_MEM and o.mem.disp in needles and (va,i.disp_offset) not in known:
     outside.append((hex(va),mn,ops))
  assert not incoming,incoming
  expected={0x403f20:{0x464e49,0x538709,0x5395c9,0x544899},0x54c060:{0x54c199,0x5862a0,0x586339}}
  assert all(calls[va]==s for va,s in expected.items()),calls
  assert len(calls[0x403ca0])==10,calls
  # Unrelated templates can have numerically equal displacements; record every
  # match for review rather than silently claiming pointer-provenance closure.
  print('GLOBAL_NUMERIC_MATCHES',json.dumps(outside))
  print('HELPER_CALLERS',json.dumps({hex(k):list(map(hex,sorted(v))) for k,v in calls.items()}))
 def sites(name,rs):
  return f'inline constexpr std::array<site,{len(rs)}> {name}{{{{\n'+''.join(f' {{{va:#x},"{h}",{o},{w},field::{f},{b},{v:#x}}},\n' for va,h,o,w,f,b,v in rs)+'}};\n'
 header='// Generated by tools/power_pool_ledger.py; do not edit.\n'
 for k,name in enumerate(('attached','vf')):
  header+=sites(name+'_sites',rows[k])
  header+=f'inline constexpr std::array<clear_site,3> {name}_clears{{{{\n'+''.join(f' {{{va:#x},"{h}",{dict(eax=0,ecx=1,esi=6)[reg]}}},\n' for va,h,reg in caves[k])+'}};\n'
 for index,(va,h,call,ch,k,kind,rs) in enumerate(helpers):
  header+=sites('helper_fields_'+str(index),rs)
  header+=f'inline constexpr helper helper_{index}{{{va:#x},"{h}",{call:#x},"{ch}"}};\n'
 return header,rows,helpers,calls

def main():
 a=argparse.ArgumentParser();a.add_argument('--exe',default=os.environ.get('XML2_FIX_TEST_EXE'));a.add_argument('--check',action='store_true');a.add_argument('--verify',action='store_true');args=a.parse_args()
 if not args.exe:a.error('set XML2_FIX_TEST_EXE or pass --exe')
 header,rows,_,_=generate(args.exe,args.verify);path=ROOT/'src/power_pool_sites.hpp'
 if args.check:assert path.read_text()==header,'stale generated header'
 else:path.write_text(header,encoding='utf-8',newline='\n')
 print(f'PASS attached {len(rows[0])} / VF {len(rows[1])} operands; 6 clears; 3 private helper copies')
if __name__=='__main__':main()
