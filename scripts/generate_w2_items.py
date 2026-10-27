"""Generate W2 item plans from the owner's retail PE and reviewed SPEC-old ledger.
No game bytes outside instruction guards are emitted. Whole copied blocks are read
at runtime only after a SHA-256 guard; branch relocation metadata is generated here.
Usage: python scripts/generate_w2_items.py XMen2.exe sites.json
"""
from pathlib import Path
import sys,json,struct,hashlib
import pefile,capstone
from capstone import x86
ROOT=Path(__file__).resolve().parent.parent
pe=pefile.PE(sys.argv[1]); image=pe.get_memory_mapped_image(); md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
assert hashlib.sha1(Path(sys.argv[1]).read_bytes()).hexdigest()=='7d95cdb4a9a599b982147a3389ea7aff211cb82b','retail executable SHA-1 mismatch'
ledger=json.loads(Path(sys.argv[2]).read_text())
def raw(a,n):return image[a-0x400000:a-0x400000+n]
def ins(a):return next(md.disasm(raw(a,16),a,count=1))
def decode(a,b):return list(md.disasm(raw(a,b-a),a))
def p32(n):return struct.pack('<I',n&0xffffffff)
def pool(n,stride):
 b=n*stride;ring=b+(n+31)//32*4;head=ring+n*4+4;buckets=head+12;live=buckets+(n+31)//32*4;handles=live+4;mask=handles+n*4
 return dict(bitmap=b,ring=ring,head=head,tail=head+4,ringcount=head+8,buckets=buckets,live=live,handles=handles,mask=mask,shift=mask+4,size=mask+12)
ip=pool(1024,0x34); ap=pool(4096,0x14)
# Manager layout: old object, enhancement records/bitmap, definitions/state,
# world pairs/bitmap. Existing unrelated members keep their offsets.
L=dict(records=0x7a00,enh_bitmap=0x2fa00,definitions=0x2fc00,states=0x3ec00,world=0x3f400,world_bitmap=0x41400,size=0x41480)
patches={}; classes={}; extra={}; full=[]
def edit(a,new,kind,oldlen=None):
 i=ins(a); n=oldlen or i.size
 if isinstance(new,str):new=bytes.fromhex(new)
 assert oldlen or a not in patches or patches[a][1]==new,(hex(a),'conflict')
 patches[a]=(n,new.ljust(n,b'\x90'));classes[a]=kind

def operand(i,value,disp=False):
 b=bytearray(i.bytes);o,s=(i.disp_offset,i.disp_size) if disp else (i.imm_offset,i.imm_size)
 assert s>0
 assert -(1<<(s*8-1))<=value<(1<<(s*8)),(hex(i.address),value,s)
 b[o:o+s]=(value&((1<<(s*8))-1)).to_bytes(s,'little');return b

def map_pool(i,old,new,capacity):
 b=bytearray(i.bytes);changed=False
 if i.disp_size:
  v=i.disp&0xffffffff
  if v in old:
   b[i.disp_offset:i.disp_offset+i.disp_size]=p32(new[old[v]])[:i.disp_size];changed=True
 if i.imm_size and not i.group(capstone.CS_GRP_CALL) and not i.group(capstone.CS_GRP_JUMP):
  v=int.from_bytes(i.bytes[i.imm_offset:i.imm_offset+i.imm_size],'little')
  nv=capacity if v== (256 if capacity==1024 else 375) else capacity-1 if v==(255 if capacity==1024 else 374) else None
  if nv is not None:b[i.imm_offset:i.imm_offset+i.imm_size]=nv.to_bytes(i.imm_size,'little');changed=True
 return b if changed else None
oldi={0x3400:'bitmap',0x3420:'ring',0x3824:'head',0x3828:'tail',0x382c:'ringcount',0x3830:'buckets',0x3850:'live',0x3854:'handles',0x3c54:'mask',0x3c58:'shift'}
olda={0x1d4c:'bitmap',0x1d7c:'ring',0x235c:'head',0x2360:'tail',0x2364:'ringcount',0x2368:'buckets',0x2398:'live',0x239c:'handles',0x2978:'mask',0x297c:'shift'}
for key in ('item-defs','enhancements','attached-pool','item-handles'):
 for row in ledger[key]:
  a=int(row['addr'],16);b=bytes.fromhex(row['bytes']);assert raw(a,len(b))==b,(key,hex(a),'retail guard')
# Pool-relative vs object-relative accesses are deliberately separate.
for i in decode(0x483090,0x483480)+decode(0x482a20,0x482ab0):
 # Shared ring<256>::pop has three foreign callers. The item call alone is
 # redirected to a private 1024-entry implementation; never widen this body.
 if 0x483410<=i.address<0x48343c:continue
 new=map_pool(i,oldi,ip,1024)
 if new:edit(i.address,new,'instance-layout')
for i in decode(0x45f2a0,0x45fb60):
 new=map_pool(i,olda,ap,4096)
 if new:edit(i.address,new,'attached-layout')
# Object live-count compare; ring-relative pop helpers.
for a,offs in ((0x483390,ip['live']+4),(0x45f920,ap['live']+4)):
 i=ins(a);b=operand(i,offs,True);b[i.imm_offset:i.imm_offset+4]=p32(1024 if a==0x483390 else 4096);edit(a,b,'object-count')
for i in decode(0x45f9a0,0x45f9ce):
 if i.disp in (0x5e4,0x5e8):edit(i.address,operand(i,4096*4+(i.disp-375*4),True),'attached-ring-relative')
for a in (0x45f315,0x45f6c9,0x45f6e8):edit(a,operand(ins(a),128),'attached-bitmap-clear')
edit(0x48361e,b'\x68'+p32(ip['size']),'instance-allocation')
edit(0x45fb0e,b'\x68'+p32(ap['size']),'attached-allocation')
# All original helper entry points stay untouched. Calls acquire private clones.
# Synthetic branch targets 1/2 resolve to the private helper clones at runtime.
for a,target in ((0x482a89,1),(0x45f309,2),(0x4833f9,5)):
 edit(a,b'\xe8'+p32(0),'helper-call');extra[a]=target
# Clears keep flags and every GP register (the retail unrolled stores do too).
def clear(reg,n):return bytes.fromhex('9c6057')+bytes([0x8b,0xf8+reg])+b'\x33\xc0\xb9'+p32(n)+bytes.fromhex('f3ab5f619d')
# mov edi,ecx=8bf9; mov edi,esi=8bfe
for a,b,reg in ((0x48310b,0x483122,1),(0x48313c,0x483153,1),(0x482a97,0x482aae,6)):
 edit(a,clear(reg,32),'instance-clear',b-a)
 for x in list(patches):
  if a<x<b:del patches[x];classes.pop(x,None)
# World-slot table and bounds; the shared table-relative bitset scan is private
# to this owner and can be raised in place.
for row in ledger['item-handles']:
 a=int(row['addr'],16);i=ins(a)
 if i.disp_size and 0x72a0<=i.disp<=0x7760:
  d=L['world']+(i.disp-0x72a0) if i.disp<0x7750 else L['world_bitmap']+(i.disp-0x7750)
  edit(a,operand(i,d,True),'world-layout')
 if i.imm_size and i.mnemonic not in ('call','jmp'):
  v=int.from_bytes(i.bytes[i.imm_offset:i.imm_offset+i.imm_size],'little')
  if v==150:edit(a,operand(i,1024),'world-capacity')
  elif v==0x72a0:edit(a,operand(i,L['world']),'world-unwind')
 if i.disp_size and i.disp==0x4b0:edit(a,operand(i,0x2000,True),'world-relative-bitmap')
# The table's clear count and its findNext capacities were not in the old list.
for i in decode(0x47db70,0x47dbea):
 if i.imm_size and i.mnemonic=='mov' and i.op_str=='ecx, 5':edit(i.address,operand(i,32),'world-clear-count')
for i in decode(0x47b850,0x47b8f0):
 if i.imm_size and i.mnemonic in ('mov','cmp') and i.operands[-1].type==x86.X86_OP_IMM and i.operands[-1].imm==150:edit(i.address,operand(i,1024),'world-findNext')
# Constructor clears: five stores become a 32-dword clear of moved storage.
edit(0x480827,bytes.fromhex('9c6057')+b'\x8d\xbe'+p32(L['world_bitmap'])+bytes.fromhex('33c0b920000000f3ab5f619d'),'world-clear',0x480845-0x480827)
for a in list(patches):
 if 0x480827<a<0x480845:del patches[a];classes.pop(a,None)
# Owner-specific entity +0x320. Keep quantity +0x322 and all foreign owners.
for row in ledger['item-handles']:
 a=int(row['addr'],16);i=ins(a)
 if i.disp!=0x320 or not 0x479a00<=a<0x47ab00:continue
 b=bytearray(i.bytes)
 if b[:2]==b'\x0f\xb6':b[1]=0xb7
 elif b[0]==0x8a:b[0]=0x8b;b=b'\x66'+b
 elif b[0]==0x88:b[0]=0x89;b=b'\x66'+b
 elif b[0] in (0xc6,0x80):b[0]=0xc7 if b[0]==0xc6 else 0x83;b=b'\x66'+b+(b'\xff' if b[0]==0xc7 else b'')
 else:continue
 edit(a,b,'entity-slot-u16')
for a in (0x42ec64,0x47a011):
 b=bytearray(ins(a).bytes);assert b[1]==0xb6;b[1]=0xb7;edit(a,b,'entity-slot-caller')
# Allocation results and their stack consumers must be widened together.
edit(0x479db5,'668b44241c','entity-slot-result')
# Write/read bridges copy record+0x12; retain the opaque payload cursor.
for a,target in ((0x479a93,3),(0x479fdf,4)):
 edit(a,b'\xe8'+p32(0),'entity-slot-stream');extra[a]=target
edit(0x47ad22,'663dffff','enhancement-none')
# Full-pool failure now reports an error before returning the native null.
# Called through a tiny bridge so the retail allocation path remains intact.
# Function replacement is selected below by the runtime, not an ini key.

# Destructor bitmap clear was absent from SPEC-old's world-slot rows.
edit(0x47dbe7,clear(6,32),'world-destructor-clear',0x47dbf5-0x47dbe7)
# Reward bases are course+4/+0x18/+0x2c; their +8 slot is initialized
# at absolute constructor offsets +0xc/+0x20/+0x34. Preserve +0xa.
for a,off in ((0x4c891a,0xc),(0x4c894a,0x20),(0x4c896f,0x34)):
 edit(a,bytes.fromhex('66c740')+bytes([off])+b'\xff\xff','definition-reward-constructor')
# Log the table-full epilogue while preserving its native register cleanup.
edit(0x47ea10,bytes.fromhex('9c60b8')+p32(0xfeed0006)+bytes.fromhex('ffd0619d')+raw(0x47ea10,12),'world-full-telemetry',12)
# E2 definition count: zero-extending reads, word stores, word comparisons.
def widen_mem_disp(i,value):
 b=bytearray(i.bytes)
 if i.disp_size==1:
  # All these authored bases use a ModRM mod=01 plus disp8; retain SIB.
  op=1 if b[0]!=0x0f else 2
  assert b[op]&0xc0==0x40
  b[op]=(b[op]&0x3f)|0x80;b[i.disp_offset:i.disp_offset+1]=p32(value)
 else:b[i.disp_offset:i.disp_offset+i.disp_size]=p32(value)
 return b
count_compares=[]
for row in ledger['item-defs']:
 a=int(row['addr'],16);i=ins(a);b=bytearray(i.bytes)
 if i.disp==0x2588:
  if b[:2]==b'\x0f\xb6':b[1]=0xb7
  elif b[0]==0x8a:b=b'\x0f\xb7'+b[1:]
  elif b[0]==0x88:b=b'\x66\x89'+b[1:]
  elif b[0]==0xc6:b=b'\x66\xc7'+b[1:]+b'\0'
  elif b[0]==0x3a:b=b'\x66\x3b'+b[1:];count_compares.append(a)
  elif b[0]==0x8d:continue
  else:raise AssertionError((hex(a),'count',b.hex()))
  edit(a,b,'definition-count-u16')
 if i.disp_size and i.disp==0x785c:edit(a,operand(i,L['states'],True),'definition-states')
 if a==0x673aae:edit(a,operand(i,L['states']),'definition-states-unwind')
 if i.mnemonic=='push' and i.operands[0].type==x86.X86_OP_IMM and i.operands[0].imm==160:edit(a,operand(i,1024),'definition-constructor-count')
# Each by-index thunk loads and zero-extends the widened argument, and its
# private base calculation follows the relocated definitions. The values the
# definition methods return (quantity/flags) retain their native widths.
for a in count_compares:
 if a!=0x47de1c:
  edit(a-4,b'\x0f\xb7'+raw(a-3,3),'definition-index-argument')
  i=ins(a+8);assert i.bytes==bytes.fromhex('0fb6c0');edit(a+8,'0fb7c0','definition-index-zeroextend')
 else:
  edit(0x47de13,'0fb744240c','definition-index-argument')
  edit(0x47de64,'0fb74c241c','definition-index-zeroextend')
# Find exactly the known manager+4 address constructions adjacent to the 18
# stride expressions, plus the six loop/constructor bases reviewed in RE-1.
defbases=set()
for row in ledger['item-defs']:
 a=int(row['addr'],16);i=ins(a)
 if i.mnemonic=='imul' and i.imm_size and i.operands[-1].type==x86.X86_OP_IMM and i.operands[-1].imm==0x3c:
  for j in decode(a+i.size,a+i.size+18):
   if j.mnemonic in ('mov','lea') and j.disp==4 and (j.bytes[1]&0xc0)==0x40:
    defbases.add(j.address)
   elif j.mnemonic not in ('pop',):break
for a in (0x48072a,0x47e123,0x47e333,0x47f643,0x47f965,0x480b70):defbases.add(a)
for a in sorted(defbases):edit(a,widen_mem_disp(ins(a),L['definitions']),'definition-base')
edit(0x480996,b'\x81\xc6'+p32(L['definitions']),'definition-destructor-base')
for a in (0x673a43,0x673ad3):edit(a,b'\x81\xc1'+p32(L['definitions']),'definition-unwind-base')
edit(0x47c257,b'\x81\xee'+p32(L['definitions']),'definition-pointer-index')
# Full-count tests use 16 bits; never treat count 256 as zero.
for a in (0x47c4d6,0x47e11e,0x47e32e,0x47f63f,0x47f95b,0x480b68):
 b=bytearray(ins(a).bytes);assert b[0]==0x84;b[0]=0x85;edit(a,b'\x66'+b,'definition-count-test')
edit(0x47c512,'0fb713','definition-count-indirect')
edit(0x480430,operand(ins(0x480430),1024),'definition-registration-cap')
edit(0x480572,'8bcb','definition-loader-count')
edit(0x480574,'41','definition-loader-increment')
edit(0x4805e9,'0fb74c241c','definition-loader-count')
# Index lookup failure/success are both widened. Success must not truncate BL.
edit(0x47e389,'83c8ff','definition-lookup-failure')
edit(0x47e393,'0fb7c3','definition-lookup-success')
for a in (0x49ed9d,0x49ee11):
 b=bytearray(ins(a).bytes);b[0]=0x89;edit(a,b,'definition-script-index-dword')
# Reward +8 becomes u16; +0xa is already a live u16 and is preserved.
edit(0x4d47da,'66894708','definition-reward-index')
for a in (0x4c8fe1,0x4ca138):edit(a,'0fb74e08','definition-reward-reader')
for a in (0x4c8fd2,0x4ca129):edit(a,'66837e08ff','definition-reward-none')
# Global first-type-2 definition becomes a DLL DWORD; runtime resolves marker.
for row in ledger['item-defs']:
 a=int(row['addr'],16);i=ins(a)
 if i.disp!=0x6d83bc:continue
 b=bytearray(i.bytes)
 if b[0] in (0x8a,0x88):b[0]+=1
 elif b[0]==0x80:b[0]=0x83
 elif b[0]==0xc6:b[0]=0xc7;b+=b'\xff'*3
 else:raise AssertionError((hex(a),'global'))
 b[i.disp_offset:i.disp_offset+4]=p32(0xfeed0005)
 edit(a,b,'definition-first-type2-dword')
# Native count, definition-pair block and instance index stream widths.
for a in (0x47c522,0x47f801,0x481629,0x482b46):edit(a,'6a02','definition-stream-width')
for a in (0x47c52c,0x47f81f):edit(a,b'\x68'+p32(2048),'definition-stream-pairs')
# Instance save originally had a byte immediately before another live stack
# local. Move its temporary to a safe two-byte slot in the existing padding.
edit(0x48162b,'8d54240e','definition-stream-index-temp')
edit(0x481632,'6689442412','definition-stream-index-store')
edit(0x482b8f,'0fb74c240c','definition-stream-index-load')
# Picker returns travel through full DWORD locals in the two receiving owners.
for a,new in ((0x47d68e,'8b5c2450'),(0x47d6a0,'663dffff'),(0x47d6a2,'89442450'),(0x47d6b6,'89542450'),(0x47ecd1,'8bd8'),(0x47ecd6,'895c2414'),(0x47ecdf,'6683fbff')):edit(a,new,'definition-picker-caller')
# Preserve the original FP instructions and exact RNG calls by copying this
# complete function, changing only its index locals / counter. No frame growth.
full.append((0x47b590,0x47b782))
for a,new in ((0x47b610,'83cbff'),(0x47b615,'66895c240e'),(0x47b621,'66c74424100000'),(0x47b640,'0fb77c2414'),(0x47b732,'0fb7442414'),(0x47b736,'6689442412'),(0x47b748,'0fb7442414'),(0x47b74e,'40'),(0x47b752,'6689442414'),(0x47b75c,'0fb74c2414'),(0x47b769,'0fb7442412'),(0x47b779,'0fb7c3')):edit(a,new,'definition-picker-u16-locals')

# Build cave spans for instructions which grow. Other edits are direct writes.
# Grow to whole instructions >=5 bytes; merge overlaps before relocating.
spans=[]
for a,(n,b) in sorted(patches.items()):
 if len(b)>n:
  hi=a+n
  while hi-a<5:hi+=ins(hi).size
  spans.append((a,hi))
# Explicit full copied functions can have any number of internal edits.
spans+=full
again=True
while again:
 again=False;result=[]
 for a,b in sorted(spans):
  if result and a<result[-1][1]:result[-1]=(result[-1][0],max(b,result[-1][1]));again=True
  else:result.append((a,b))
 spans=result
# Refuse a control-flow edge into a displaced instruction except its entry.
# Same-size writes don't displace any instruction.
md.skipdata=True
text=next(s for s in pe.sections if s.Name.startswith(b'.text'))
# Streaming chunks bound capstone detail allocation.
def wholetext():
 lo=0x400000+text.VirtualAddress;hi=lo+text.Misc_VirtualSize
 while lo<hi:
  stop=min(lo+0x8000,hi);end=lo
  for i in md.disasm(raw(lo,min(stop+16,hi)-lo),lo):
   if i.address>=stop:break
   yield i;end=i.address+i.size
  if end<=lo:break
  lo=end
# Promote a cave to its complete owner whenever an incoming branch targets a
# displaced interior instruction (notably the by-name lookup's loop head).
# Only instruction offsets/edits/branch targets are emitted; bodies come from
# the separately hash-guarded owner image at runtime.
functions=[(int(line.split()[0],16),int(line.split()[1])) for line in (Path(sys.argv[2]).parent/'funcs.txt').read_text().splitlines()]
branches=[]
for i in wholetext():
 if i.id and (i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)) and i.operands and i.operands[0].type==x86.X86_OP_IMM:branches.append((i.address,i.operands[0].imm))
for iteration in range(10):
 incoming=[(source,t,a,b) for source,t in branches for a,b in spans if a<t<b and not a<=source<b]
 if not incoming:break
 for source,t,a,b in incoming:
  owners=[(lo,lo+size) for lo,size in functions if lo<=a<b<=lo+size]
  if not owners:
   starts=sorted({int(row['func'],16) for group in ledger.values() for row in group if row.get('func','').startswith('0x') and int(row['func'],16)<=a},reverse=True)
   for start in starts:
    finish=next((i.address for i in decode(start,start+0x5000) if i.id and i.mnemonic=='int3'),start)
    if start<=a<b<=finish:owners=[(start,finish)];break
   if not owners:raise RuntimeError(('missing owner',hex(a),hex(b),hex(source),hex(t)))
  lo,hi=min(owners,key=lambda v:v[1]-v[0])
  if (lo,hi) in spans:raise RuntimeError(('external interior entry',hex(source),hex(t),hex(lo),hex(hi)))
  spans.append((lo,hi))
 merged=[]
 for a,b in sorted(spans):
  if merged and a<merged[-1][1]:merged[-1]=(merged[-1][0],max(b,merged[-1][1]))
  else:merged.append((a,b))
 spans=merged
else:raise RuntimeError('owner promotion did not converge')
lines=['// Generated by scripts/generate_w2_items.py; retail guards and authored transformations.','#pragma once','#include <cstdint>','#include <vector>','namespace item_capacity_sites {','struct row { std::uint32_t va; const char* old; const char* value; const char* kind; unsigned target; };','inline const std::vector<row> direct{']
for a,(n,b) in sorted(patches.items()):
 if not any(lo<=a<hi for lo,hi in spans):lines.append(f' {{{a:#x},"{raw(a,n).hex()}","{b.hex()}","{classes[a]}",{extra.get(a,0)}}},')
lines+=['};','struct change { unsigned offset, size; const char* value; std::uint32_t target; };','struct block { std::uint32_t lo,hi; const char* hash; std::vector<change> edits; };','inline const std::vector<block> blocks{']
for lo,hi in spans:
 changes=[];covered=set()
 for i in decode(lo,hi):
  a=i.address
  if not i.id or a in covered:continue
  if a in patches:
   n,b=patches[a];changes.append((a-lo,n,b.hex(),extra.get(a,0)));covered.update(range(a,a+n))
  elif (i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)) and i.operands[0].type==x86.X86_OP_IMM:
   b=i.bytes;t=i.operands[0].imm
   if b[0] in (0xe8,0xe9,0xeb):value='e800000000' if b[0]==0xe8 else 'e900000000'
   elif 0x70<=b[0]<=0x7f:value=f'0f{b[0]+0x10:02x}00000000'
   elif b[:1]==b'\x0f' and 0x80<=b[1]<=0x8f:value=b[:2].hex()+'00000000'
   else:raise RuntimeError(('branch',hex(a),i.mnemonic))
   changes.append((a-lo,i.size,value,t))
  elif i.mnemonic=='call' and i.operands[0].type==x86.X86_OP_IMM:raise AssertionError(hex(a))
 lines.append(f' {{{lo:#x},{hi:#x},"{hashlib.sha256(raw(lo,hi-lo)).hexdigest()}",{{')
 for off,n,b,t in changes:lines.append(f'  {{{off:#x},{n},"{b}",{t:#x}}},')
 lines.append(' }},')
lines+=['};','struct guard {std::uint32_t va; const char* bytes;};','inline const std::vector<guard> ledger{']
for key in ('item-defs','enhancements','attached-pool','item-handles'):
 for row in ledger[key]:lines.append(f' {{{int(row["addr"],16):#x},"{row["bytes"]}"}},')
omission_addresses=[0x483094,0x48309a,0x4830a0,0x4830b0,0x4830b6,0x4830be,0x4830c3,0x4830c9,0x4830d1,0x4830d7,0x4830df,0x4830e7,
 0x483410,0x483417,0x483422,0x48342a,0x483435,
 0x482a20,0x482a27,0x482a39,0x482a79,0x482a89,0x482a8e,
 0x44d0d4,0x44d0de,0x44d10b,0x44d158,0x44d15f]
for lo,hi in ((0x48310b,0x483122),(0x48313c,0x483153),(0x482a97,0x482aae)):
 omission_addresses += [i.address for i in decode(lo,hi)]
assert len(omission_addresses)==52
lines+=['};','inline const std::vector<guard> omissions{']
for a in omission_addresses:lines.append(f' {{{a:#x},"{ins(a).bytes.hex()}"}},')
lines+=['};','}']
(ROOT/'src/item_capacity_sites.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
print('layout',L,'instance',ip,'attached',ap)
print('guards',sum(len(ledger[k]) for k in ('item-defs','enhancements','attached-pool','item-handles')),'edits',len(patches),'copied blocks',len(spans))
(ROOT/'build/item-capacity-plan.json').write_text(json.dumps(dict(layout=L,instance=ip,attached=ap,patches=[dict(va=a,size=n,new=b.hex(),kind=classes[a]) for a,(n,b) in sorted(patches.items())],blocks=spans),indent=2)+'\n',encoding='utf-8',newline='\n')
