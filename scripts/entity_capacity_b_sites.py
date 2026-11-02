"""Derive the stage-1 operand ledger from retail instructions, independently of the audit.

The SQLite inventory supplies instruction boundaries/function ownership, not patch addresses.
Capstone decodes and byte-checks every instruction in .text. The audit is only a test oracle.
Usage: python scripts/entity_capacity_b_sites.py --exe <retail> --db <native.sqlite>
"""
import argparse
import hashlib
import json
import sqlite3
import struct
from pathlib import Path
import capstone as cs
from capstone.x86 import X86_OP_MEM, X86_OP_IMM
import pefile

ROOT = Path(__file__).resolve().parents[1]


def layout(n, stride, shadow=False):
    words = (n + 31) // 32
    constructed = n * stride
    ring = constructed + (4 if shadow else words * 4)
    enqueue = ring + (n + 1) * 4
    active = enqueue + 12
    live = active + words * 4
    result = dict(constructed=constructed, ring=ring, enqueue=enqueue,
                  dequeue=enqueue + 4, free=enqueue + 8, active=active, live=live)
    if shadow:
        result.update(generations=live + 4, mask=live + 4 + n * 4,
                      shift=live + 8 + n * 4, extent=live + 12 + n * 4)
    else:
        result['extent'] = live + 4
    return result


def derive(exe, db):
    raw = Path(exe).read_bytes()
    audited = bytearray(raw)                  # a Legends Classic build's exe: the retail one with the LAA flag (E12)
    audited[int.from_bytes(raw[0x3C:0x40], 'little') + 0x16] &= ~0x20
    assert hashlib.sha1(audited).hexdigest() == '7d95cdb4a9a599b982147a3389ea7aff211cb82b'
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    conn = sqlite3.connect(f'file:{Path(db).as_posix()}?mode=ro', uri=True)
    decoder = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_32)
    decoder.detail = True
    insns, owners, bounds = {}, {}, {}
    text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    lo, hi = base + text.VirtualAddress, base + text.VirtualAddress + text.Misc_VirtualSize
    for va, fn, hx in conn.execute('select va,fn,bytes from insn where va>=? and va<? order by va', (lo, hi)):
        b = bytes.fromhex(hx)
        assert image[va-base:va-base+len(b)] == b, hex(va)
        if fn:
            bounds[fn] = max(bounds.get(fn,fn),va+len(b))
        if not (0x491e30 <= va < 0x499b90 or 0x4b49f0 <= va < 0x4b6e50 or
                0x50dee0 <= va < 0x50e4c6 or 0x522a10 <= va < 0x522a70 or bytes.fromhex('48317500') in b or bytes.fromhex('603f7500') in b):
            continue
        decoded = list(decoder.disasm(b, va))
        # Inventory includes data/padding; only complete single instructions are candidates.
        if len(decoded) == 1 and decoded[0].size == len(b):
            insns[va], owners[va] = decoded[0], fn
    old = {p: layout(32, stride, p == 'A8') for p, stride in [('A7', 0x6a8), ('A8', 0x48), ('A9', 0x6c)]}
    new = {p: layout(124, stride, p == 'A8') for p, stride in [('A7', 0x6a8), ('A8', 0x48), ('A9', 0x6c)]}
    # Scope is a subsystem range, never an enumerated set of operand sites.
    ranges = {'A7': (0x491e30, 0x496a00), 'A8': (0x4b49f0, 0x4b6e50), 'A9': (0x497a30, 0x499b90)}
    rows = []
    def add(i, pool, role, field, offset, size, value):
        assert size in (1, 4)
        rows.append(dict(va=i.address, pool=pool, role=role, field=field, offset=offset,
                         size=size, value=value, before=bytes(i.bytes).hex(), fn=owners[i.address] or i.address))
    selected_functions = {p: set() for p in ranges}
    for p, (a, b) in ranges.items():
        for va, i in insns.items():
            if not a <= va < b:
                continue
            for op in i.operands:
                if op.type != X86_OP_MEM or not op.mem.base:
                    continue
                disp = op.mem.disp
                field = next((k for k,v in old[p].items() if v == disp and k != 'extent'), None)
                # The outer manager's bitmap is not the arena's generation array.
                if p == 'A8' and disp == 0x99c and va < 0x4b68a0:
                    field, value = 'outer_active', new[p]['active'] + 8
                elif field:
                    value = new[p][field]
                elif p == 'A8' and 0xa2c <= disp <= 0xa7c:
                    field, value = f'trailing_{disp:x}', disp + new[p]['extent'] - old[p]['extent']
                elif p == 'A8' and disp in (0x9a0, 0x9a4, 0xa24):
                    field, value = f'adjacent_{disp:x}', disp + new[p]['generations'] - old[p]['generations']
                    if disp == 0xa24:
                        value = new[p]['extent']
                else:
                    continue
                add(i, p, 'trailing' if field.startswith('trailing') else 'field', field, i.disp_offset, i.disp_size, value)
                selected_functions[p].add(owners[va])
    for va, i in insns.items():
        if any(o.type==X86_OP_IMM and o.imm==0x753148 or o.type==X86_OP_MEM and o.mem.disp==0x753f60 for o in i.operands):
            selected_functions['A9'].add(owners[va])
    for va, i in insns.items():
        for p in ranges:
            if owners[va] not in selected_functions[p]:
                continue
            imm = [o.imm & 0xffffffff for o in i.operands if o.type == X86_OP_IMM]
            if i.mnemonic == 'cmp' and imm == [32]:
                add(i, p, 'capacity', 'capacity', i.imm_offset, i.imm_size, 124)
            elif i.mnemonic == 'mov' and imm == [32]:
                add(i, p, 'capacity', 'sentinel', i.imm_offset, i.imm_size, 124)
            elif i.mnemonic == 'mov' and imm == [31] and i.operands[0].type != X86_OP_MEM:
                add(i, p, 'seed', 'last', i.imm_offset, i.imm_size, 123)
        # Absolute references anywhere in .text, including CRT init and exit.
        for op in i.operands:
            if op.type == X86_OP_IMM and op.imm == 0x753148:
                add(i, 'A9', 'absolute', 'base', i.imm_offset, i.imm_size, 0)
            elif op.type == X86_OP_MEM and op.mem.disp in (0x753148,0x753f60):
                field='base' if op.mem.disp==0x753148 else 'live'
                add(i, 'A9', 'absolute', field, i.disp_offset, i.disp_size, 0 if field=='base' else new['A9']['live'])
        if i.mnemonic == 'push' and any(o.type == X86_OP_IMM and o.imm in (0xd59c,0xa80) for o in i.operands):
            p = 'A7' if i.operands[0].imm == 0xd59c else 'A8'
            if ranges[p][0] <= va < ranges[p][1]:
                value = new[p]['extent'] if p == 'A7' else 0xa80 + new[p]['extent'] - old[p]['extent']
                add(i,p,'heap','size',i.imm_offset,i.imm_size,value)
    # Stores to bitmap bases are widened. Destructor scans retain a pointer to the
    # constructed bitmap in ESI, so their final indirect zero store is classified too.
    for p in ranges:
        candidates = [r for r in rows if r['pool']==p and r['role']=='field' and r['field'] in ('constructed','active')]
        for r in candidates:
            i = insns[r['va']]
            if i.mnemonic == 'mov' and i.operands[0].type == X86_OP_MEM and not i.operands[0].mem.index:
                add(i,p,'clear',r['field'],0,4,r['value'])
        if p != 'A8':
            for va,i in insns.items():
                if owners[va] not in selected_functions[p] or i.mnemonic != 'mov' or len(i.operands)!=2:
                    continue
                m,v=i.operands
                if m.type==X86_OP_MEM and m.mem.base==cs.x86.X86_REG_ESI and not m.mem.index and m.mem.disp==0 and v.type==X86_OP_IMM and v.imm==0:
                    add(i,p,'clear','constructed_indirect',0,4,0)
    # The five corrected spans are control-flow changes rather than operand edits.
    for va,hx in [(0x50e1c2,'85c074128b5610'),(0x50e217,'05600200008910'),(0x50e3c8,'39b864020000'),(0x50e3e6,'8b8864020000'),(0x50e4aa,'e8b16f0100')]:
        assert image[va-base:va-base+len(bytes.fromhex(hx))].hex()==hx
        rows.append(dict(va=va,pool='AI',role='guard',field='team',offset=0,size=4,value=0,before=hx,fn=0x50dee0))
    rows.sort(key=lambda r:(r['va'],r['role']))
    regions=[]
    for fn in sorted({r['fn'] for r in rows}):
        end=bounds.get(fn,fn+insns[fn].size)
        regions.append(dict(lo=fn,hi=end,hash=hashlib.sha256(image[fn-base:end-base]).hexdigest()))
    reset=next(i for va,i in insns.items() if owners[va]==0x522a10 and i.mnemonic=='mov' and
               any(o.type==X86_OP_MEM and o.mem.disp==0xc384 for o in i.operands))
    return dict(rows=rows,regions=regions,stock=old,raised=new,
                controller_reset=dict(va=reset.address,before=bytes(reset.bytes).hex(),lo=0x522a10,hi=bounds[0x522a10],
                                      hash=hashlib.sha256(image[0x522a10-base:bounds[0x522a10]-base]).hexdigest()))


def header(data):
    out=['#pragma once','// GENERATED by scripts/entity_capacity_b_sites.py; retail SHA1 7d95cdb4a9a599b982147a3389ea7aff211cb82b.',
         '#include <array>','#include <cstdint>','#include <string_view>','namespace entity_capacity_b_sites {',
         'struct site { std::uint32_t va; std::string_view pool, role, field; unsigned offset, size; std::uint32_t value; std::string_view before; };',
         f'inline constexpr std::array<site, {len(data["rows"])}> sites{{{{']
    for r in data['rows']:
        out.append(f'{{0x{r["va"]:x}, "{r["pool"]}", "{r["role"]}", "{r["field"]}", {r["offset"]}, {r["size"]}, 0x{r["value"]:x}, "{r["before"]}"}},')
    out+=['}};','struct region { std::uint32_t lo, hi; std::string_view hash; };',f'inline constexpr std::array<region, {len(data["regions"])}> regions{{{{']
    for r in data['regions']:
        out.append(f'{{0x{r["lo"]:x}, 0x{r["hi"]:x}, "{r["hash"]}"}},')
    r=data['controller_reset']
    return '\n'.join(out+['}};',f'inline constexpr site controller_reset{{0x{r["va"]:x}, "AI", "reset", "controller", 0, 4, 0, "{r["before"]}"}};',
                          f'inline constexpr region controller_reset_region{{0x{r["lo"]:x},0x{r["hi"]:x},"{r["hash"]}"}};','}'])+'\n'


if __name__=='__main__':
    a=argparse.ArgumentParser()
    a.add_argument('--exe',required=True); a.add_argument('--db',required=True)
    a.add_argument('--check',action='store_true')
    args=a.parse_args(); data=derive(args.exe,args.db)
    outputs={ROOT/'src/entity_capacity_b_sites.hpp':header(data),ROOT/'test/actor_pools_sites.json':json.dumps(data,indent=2)+'\n'}
    for path,content in outputs.items():
        if args.check:
            assert path.read_text()==content, f'stale generated file {path}'
        else:
            path.write_text(content,encoding='utf-8',newline='\n')
    print(f'ledger: {len(data["rows"])} roles, {len(data["regions"])} owner regions')
