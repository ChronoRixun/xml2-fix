"""Whole-image completeness gate for [Limits] IGBCache (src/limits_rules.hpp: igb_sites, igb_references, igb_guards,
find_next_200).

Decodes the whole .text of a retail XMen2.exe with capstone and checks, independently of the table's generator:
  1. every instruction anywhere in .text with an immediate or displacement inside the moved object
     (CIGBInfoCache2, 0x7b7fb0..0x7bf6c8) is a reference row (the getter's three, the atexit thunk's);
  2. no aligned dword of .rdata or the initialised .data points into the object;
  3. across the WHOLE .text, every cache/name field or singleton-base operand must be a patch row
     or an exact-byte entry in igb_cache_text_allowlist.tsv (no address-range exemptions). Additionally,
     inside the cache's own code (0x56e840..0x56fa34 and ring<200>::pop 0x491180..0x4911ac) every 32-bit
     immediate or displacement holding one of the pool's retail field values (200, 199, the ten field offsets,
     the ring-relative 0x328 / 0x32c) is a site row, and every 8-bit one that touches a bitmap is a known,
     accepted one (the constructor's unrolled clears run on the DLL's zero-filled block; the pool destructor's
     tail clears bitmap A once, at exit);
  4. the class's vtable (0x69c034) is stored only by the cache's own code - no second CIGBInfoCache2 exists
     anywhere with the old layout - and no data dword other than that vtable and the atexit registration points
     at any of the pool's methods;
  5. every direct call or jump to a pool method comes from the cache's own code or the atexit thunk, and the
     shared bitset<200>::findNext (0x490d70) has exactly the two outside callers the clone leaves on it.
The native test (xml2_test --igb-cache-rules) then proves the writes change exactly the listed operands and runs the
patched pool code at 1024 records.

usage: python scripts/check_igb_cache_text.py XMen2.exe --self-test     (needs capstone and pefile)
"""
import re
import struct
import sys
from pathlib import Path

import capstone
import pefile
from capstone import x86

ROOT = Path(__file__).resolve().parent.parent
RULES = (ROOT / 'src' / 'limits_rules.hpp').read_text()
OBJ = (0x7b7fb0, 0x7bf6c8)
GUARD = 0x7bf6c8
VTABLE = 0x69c034
CODE = [(0x56e840, 0x56fa34), (0x491180, 0x4911ac)]
FIND_NEXT = 0x490d70
FIELDS = {0xc8, 0xc7, 0x7080, 0x709c, 0x73c0, 0x73c4, 0x73c8, 0x73cc, 0x73e8, 0x73ec, 0x770c, 0x7710}
RING_FIELDS = {0x328, 0x32c}
# pool and cache methods (igb-cache.md section 3) and the object destructor
# (releaseGroup 0x56ea30 is left out: it holds no pool field and the linker shares it with four other caches' vtables)
METHODS = {0x56e840, 0x56e8f0, 0x56e960, 0x56e9c0, 0x56ea50, 0x56eb40, 0x56ebb0, 0x56ebe0, 0x56ed00, 0x56f640,
           0x56f680, 0x56f7e0, 0x56f850, 0x56f880, 0x56f8c0, 0x56f940, 0x56f980, 0x56f9d0, 0x491180}
# The unrolled seven-dword bitmap clears of the cache code (dword stores at +4..+0x18 of a bitmap pointer), accepted
# with the reason in the module docstring: any other such store would be a bitmap clear the plan doesn't know.
ACCEPTED_IMM8 = {0x56e8d6, 0x56e8d9, 0x56e8dc, 0x56e8df, 0x56e8e2, 0x56e8e5,   # pool destructor tail (bitmap A, at exit)
                 0x56e96d, 0x56e970, 0x56e973, 0x56e976, 0x56e979, 0x56e97c,   # constructor: bitmap A (zero-filled block)
                 0x56e99b, 0x56e99e, 0x56e9a1, 0x56e9a4, 0x56e9a7, 0x56e9aa}   # constructor: bitmap B (zero-filled block)


# Independent retail layout inventory, NOT derived from the patch rows: removing a
# row must not remove the value being searched for. The common 0xf is the name
# bitmap's imm32 word count; fixed record-internal disp8 fields do not relocate.
NAME_POINTER = 0x7ac244  # stable singleton pointer, not the dynamically allocated table
ALL_FIELDS = FIELDS | RING_FIELDS | {
    0x1c2, 0x1c1, 0xf, 0xb064, 0x8ca4, 0x93b0, 0x93b4, 0x93b8,
    0x93bc, 0x93f8, 0x70c, 0x710, 0x714, 0x9404, 0x9408, 0x940c,
    0x9414, 0x941, 0xb028, 0x1c20,
}


def load_allowlist():
    # Address + operand offset + WHOLE instruction bytes. No range or value-only
    # exemptions: even a changed instruction at an accepted address must fail.
    result = {}
    path = ROOT / 'scripts' / 'igb_cache_text_allowlist.tsv'
    for line in path.read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        va, offset, code, reason = line.split('\t', 3)
        key = (int(va, 16), int(offset))
        if key in result or not reason.strip():
            raise ValueError(f'invalid allowlist entry: {line}')
        result[key] = bytes.fromhex(code)
    return result


def in_code(va):
    return any(a <= va < b for a, b in CODE)


def table(name):
    m = re.search(r'inline constexpr std::array<(?:actor_site|name_site), \d+> ' + name + r'\{\{(.*?)\}\};', RULES, re.S)
    return [(int(v, 16), h, int(o), int(s)) for v, h, o, s in re.findall(r'\{(0x[0-9a-f]+), "([0-9a-f]+)", (\d+), (\d+),', m.group(1))]


def scan(pe, img=None, verbose=True):
    base = pe.OPTIONAL_HEADER.ImageBase
    if img is None:
        img = pe.get_memory_mapped_image()
    text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
    lo, hi = base + text.VirtualAddress, base + text.VirtualAddress + text.Misc_VirtualSize
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    sites = table('igb_sites')
    refs = table('igb_references')
    names = table('name_sites')
    all_ops = {(va, o): bytes.fromhex(h) for va, h, o, _ in sites + names}
    allowed = load_allowlist()
    allowed_seen = set()
    site_ops = {(va, o) for va, _, o, _ in sites}
    ref_ops = {(va, o) for va, _, o, _ in refs}
    failures = []
    calls = {}
    n = obj_refs = field_ops = vt_refs = 0
    imm8_seen = set()
    for ins in md.disasm(bytes(img[lo - base:hi - base]), lo):
        n += 1
        if ins.id == 0:
            continue
        code = in_code(ins.address)
        for op in ins.operands:
            if op.type == x86.X86_OP_IMM:
                v, off, size = op.imm & 0xffffffff, ins.imm_offset, ins.imm_size
            elif op.type == x86.X86_OP_MEM:
                v, off, size = op.mem.disp & 0xffffffff, ins.disp_offset, ins.disp_size
            else:
                continue
            # All moved offsets are disp32; capacities and bitmap word counts in the
            # patch tables are imm32. Do not exempt stack operands or code regions.
            if size == 4 and (v in ALL_FIELDS or v == NAME_POINTER):
                key = (ins.address, off)
                expected = all_ops.get(key)
                if expected is not None and bytes(ins.bytes) == expected:
                    pass
                elif key in allowed and bytes(ins.bytes) == allowed[key]:
                    allowed_seen.add(key)
                else:
                    failures.append(f'uncovered whole-text field/base {ins.address:08x} {ins.mnemonic} {ins.op_str} (value {v:#x})')
            if OBJ[0] <= v < OBJ[1]:
                obj_refs += 1
                if (ins.address, off) not in ref_ops:
                    failures.append(f'uncovered reference to the object {ins.address:08x} {ins.mnemonic} {ins.op_str}')
            if v == GUARD and not (0x56f9f0 <= ins.address < 0x56fa34):
                failures.append(f'the init guard used outside the getter {ins.address:08x} {ins.mnemonic} {ins.op_str}')
            if v == VTABLE and op.type == x86.X86_OP_IMM:
                vt_refs += 1
                if not code:
                    failures.append(f'the CIGBInfoCache2 vtable stored outside the cache {ins.address:08x} {ins.mnemonic} {ins.op_str}')
            if code and size == 4 and (v in FIELDS or (ins.address < 0x500000 and v in RING_FIELDS)):
                field_ops += 1
                if (ins.address, off) not in site_ops:
                    failures.append(f'uncovered field {ins.address:08x} {ins.mnemonic} {ins.op_str} (value {v:#x})')
            if (code and size == 1 and op.type == x86.X86_OP_MEM and op.mem.index == x86.X86_REG_INVALID and ins.mnemonic == 'mov' and v in (4, 8, 0xc, 0x10, 0x14, 0x18)
                    and ins.operands[0].type == x86.X86_OP_MEM and ins.operands[1].type == x86.X86_OP_REG and ins.address < 0x56ea00):
                imm8_seen.add(ins.address)
        if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
            op = ins.operands[0] if ins.operands else None
            if op is not None and op.type == x86.X86_OP_IMM:
                calls.setdefault(op.imm & 0xffffffff, []).append(ins.address)
    # 2 and 4: data pointers into the object or at the methods
    words = 0
    vt_slots = set(range(VTABLE, VTABLE + 0x20, 4))
    for s in pe.sections:
        name = s.Name.rstrip(b'\0')
        if name not in (b'.rdata', b'.data'):
            continue
        data = s.get_data()
        sva = base + s.VirtualAddress
        for i in range(0, len(data) - 3, 4):
            v = struct.unpack_from('<I', data, i)[0]
            words += 1
            if OBJ[0] <= v < OBJ[1]:
                failures.append(f'pointer into the object at {sva + i:08x}: {v:08x}')
            if v in METHODS and sva + i not in vt_slots:
                failures.append(f'pointer at a pool method at {sva + i:08x}: {v:08x}')
    # 5: callers
    for t in sorted(METHODS):
        for c in calls.get(t, []):
            if not in_code(c) and c != 0x67e195:
                failures.append(f'pool method {t:08x} called from outside the cache at {c:08x}')
    outside = sorted(c for c in calls.get(FIND_NEXT, []) if not in_code(c))
    inside = sorted(c for c in calls.get(FIND_NEXT, []) if in_code(c))
    if outside != [0x4916b9, 0x57d8d0]:
        failures.append(f'bitset<200>::findNext outside callers {[hex(c) for c in outside]} (expected 0x4916b9, 0x57d8d0)')
    clone_rows = sorted(va for va, h, o, s in refs if h.startswith('e8'))
    if inside != clone_rows:
        failures.append(f'the cache calls findNext at {[hex(c) for c in inside]}, the table re-aims {[hex(c) for c in clone_rows]}')
    if calls.get(0x491180, []) != [0x56f6b9]:
        failures.append(f'ring<200>::pop callers {[hex(c) for c in calls.get(0x491180, [])]} (expected only 0x56f6b9)')
    if allowed_seen != set(allowed):
        failures.append(f'allowlist entries absent or changed: {sorted(set(allowed) - allowed_seen)}')
    if imm8_seen != ACCEPTED_IMM8:
        failures.append(f'unrolled bitmap stores {sorted(hex(a) for a in imm8_seen ^ ACCEPTED_IMM8)} differ from the accepted set')
    if len(site_ops) != len(sites) or field_ops != len(sites):
        failures.append(f'{field_ops} field operands found, {len(sites)} rows')
    if verbose:
        print(f'{n} instructions over {hi - lo} bytes; {obj_refs} references to the object, {field_ops} field operands in the cache code '
          f'({len(sites)} site rows), {len(imm8_seen)} unrolled bitmap stores, {vt_refs} vtable stores, {words} aligned data dwords; '
          f'{len(names)} name rows, {len(allowed_seen)} exact allowlist operands; '
          f'findNext callers inside {len(inside)} outside {len(outside)}; {len(failures)} failures')
    if verbose:
        for f in failures:
            print('FAIL', f)
    return failures


def main(exe, self_test=False):
    pe = pefile.PE(exe, fast_load=True)
    failures = scan(pe)
    if self_test:
        # Independent review's missed reader: caller is outside every cache code range.
        # Never write the executable, even a temporary copy, and require THIS diagnostic.
        img = bytearray(pe.get_memory_mapped_image())
        va = 0x560e90
        reader = b'\xe8' + struct.pack('<i', 0x56f9f0 - (va + 5)) + bytes.fromhex('8b80ec730000c3')
        offset = va - pe.OPTIONAL_HEADER.ImageBase
        img[offset:offset + len(reader)] = reader
        negative = scan(pe, img, verbose=False)
        caught = any('uncovered whole-text field/base 00560e95' in f for f in negative)
        print('synthetic external live-count reader:', 'rejected (PASS)' if caught else 'MISSED (FAIL)')
        if not caught:
            failures.append('negative control was not caught by the whole-text field scan')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], '--self-test' in sys.argv[2:]))
