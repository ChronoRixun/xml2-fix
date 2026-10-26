"""TM1: generated R3 rows must exactly match the approved ledger, with retail bytes."""
import collections
import re
import sys
from pathlib import Path
import pefile

root = Path(__file__).resolve().parents[1]
ledger = Path(sys.argv[1]).read_text(encoding='utf-8')
body = next(b for b in re.findall(r'```ledger\n(.*?)```', ledger, re.S) if '# row R3' in b)
rows = [line.split() for line in body.splitlines() if line and not line.startswith('#')]
assert len(rows) == 82
assert collections.Counter(r[2] for r in rows) == {
    'pool-field': 54, 'capacity': 14, 'last-index': 3, 'bitmap-dwords': 3,
    'alloc': 3, 'owner-guard': 1, 'reader-guard': 4,
}
header = root / 'src/motion_pool_sites.hpp'
assert header.exists(), 'TM1: generated MotionPool site table is missing'
text = header.read_text(encoding='utf-8')
generated = re.findall(r'// ledger (.+)', text)
assert generated == [' '.join(r) for r in rows], 'TM1: table differs from authoritative R3 block'
pe = pefile.PE(sys.argv[2])
image = pe.get_memory_mapped_image()
for va, kind, role, field, off, size, old, new, code in rows:
    raw = bytes.fromhex(code)
    start = int(va, 16)-pe.OPTIONAL_HEADER.ImageBase
    assert image[start:start+len(raw)] == raw, va
    if kind != 'cave':
        o, n = int(off), int(size)
        assert int.from_bytes(raw[o:o+n], 'little') == int(old, 16)
        assert int(new, 16) < 1 << (8*n)
assert {int(r[0], 16) for r in rows if r[2] == 'alloc'} == {0x56a48e, 0x56a528, 0x56a695}
assert {int(r[0], 16) for r in rows if r[2] == 'bitmap-dwords'} == {0x56a125, 0x56a339, 0x56a358}
print('PASS TM1: 82 generated R3 ledger rows, 3 allocations, 3 bitmap clears, 5 caves; retail bytes verified')

# All existing literal byte guards, including bytes_of() expectations, must
# avoid the installed instruction spans. The R3 module owns its own guards.
spans = [(int(r[0], 16), int(r[0], 16)+len(bytes.fromhex(r[8]))) for r in rows]
spans += [(0x56a672, 0x56a677), (0x56b6b9, 0x56b6bf)]
pattern = re.compile(r'0x([0-9a-fA-F]{6,8})\s*,\s*(?:patch_transaction::bytes_of\(\s*)?"([0-9a-fA-F]+)"')
checked, overlaps = 0, []
for path in sorted((root / 'src').rglob('*')):
    if not path.is_file() or path.name in ('motion_pool.cpp', 'motion_pool_sites.hpp'):
        continue
    source = path.read_text(encoding='utf-8', errors='replace')
    for match in pattern.finditer(source):
        checked += 1
        lo = int(match[1], 16)
        hi = lo+len(match[2])//2
        if any(lo < end and start < hi for start, end in spans):
            overlaps.append((path.relative_to(root).as_posix(), source.count('\n', 0, match.start())+1, hex(lo), hex(hi)))
assert not overlaps, overlaps
print(f'PASS R3 guard-vs-site scan: {checked} existing guards, zero overlaps (including observer sites)')
