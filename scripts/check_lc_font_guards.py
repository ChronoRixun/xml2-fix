"""Read-only census of scoped font guards and every retail array/stride reader.

Usage: python scripts/check_lc_font_guards.py <owned XMen2.exe>
Developer-only dependencies: pefile and capstone. No game process is started.
"""
from pathlib import Path
import hashlib,re,struct,sys
import capstone,pefile
root=Path(__file__).resolve().parents[1]
pe=pefile.PE(sys.argv[1]);data=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
text=(root/'src/lc_fonts_sites.hpp').read_text()
sites=[(int(a,16),bytes.fromhex(b)) for a,b in re.findall(r'\{0x([0-9a-f]+),"([0-9a-f]+)"',text)]
regions=[(int(a,16),int(b,16),h) for a,b,h in re.findall(r'\{0x([0-9a-f]+),0x([0-9a-f]+),"([0-9a-f]+)"',text)]
for va,expected in sites:assert data[va-base:va-base+len(expected)]==expected,hex(va)
for lo,hi,h in regions:assert hashlib.sha256(data[lo-base:hi-base]).hexdigest()==h,hex(lo)
for va,name in ((0x687fdc,b'medium'),(0x69d624,b'big'),(0x69d620,b'hud'),(0x69d618,b'digital')):
    assert data[va-base:va-base+len(name)+1]==name+b'\0',hex(va)
thunk=data[0x672562-base:0x672568-base];assert thunk[:2]==b'\xff\x25'
iat=struct.unpack_from('<I',thunk,2)[0]
assert any(f.address==iat and f.name==b'_stricmp' for group in pe.DIRECTORY_ENTRY_IMPORT for f in group.imports)
watched={0x1c18,0x7068,0x706c,0x6e4344}
scan=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);scan.detail=True;scan.skipdata=True
readers=[]
for section in pe.sections:
    if not section.Characteristics&0x20000000:continue
    for instruction in scan.disasm(section.get_data(),base+section.VirtualAddress):
        if not instruction.id:continue
        values=[op.imm for op in instruction.operands if op.type==capstone.x86.X86_OP_IMM]
        values += [op.mem.disp for op in instruction.operands if op.type==capstone.x86.X86_OP_MEM]
        if any(v in watched or 0x8161f8<=v<=0x81d268 for v in values):
            assert any(lo<=instruction.address and instruction.address+instruction.size<=hi for lo,hi,_ in regions),hex(instruction.address)
            readers.append(hex(instruction.address))
# Raw operand scan catches a reference even if a linear disassembler starts on
# an embedded jump table. All known absolute roots/field offsets are accounted.
for value in (0x8161f8,0x8161fc,0x81d260,0x81d268,0x7068,0x706c,0x1c18,0x6e4344):
    for section in pe.sections:
        if not section.Characteristics&0x20000000:continue
        raw=section.get_data();needle=struct.pack('<I',value)
        for m in re.finditer(re.escape(needle),raw):
            va=base+section.VirtualAddress+m.start()
            assert any(lo<=va and va+4<=hi for lo,hi,_ in regions),(hex(value),hex(va))
print(f'PASS {len(sites)} patch-site guards, {len(regions)} region hashes, {len(readers)} slot/root/stride references; raw operand census, four name strings and _stricmp semantics verified')
