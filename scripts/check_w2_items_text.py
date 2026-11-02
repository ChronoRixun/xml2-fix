"""Whole-text completeness gate for W2 item capacities (no game launch).
Usage: python scripts/check_w2_items_text.py XMen2.exe build/items
The mapped-retail item test emits <prefix>-text.bin, -caves.tsv and copied bodies
before installing environment fakes. Guard bytes and region hashes are checked
again here independently of the transaction, then every live instruction is
scanned for old class-owned widths, bases and capacity values.
"""
from pathlib import Path
import re,sys,hashlib,struct
import capstone,pefile
from capstone import x86
from check_w2_save_text import decode
ROOT=Path(__file__).resolve().parent.parent
source=(ROOT/'src/item_capacity_sites.hpp').read_text()
pe=pefile.PE(sys.argv[1]);image=pe.get_memory_mapped_image();prefix=Path(sys.argv[2]);errors=[]
text=next(s for s in pe.sections if s.Name.startswith(b'.text'));base=0x400000+text.VirtualAddress;size=text.Misc_VirtualSize
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;md.skipdata=True
ledger=source.split('inline const std::vector<guard> ledger{')[1]
guards=re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"\}',ledger)
for address,hexbytes in guards:
 address=int(address,16);b=bytes.fromhex(hexbytes)
 if image[address-0x400000:address-0x400000+len(b)]!=b:errors.append(f'guard {address:x}')
if len(guards)!=521:errors.append(f'ledger inventory {len(guards)}, expected 469+52')
blocks=[(int(a,16),int(b,16),h) for a,b,h in re.findall(r'\{(0x[0-9a-f]+),(0x[0-9a-f]+),"([0-9a-f]{64})",\{',source)]
cpp=(ROOT/'src/item_capacity.cpp').read_text()
regions=blocks[:]
regions += [(int(a,16),int(b,16),h) for a,b,h in re.findall(r'"(?:DLL item pool getter|W2 inventory slot serializer|W2 item registrar owner|shared instance ring pop)",(0x[0-9a-f]+),(0x[0-9a-f]+),sha256_of\("([0-9a-f]{64})"\)',cpp)]
regions += [(int(a,16),int(b,16),h) for a,b,h in re.findall(r'\{(0x[0-9a-f]+),(0x[0-9a-f]+),"([0-9a-f]{64})",&',cpp)]
helper_hashes=re.search(r'hash=id==1\?"([0-9a-f]{64})":"([0-9a-f]{64})"',cpp)
assert helper_hashes
regions += [(0x44d0d0,0x44d169,helper_hashes[1]),(0x45e5f0,0x45e689,helper_hashes[2])]
for a,b,h in regions:
 if hashlib.sha256(image[a-0x400000:b-0x400000]).hexdigest()!=h:errors.append(f'region {a:x}')
DEAD=[(a,b) for a,b,h in blocks]+[(0x47e690,0x47e818),(0x47ee90,0x47effb),(0x481938,0x481940),(0x483610,0x48365d),(0x45fb00,0x45fb4d)]
def inside(a,spans):return any(lo<=a<hi for lo,hi in spans)
INSTANCES=(0x482a20,0x483670);ATTACHED=(0x45f2a0,0x45fb60);MANAGER=(0x47b4d0,0x480c24);ENTITY=(0x479a70,0x47ab00)
old_instance={0x3400,0x3420,0x3824,0x3828,0x382c,0x3830,0x3850,0x3854,0x3c54,0x3c58}
old_attached={0x1d4c,0x1d7c,0x235c,0x2360,0x2364,0x2368,0x2398,0x239c,0x2978,0x297c}
old_manager={0x2594,0x602c,0x785c,0x72a0,0x72a4,0x7750,0x7754,0x7758,0x775c,0x7760}
checked=0

def inspect(i,owner):
 global checked
 if not i.id:return
 checked+=1
 for o in i.operands:
  if o.type==x86.X86_OP_MEM:
   d=o.mem.disp & 0xffffffff
   if d==0x6d83bc:errors.append(f'old definition global {owner:x}: {i.mnemonic} {i.op_str}')
   if MANAGER[0]<=owner<MANAGER[1] and (d in old_manager or d==0x2588 and o.size==1):errors.append(f'old manager operand {owner:x}: {i.mnemonic} {i.op_str}')
   if INSTANCES[0]<=owner<INSTANCES[1] and d in old_instance:errors.append(f'old instance operand {owner:x}: {i.mnemonic} {i.op_str}')
   if ATTACHED[0]<=owner<ATTACHED[1] and d in old_attached:errors.append(f'old attached operand {owner:x}: {i.mnemonic} {i.op_str}')
   if (ENTITY[0]<=owner<ENTITY[1] or owner==0x42ec64) and d==0x320 and o.size==1:errors.append(f'old entity slot byte {owner:x}: {i.mnemonic} {i.op_str}')
  if o.type==x86.X86_OP_MEM and owner in (0x4c8fd2,0x4c8fe1,0x4ca129,0x4ca138,0x4d47da) and o.mem.disp==8 and o.size==1:errors.append(f'old reward index byte {owner:x}')
  if owner in (0x673a43,0x673ad3) and o.type==x86.X86_OP_IMM and o.imm==4:errors.append(f'old definition unwind base {owner:x}')
  if o.type==x86.X86_OP_IMM and i.mnemonic in ('cmp','mov','push'):
   v=o.imm & 0xffffffff
   if INSTANCES[0]<=owner<INSTANCES[1] and not 0x483410<=owner<0x48343c and v==256:errors.append(f'old instance capacity {owner:x}')
   if ATTACHED[0]<=owner<ATTACHED[1] and v==375:errors.append(f'old attached capacity {owner:x}')
   if MANAGER[0]<=owner<MANAGER[1] and v==150:errors.append(f'old world-slot capacity {owner:x}')

patched=Path(str(prefix)+'-text.bin').read_bytes();assert len(patched)==size
for i in decode(md,patched,base):
 if not inside(i.address,DEAD):inspect(i,i.address)
# Copied owners keep their retail ownership even though their addresses move.
# The checks above do not rely on the generator's edit classification.
caves=0
for line in Path(str(prefix)+'-caves.tsv').read_text().splitlines():
 a,b,at,n,path=line.split('\t');a,b,at,n=[int(v,16) for v in (a,b,at,n)]
 if (a,b) not in [(lo,hi) for lo,hi,h in blocks]:errors.append(f'unexpected cave {a:x}')
 code=Path(path).read_bytes();assert len(code)==n
 for i in decode(md,code,at):inspect(i,a) # each copied span belongs wholly to one owner class
 caves+=1
# The original bitset helper is shared: the install must leave it byte-identical.
for lo,hi in ((0x44d0d0,0x44d169),(0x45e5f0,0x45e689),(0x483410,0x48343c)):
 if patched[lo-base:hi-base]!=image[lo-0x400000:hi-0x400000]:errors.append(f'shared helper changed {lo:x}')
for caller in (0x4c7089,0x5421f9,0x58a209):
 if patched[caller-base:caller-base+5]!=image[caller-0x400000:caller-0x400000+5]:errors.append(f'foreign ring caller changed {caller:x}')
# No live original instruction may branch into the five bytes displaced by a
# cave or full replacement. Internal copied branches are resolved in the dump.
entries=[(lo,lo+5) for lo,hi,h in blocks]+[(0x47e690,0x47e695),(0x47ee90,0x47ee95),(0x481938,0x48193d),(0x483610,0x483615),(0x45fb00,0x45fb05)]
branches=0
for i in decode(md,image[text.VirtualAddress:text.VirtualAddress+size],base):
 if not i.id or inside(i.address,DEAD):continue
 if (i.group(capstone.CS_GRP_CALL) or i.group(capstone.CS_GRP_JUMP)) and i.operands and i.operands[0].type==x86.X86_OP_IMM:
  branches+=1;t=i.operands[0].imm
  if any(lo<t<hi for lo,hi in entries):errors.append(f'branch into displaced entry {i.address:x}->{t:x}')
print(f'W2 item text gate: {len(guards)} guards, {len(regions)} region hashes, {caves} copied owners, {checked} live instructions, {branches} external branch targets, {len(errors)} failures')
for error in errors[:50]:print('FAIL',error)
raise SystemExit(bool(errors))
