"""Independent retail-guard and whole-text gate for W2 E14/E7; no game launch.
Usage: python scripts/check_w2_save_limits_text.py XMen2.exe build/save-limits
The execution fixture dumps patched text and all generated instruction caves.
"""
from pathlib import Path
import hashlib, re, sys
import capstone, pefile
from capstone import x86
from check_w2_save_text import decode

ROOT=Path(__file__).resolve().parent.parent
source=(ROOT/'src/save_limits_sites.hpp').read_text()
image=pefile.PE(sys.argv[1]).get_memory_mapped_image();prefix=Path(sys.argv[2]);errors=[]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;md.skipdata=True
ledger=source.split('inline const std::vector<guard> ledger{')[1]
guards=re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"\}',ledger)
for a,b in guards:
 a=int(a,16);b=bytes.fromhex(b)
 if image[a-0x400000:a-0x400000+len(b)]!=b:errors.append(f'retail ledger mismatch {a:x}')
if len(guards)!=138:errors.append(f'ledger inventory {len(guards)} != 105 objectives + 27 capacities + 6 special talent rows')
blocks=[(int(a,16),int(b,16),h) for a,b,h in re.findall(r'\{(0x[0-9a-f]+),(0x[0-9a-f]+),"([0-9a-f]{64})",\{',source)]
regions=[(int(a,16),int(b,16),int(t),h) for a,b,t,h in re.findall(r'\{(0x[0-9a-f]+),(0x[0-9a-f]+),(\d+),"([0-9a-f]{64})",',source)]
for a,b,h in blocks+[(a,b,h) for a,b,t,h in regions]:
 if hashlib.sha256(image[a-0x400000:b-0x400000]).hexdigest()!=h:errors.append(f'whole-owner hash {a:x}')
dead=[(a,b) for a,b,h in blocks]+[(a,b) for a,b,t,h in regions if t]
def inside(a,spans):return any(l<=a<h for l,h in spans)
old_data={0x72b118,0x72b248,0x72b56c,0x72b570,0x72b58c,0x72b590,0x72b5ac,0x72b5b0,0x72ba8c}
old_pools={0x7d64,0x7d6c,0x7d70,0x7d78,0x7d7c,0x8050}
old_talent={0x1130,0x1134,0x12c8,0x12cc,0x12d0,0x12d4,0x12e4,0xe10,0xe20,0xfb4,0xfb8,0xfbc,0xfc0,0xfd0,0xfd4,0x1164,0x1168,0xfd8,0x640,0x644,0x7d8,0x7dc,0x7e0,0x7e4,0x7f4,0x1654,0x1658,0x1724,0x26e4,0x26f4,0x26f8,0x2888,0x288c,0x2890,0x2894,0x2898,0x289c,0x3090,0x3094,0x12f4,0x804}
snapshot=[(0x4874b0,0x487571),(0x489300,0x4894d0),(0x48a170,0x48a3d0),(0x5f4400,0x5f4497)]
side=[(0x489b10,0x489d40),(0x48a130,0x48a170),(0x5f2ce0,0x5f2d09),(0x5f3630,0x5f3728),(0x5f44a0,0x5f4900)]
# Shared ratl pool<16,100> pop / clear: retail for their foreign owners.
SHARED_POOL=((0x4be010,0x4be06f),(0x4be9c0,0x4bea1d))
checked=0
def inspect(i,owner):
 global checked
 if not i.id:return
 checked+=1
 for o in i.operands:
  if o.type==x86.X86_OP_MEM:
   d=o.mem.disp&0xffffffff
   if d in old_data:errors.append(f'old objective global {owner:x}: {i.mnemonic} {i.op_str}')
   if d in old_pools:errors.append(f'old objective pool field {owner:x}: {i.op_str}')
   if 0x4bdfb0<=owner<0x4c06a0 and not inside(owner,SHARED_POOL) and d in old_talent:errors.append(f'old talent field {owner:x}: {i.op_str}')
   if inside(owner,snapshot) and d in range(0x254,0x26c,4):errors.append(f'old snapshot vector {owner:x}: {i.op_str}')
   if inside(owner,side) and o.mem.base!=x86.X86_REG_ESP and d in (0x4dc,(-0x26c)&0xffffffff):errors.append(f'old side-stack field {owner:x}: {i.op_str}')
   if 0x488720<=owner<0x489f80 and i.mnemonic=='movzx' and d==0x1a9:errors.append(f'byte objective index reader {owner:x}')
  elif o.type==x86.X86_OP_IMM:
   v=o.imm&0xffffffff
   if v in old_data:errors.append(f'old objective data address {owner:x}')
   if 0x4bdfb0<=owner<0x4c06a0 and owner!=0x4c05d9 and not inside(owner,SHARED_POOL) and i.mnemonic in ('cmp','mov','push') and v in (99,100):errors.append(f'old talent capacity {owner:x}: {i.op_str}')
   if inside(owner,side) and i.mnemonic in ('push','imul') and v==0x26c:errors.append(f'old side stride {owner:x}')
   if owner in (0x4c0584,0x4c083c) and v==0x3228:errors.append(f'old registry allocation {owner:x}')
patched=Path(str(prefix)+'-text.bin').read_bytes()
for i in decode(md,patched,0x401000):
 if not inside(i.address,dead):inspect(i,i.address)
for row in Path(str(prefix)+'-caves.tsv').read_text().splitlines():
 lo,hi,at,size,path=row.split('\t');lo,hi,at,size=[int(n,16) for n in (lo,hi,at,size)]
 if (lo,hi) not in [(l,h) for l,h,_ in blocks]:errors.append(f'unknown cave {lo:x}')
 body=Path(path).read_bytes();assert len(body)==size
 # Map emitted instructions back to their original instruction owners. Full
 # function caves can span several roles, so use each generated change delta.
 section=source.split(f'{{{lo:#x},{hi:#x},')[1].split(' }},')[0]
 changes=[(int(o,16),int(n),bytes.fromhex(b)) for o,n,b in re.findall(r'\{(0x[0-9a-f]+),(\d+),"([0-9a-f]+)"',section)]
 intervals=[];old=0;new=0
 for off,n,b in changes:
  if off>old:intervals.append((new,new+off-old,lo+old));new+=off-old
  intervals.append((new,new+len(b),lo+off));new+=len(b);old=off+n
 intervals.append((new,size,lo+old))
 for i in decode(md,body,at):
  offset=i.address-at;owner=next((v+(offset-a) for a,b,v in intervals if a<=offset<b),lo)
  inspect(i,owner)
# Shared helpers must stay byte-identical: only talent-owned edges may change.
for lo,hi in ((0x460250,0x460297),(0x4602a0,0x4602b7),(0x460310,0x46031c))+SHARED_POOL:
 if patched[lo-0x401000:hi-0x401000]!=image[lo-0x400000:hi-0x400000]:errors.append(f'shared helper changed {lo:x}')
import struct
def target(buf,base,a):return a+5+struct.unpack_from('<i',buf,a-base+1)[0]
for a,t in ((0x4d6b09,0x4be010),(0x4fd8d9,0x4be010),(0x4605c2,0x4be9c0),(0x4d7020,0x4be9c0),(0x4d81fe,0x4be9c0)):
 if patched[a-0x401000]!=0xe8 or target(patched,0x401000,a)!=t:errors.append(f'foreign ratl pool edge changed {a:x}')
for a,t in ((0x4bfce9,0x4be010),(0x4bfe10,0x4be9c0),(0x4c043d,0x4be9c0)):
 if patched[a-0x401000]!=0xe8 or target(patched,0x401000,a)==t:errors.append(f'talent pool edge still calls the shared helper {a:x}')
for error in errors[:60]:print('FAIL',error)
print(f'W2 E14/E7 text gate: {len(guards)} ledger guards, {len(blocks)+len(regions)} owner hashes, {len(blocks)} caves, {checked} instructions, {len(errors)} errors')
raise SystemExit(bool(errors))
