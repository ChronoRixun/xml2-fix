"""TS1: independent R1 ledger, retail full-text operand sweep and guard overlap scan.

python -B test/char_sound_sites_test.py <owned retail exe>
Requires the developer's pefile and capstone; reads inputs only.
"""
import re
import sys
from pathlib import Path
import capstone as C
import pefile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/char_sound_sites.hpp').read_text(encoding='utf-8')
rows = [(int(va, 16), bytes.fromhex(code), int(off), kind)
        for va, code, off, kind in re.findall(r'\{0x([0-9a-f]+),"([0-9a-f]+)",(\d+),value::(\w+)\}', source)]
expected = {
    0x438cb8: ('83f810', 2, 'bound'), 0x438cc3: ('05480d7100', 1, 'base'),
    0x438cc9: ('b8480d7100', 1, 'base'), 0x438dfc: ('bb4c0d7100', 1, 'key'),
    0x438e50: ('05480d7100', 1, 'base'), 0x438e8d: ('81fb0c1b7100', 2, 'end'),
    0x438eb4: ('81c6480d7100', 2, 'base'), 0x42198d: ('83f810', 2, 'bound'),
}
assert len(rows) == 8
assert {a: (b.hex(), o, k) for a, b, o, k in rows} == expected
pe = pefile.PE(sys.argv[1])
image = pe.get_memory_mapped_image()
assert pe.OPTIONAL_HEADER.ImageBase == 0x400000
for va, code, _, _ in rows:
    assert image[va-0x400000:va-0x400000+len(code)] == code, hex(va)
cs = C.Cs(C.CS_ARCH_X86, C.CS_MODE_32)
cs.detail = True
# Continue past embedded data just as the design's exhaustive linear sweep does.
cs.skipdata = True
refs = set()
instructions = 0
for section in pe.sections:
    if not section.Characteristics & 0x20000000:
        continue
    for insn in cs.disasm(section.get_data(), 0x400000+section.VirtualAddress):
        if insn.id == 0:
            continue
        instructions += 1
        for operand in insn.operands:
            value = operand.imm if operand.type == C.x86.X86_OP_IMM else operand.mem.disp if operand.type == C.x86.X86_OP_MEM else -1
            if 0x710d48 <= value < 0x711b10:
                refs.add(insn.address)
assert refs == {0x438cc3, 0x438cc9, 0x438dfc, 0x438e50, 0x438e8d, 0x438eb4, 0x67a110, 0x67dad0}, [hex(a) for a in sorted(refs)]
print(f'PASS TS1: 8 capacity sites; {instructions} instructions swept; 8 absolute operands (6 moved, 2 CRT retained)')
# Check every literal address/hex guard in every src file, including guards
# expressed through bytes_of(). Exclude only this module's own expectations.
spans = [(a, a+len(b)) for a, b, _, _ in rows] + [(0x4239be, 0x4239c3)]
guards = 0
overlaps = []
rx = re.compile(r'0x([0-9a-fA-F]{6,8})\s*,\s*(?:patch_transaction::bytes_of\(\s*)?"([0-9a-fA-F]+)"')
for file in sorted((root / 'src').rglob('*')):
    if not file.is_file() or file.name in ('char_sound_sites.hpp', 'char_sounds.cpp'):
        continue
    text = file.read_text(encoding='utf-8', errors='replace')
    for match in rx.finditer(text):
        guards += 1
        lo = int(match[1], 16)
        hi = lo+len(match[2])//2
        if any(lo < end and start < hi for start, end in spans):
            overlaps.append((file.relative_to(root).as_posix(), text.count('\n', 0, match.start())+1, hex(lo), hex(hi)))
assert not overlaps, overlaps
print(f'PASS guard-vs-site scan: {guards} existing literal guards, zero overlaps with R1 or observer')
