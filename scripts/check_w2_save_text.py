"""Step-3 whole-.text gate. Decode retail and the union test's patched image.

Usage: python scripts/check_w2_save_text.py XMen2.exe build/w2-text.bin build/w2-gc.bin
The binary dumps are emitted by xml2_save_manager_test <exe> build/w2 before
external test fakes replace any helper. No game is started. Requires capstone/pefile.
"""
from pathlib import Path
import re
import struct
import sys
import capstone
from capstone import x86
import pefile

ROOT = Path(__file__).resolve().parent.parent
SITES = (ROOT / "src/entity_capacity_sites.hpp").read_text()
# Full replacements leave the original bodies unreachable. GC is copied and
# checked separately; smart methods are reached through the DLL object's vtable.
DEAD = [(0x4654f0,0x465568),(0x4656c0,0x4657a3),(0x465ec0,0x465f89),
        (0x466020,0x46649f),(0x479c10,0x479c46),(0x4b6e70,0x4b7744),
        (0x465e70,0x465eb3),(0x4664a0,0x4664c4),(0x487610,0x48763d),
        (0x466745,0x4667a3),(0x466843,0x466887),(0x466906,0x466932)]
OWNERS = [(0x4654f0,0x466b00),(0x484a30,0x484d20),(0x487320,0x487700)]
OLD = {0x4804,0x4808,0x480a,0x480c,0x4a10,0x4c6c,0x4c70,0x5274,0x5278,
       0x55c4,0x55d8,0x55dc,0x55e0,0xabb8,0xffffaa20}
FOREIGN = [(0x4f7570,0x500450)] # unrelated object, Phase A foreign_tail_ranges

def inside(va, spans):
    return any(lo <= va < hi for lo,hi in spans)

def operands(i):
    return [o.imm & 0xffffffff for o in i.operands if o.type == x86.X86_OP_IMM] + [
        o.mem.disp & 0xffffffff for o in i.operands if o.type == x86.X86_OP_MEM]

def decode(md, data, base):
    # Bound capstone's native detail allocation. One whole-image disasm call
    # can allocate gigabytes even when its Python iterator is consumed lazily.
    off=0
    while off<len(data):
        stop=min(off+0x8000,len(data));end=off
        for i in md.disasm(data[off:min(stop+16,len(data))],base+off):
            if i.address>=base+stop:break
            yield i
            end=i.address+i.size-base
        if end<=off:raise RuntimeError(f"decoder made no progress at {base+off:x}")
        off=end

def main():
    pe=pefile.PE(sys.argv[1]); image=pe.get_memory_mapped_image(); errors=[]
    text=next(s for s in pe.sections if s.Name.startswith(b".text"))
    base=0x400000+text.VirtualAddress; length=text.Misc_VirtualSize
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;md.skipdata=True
    rows=re.findall(r'\{(0x[0-9a-f]+), "([0-9a-f]+)", "([0-9a-f]+)", "save", "([^"\n]+)"',SITES.split("// A relocation:")[0])
    if len(rows)!=178:errors.append(f"save literal inventory is {len(rows)}, expected178")
    coverage=[]
    for va,old,new,role in rows:
        va=int(va,16);old=bytes.fromhex(old);coverage.append((va,va+len(old)))
        if image[va-0x400000:va-0x400000+len(old)]!=old:errors.append(f"retail row guard mismatch {va:08x}")
    coverage += [(int(va,16),int(va,16)+len(bytes.fromhex(old))) for va,old in re.findall(
        r'\{(0x[0-9a-f]+), "([0-9a-f]+)", "[0-9a-f]*", relocation_kind::\w+, symbol\(\d+\), \d+, "save"',SITES)]
    coverage.append((0x484c76,0x484c7d))
    entries=[(lo,lo+(8 if lo==0x485724 else 5)) for lo in (0x4654f0,0x4656c0,0x465ec0,0x479c10,0x485724)]
    count=tail_refs=branch_checks=0
    for i in decode(md,image[text.VirtualAddress:text.VirtualAddress+length],base):
        count+=1
        if i.id==0:continue
        if i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL):
            for o in i.operands:
                if o.type!=x86.X86_OP_IMM:continue
                branch_checks+=1
                for lo,hi in entries:
                    if lo<o.imm<hi:errors.append(f"control transfer into overwritten entry {i.address:08x}->{o.imm:08x}")
        values=operands(i)
        # Whole image manager-tail sweep: values are uniquely class-owned,
        # except for the pre-existing, explicitly enumerated foreign object.
        if any(0x1ad60<=v<0x1afdc for v in values) and not inside(i.address,FOREIGN):
            tail_refs+=1
            if not inside(i.address,coverage+DEAD):errors.append(f"unledgered manager tail {i.address:08x} {i.mnemonic} {i.op_str}")
    patched=Path(sys.argv[2]).read_bytes()
    if len(patched)!=length:errors.append(f"patched .text length {len(patched)}, expected{length}")
    live_refs=0
    for i in decode(md,patched,base):
        if i.id==0 or inside(i.address,DEAD):continue
        values=operands(i)
        stale_tail=any(0x1ad60<=v<0x1afdc for v in values) and not inside(i.address,FOREIGN)
        stale_record=inside(i.address,OWNERS) and any(v in OLD for v in values)
        if stale_tail or stale_record:
            errors.append(f"old save operand survives {i.address:08x} {i.mnemonic} {i.op_str}")
        if inside(i.address,OWNERS):live_refs+=1
        if inside(i.address,OWNERS) and i.mnemonic=="add" and any(o.type==x86.X86_OP_IMM and o.imm==0x18 for o in i.operands):
            errors.append(f"old state stride survives {i.address:08x}")
    clone_count=0
    for i in md.disasm(Path(sys.argv[3]).read_bytes(),0x10000000):
        if i.id==0:continue
        clone_count+=1
        if any(v in OLD or 0x1ad60<=v<0x1afdc for v in operands(i)):
            errors.append(f"old operand in GC clone +{i.address-0x10000000:x} {i.mnemonic} {i.op_str}")
        if i.mnemonic=="add" and any(o.type==x86.X86_OP_IMM and o.imm==0x18 for o in i.operands):
            errors.append(f"old stride in GC clone +{i.address-0x10000000:x}")
    print(f"T4: {count} decoded .text items; {tail_refs} manager-tail references; {live_refs} live save-owner instructions; {clone_count} GC instructions; {branch_checks} branch/call targets; {len(rows)} retail literal guards")
    for e in errors[:30]:print("FAIL",e)
    print(f"W2 save text gate: {len(errors)} failure(s)")
    return bool(errors)
if __name__=="__main__":raise SystemExit(main())
