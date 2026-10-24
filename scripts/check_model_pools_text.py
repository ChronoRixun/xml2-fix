"""Whole-image completeness gate for [Engine] ModelPools (src/model_pools_sites.hpp, src/model_pools_rules.hpp).

Decodes the whole .text of a retail XMen2.exe with capstone and checks, independently of the generator:
  1. every instruction with an immediate or displacement inside the model aggregate (0x7bf6d0..0x7dad68) or the
     particle-system manager (0x7dad78..0x7dc868) is a site row of the table (the plan moves it);
  2. no aligned dword of .rdata or the initialised .data points into either object (no pointer the plan can't see);
  3. no direct branch or call anywhere in .text, and no aligned dword of .text/.rdata/.data (jump tables, unwind
     maps, vtables), lands strictly inside a span the plan overwrites (the imm8 caves, the bitmap clears, the fixed
     write, the moved findNext call);
  4. every direct caller of each findNext helper the pools use is known: a helper with a caller outside the pool's
     own destructor is the 0x403d40 class (shared, cloned) and must be in helper_clones.
The raw-byte scans of the native test (test/model_pools_test.cpp) then prove the plan removes exactly these.

usage: python scripts/check_model_pools_text.py XMen2.exe     (needs capstone and pefile)
"""
import re
import struct
import sys
from pathlib import Path

import capstone
import pefile
from capstone import x86

ROOT = Path(__file__).resolve().parent.parent
SITES = (ROOT / 'src' / 'model_pools_sites.hpp').read_text()
RULES = (ROOT / 'src' / 'model_pools_rules.hpp').read_text()
AGG = (0x7bf6d0, 0x7dad68)
PSYS = (0x7dad78, 0x7dc868)


def inside(v):
    return AGG[0] <= v < AGG[1] or PSYS[0] <= v < PSYS[1]


def main(exe):
    pe = pefile.PE(exe, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
    lo, hi = base + text.VirtualAddress, base + text.VirtualAddress + text.Misc_VirtualSize
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    rows = set(int(v, 16) for v in re.findall(r'\{(0x[0-9a-f]+), "[0-9a-f]+", \d+, \d+,', SITES))
    spans = [(int(a, 16), int(r, 16)) for a, r in re.findall(r'\{(0x[0-9a-f]+), \d+, \d+, (0x[0-9a-f]+)\}', SITES)]
    spans += [(int(v, 16), int(r, 16)) for v, r in re.findall(r'\{(0x[0-9a-f]+), "[0-9a-f]+", kind::\w+, (0x[0-9a-f]+), (?:true|false)', RULES)]
    spans += [(int(v, 16), int(v, 16) + len(b) // 2) for v, b in re.findall(r'\{(0x[0-9a-f]+), "([0-9a-f]+)", "[0-9a-f]+", "', RULES)]
    clones = re.findall(r'\{(0x[0-9a-f]+),\s*"[0-9a-f]+"\s*(?:"[0-9a-f]+"\s*)*,\s*\{[^}]*\}, kind::\w+, (0x[0-9a-f]+), "([0-9a-f]+)"\}', RULES)
    spans += [(int(c, 16), int(c, 16) + 5) for _, c, _ in clones]
    cloned = {int(h, 16) for h, _, _ in clones}
    failures = []
    refs = 0
    calls = {}
    n = 0
    for ins in md.disasm(bytes(img[lo - base:hi - base]), lo):
        n += 1
        if ins.id == 0:
            continue
        for op in ins.operands:
            v = op.imm if op.type == x86.X86_OP_IMM else op.mem.disp & 0xffffffff if op.type == x86.X86_OP_MEM else None
            if v is not None and inside(v):
                refs += 1
                if ins.address not in rows:
                    failures.append(f'uncovered reference {ins.address:08x} {ins.mnemonic} {ins.op_str}')
        if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
            op = ins.operands[0] if ins.operands else None
            if op is not None and op.type == x86.X86_OP_IMM:
                t = op.imm & 0xffffffff
                if ins.mnemonic == 'call':
                    calls.setdefault(t, []).append(ins.address)
                for a, r in spans:
                    if a < t < r and not (a <= ins.address < r):
                        failures.append(f'branch into an overwritten span {ins.address:08x} {ins.mnemonic} {t:08x} (span {a:08x})')
    words = 0
    for s in pe.sections:
        if s.Name.rstrip(b'\0') not in (b'.text', b'.rdata', b'.data'):
            continue
        data = s.get_data()
        sva = base + s.VirtualAddress
        for i in range(0, len(data) - 3, 4):
            v = struct.unpack_from('<I', data, i)[0]
            words += 1
            # 0x6d28d0 lies in the import-name strings ("FileAttributes\0\0|\0DeleteFile..."): text, not a pointer
            if s.Name.rstrip(b'\0') != b'.text' and inside(v) and (sva + i, v) != (0x6d28d0, 0x7c0000):
                failures.append(f'pointer into a moved object at {sva + i:08x}: {v:08x}')
            for a, r in spans:
                # 0x655798 is the tail of `mov dword ptr [ecx+0x14], 0x57` (c7 41 14 57 00 00 00), not a pointer
                if a < v < r and not (s.Name.startswith(b'.text') and sva + i == 0x655798 and v == 0x571441):
                    failures.append(f'dword into an overwritten span at {sva + i:08x}: {v:08x}')
    # findNext helpers of the pools: called from the destructors (bitset<N>::findNext rows, plus helper_clones)
    helpers = set(int(v, 16) for v in re.findall(r'// bitset<\d+>::findNext \((0x[0-9a-f]+)\)', SITES)) | cloned
    # the eight pool destructors (0x571300 .. 0x5719d0) and the particle-system records clear
    ranges = [(0x571300, 0x5713a0), (0x5713a0, 0x571460), (0x571460, 0x571500), (0x571500, 0x5715a0), (0x5715a0, 0x5716a0),
              (0x5716a0, 0x571750), (0x571750, 0x571850), (0x571930, 0x5719d0), (0x578570, 0x578670)]
    in_dtor = lambda c: any(a <= c < b for a, b in ranges)
    for t, sites in sorted(calls.items()):
        if any(in_dtor(c) for c in sites):
            outside = [c for c in sites if not in_dtor(c)]
            if t == 0x577ec0:  # the particle-system record's own destructor (one 24-byte record, ecx = the record)
                continue
            if t not in helpers:
                failures.append(f'destructor callee {t:08x} is not a known findNext helper (callers {[hex(c) for c in sites]})')
            elif outside and t not in cloned:
                failures.append(f'findNext {t:08x} is shared with {[hex(c) for c in outside]} but raised in place')
            print(f'  destructor callee {t:08x}: {len(sites)} callers, outside the pool destructors: {[hex(c) for c in outside]}'
                  f'{" (cloned)" if t in cloned else ""}')
    print(f'{n} instructions over {hi - lo} bytes, {refs} references to the moved objects (all rows: {not any(f.startswith("uncovered") for f in failures)}), '
          f'{words} aligned dwords, {len(spans)} overwritten spans, {len(failures)} failures')
    for f in failures:
        print('FAIL', f)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
