"""Generate/verify the read-only pool census and D1 observer ledger from an owned retail EXE.

No executable or disassembly is redistributed. Seeds identify audited instructions; Capstone
supplies all guarded bytes, instruction boundaries and branch destinations. --check --verify
also rejects incoming branches into any replaced instruction span and scans every src guard,
including the separately maintained CharSounds/MotionPool ledgers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import capstone as cs
import pefile

ROOT = Path(__file__).resolve().parents[1]
SHA = '146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b'
# name, first instruction, comparison/branch, ZF=1 is the refusal condition,
# owner register (Capstone register spelling; empty for non-FX pools).
SEEDS = [
    ('fx_entries', 0x4073ab, 0x4073b5, 'edi'),
    ('fx_128', 0x407506, 0x407510, 'esi'),
    ('fx_64', 0x406cb4, 0x406cbb, 'esi'),
    ('fx_300', 0x406d13, 0x406d1d, 'edi'),
    ('prims', 0x406a8c, 0x406a91, ''),
    ('prims', 0x406b0c, 0x406b11, ''),
    ('affecters', 0x535ed0, 0x535eda, ''),
    ('powerups', 0x5386aa, 0x5386aa, ''),
    ('primary_fx', 0x539560, 0x53956a, ''),
    ('primary_data', 0x539940, 0x539947, ''),
    ('fx_core', 0x53d370, 0x53d37a, ''),
    ('scopes', 0x544830, 0x54483a, ''),
    ('boltons', 0x540630, 0x540637, ''),
    ('attached_boltons', 0x540a60, 0x540a67, ''),
    ('vf_nodes', 0x54c39a, 0x54c39a, ''),
    ('statements', 0x4d7e6e, 0x4d7e74, ''),
    ('values', 0x4d6518, 0x4d651e, ''),
    ('values', 0x4d657e, 0x4d657e, ''),
    ('objects', 0x4d86d1, 0x4d86d4, ''),
    ('contexts', 0x4d8711, 0x4d8714, ''),
    ('zone_globals', 0x4d709f, 0x4d70a6, ''),
    ('zone_globals', 0x4d70a8, 0x4d70ae, ''),
    ('game_globals', 0x4d716c, 0x4d7173, ''),
    ('game_globals', 0x4d7175, 0x4d717b, ''),
    # The compares move with existing raises: observe their unchanged branch instead.
    ('events', 0x4faeb9, 0x4faec3, ''),
    ('fx_files', 0x4194e4, 0x4194ee, ''),
    ('fx_defs', 0x418d90, 0x418d9a, ''),
    ('prim_defs', 0x4188a8, 0x4188b2, ''),
    ('prims_per_effect', 0x4188b4, 0x4188b8, ''),
    ('vectors', 0x416255, 0x41625f, ''),
    ('model_component', 0x5757ae, 0x5757ae, 'esi'),
]
# name, root (dereference when indirect), live offset, cap, evidence instruction.
READS = [
    ('fxA', 0x6f3c50, False, 0x17e0, 374, 0x4073ab),
    ('fxA_128', 0x6f3c50, False, 0x401c, 128, 0x407506),
    ('fxA_64', 0x6f3c50, False, 0x4584, 64, 0x406cb4),
    ('fxA_300', 0x6f3c50, False, 0x4dd4, 300, 0x406d13),
    ('fxB', 0x6f8ee8, False, 0x17e0, 374, 0x4073ab),
    ('fxB_128', 0x6f8ee8, False, 0x401c, 128, 0x407506),
    ('fxB_64', 0x6f8ee8, False, 0x4584, 64, 0x406cb4),
    ('fxB_300', 0x6f8ee8, False, 0x4dd4, 300, 0x406d13),
    ('prims', 0x6f3c38, True, 0x23228, 336, 0x406a83),
    ('affecters', 0x7988c4, True, 0x3c74, 384, 0x535ed0),
    ('powerups', 0x7988d4, True, 0x3634, 128, 0x5386a0),
    ('primary_fx', 0x7988e4, True, 0x1234, 128, 0x539560),
    ('primary_data', 0x7988e8, True, 0x1b2c, 96, 0x539940),
    ('fx_core', 0x798908, True, 0x1634, 128, 0x53d370),
    ('scopes', 0x79b67c, True, 0x1034, 128, 0x544830),
    ('boltons', 0x79ac68, True, 0x31c, 32, 0x540630),
    ('attached_boltons', 0x79ac6c, True, 0x824, 64, 0x540a60),
    ('vf_nodes', 0x79ba3c, True, 0x1244, 192, 0x54c390),
    ('statements', 0x787740, True, 0x15914, 620, 0x4d7e66),
    ('values', 0x787740, True, 0xc090, 1556, 0x4d6570),
    ('objects', 0x787740, True, 0x261a8, 120, 0x4d86c9),
    ('contexts', 0x787740, True, 0x3a07c, 30, 0x4d8709),
    ('zone_globals', 0x787740, True, 0x26550, 32, 0x4d7099),
    ('game_globals', 0x787740, True, 0x26dd8, 100, 0x4d7166),
    ('moves', 0x789e1c, True, 0x1aef8, 332, 0x500222),
    ('fx_files', 0x6f3c3c, True, 0x44fc, 220, 0x4194e4),
    ('fx_defs', 0x6f3c3c, True, 0x869c, 220, 0x418d90),
    ('prim_defs', 0x70b64c, True, 0x38a20, 816, 0x4188a8),
    ('vectors', 0x706680, False, 0x4e70, 710, 0x416255),
]


def ledger(exe):
    raw = exe.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SHA, 'wrong retail executable'
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_32)
    md.detail = True

    def instructions(va, size=64):
        return list(md.disasm(image[va-0x400000:va-0x400000+size], va))

    observers = []
    seeds = list(SEEDS)
    # Every primitive factory checks the same live field. Include all 17, not the
    # constructor's ring-fill loop (which also compares 336).
    for entry in (0x406430,0x4064a0,0x406530,0x4065c0,0x406630,0x4066a0,0x406710,
                  0x406780,0x4067f0,0x406870,0x4068f0,0x406960,0x4069f0,0x406b90,0x406c00):
        ins = instructions(entry)
        compare = next(i for i in ins[:8] if i.mnemonic=='cmp' and i.operands[-1].type==cs.x86.X86_OP_IMM and i.operands[-1].imm==336)
        j = next(i for i in ins if i.address==compare.address+compare.size)
        seeds.append(('prims',compare.address,j.address,''))
    for name, start, branch, owner in seeds:
        insns = instructions(start)
        j = next(i for i in insns if i.address == branch)
        assert j.mnemonic in ('je', 'jne', 'jl'), (hex(branch), j.mnemonic)
        end = branch + j.size
        suffix = []
        for i in insns:
            if i.address >= end and end - start < 5:
                assert not i.group(cs.CS_GRP_JUMP) and not i.group(cs.CS_GRP_CALL)
                suffix.append(i)
                end += i.size
        assert end-start >= 5
        assert all(not i.group(cs.CS_GRP_CALL) and not i.group(cs.CS_GRP_JUMP) for i in insns if i.address<branch),('relative prefix',hex(start))
        prefix = image[start-0x400000:branch-0x400000].hex()
        observers.append(dict(name=name, va=start, hex=image[start-0x400000:end-0x400000].hex(),
                              prefix=prefix, suffix=b''.join(i.bytes for i in suffix).hex(),
                              condition={'je':4,'jne':5,'jl':12}[j.mnemonic],
                              target=j.operands[0].imm, next=end, owner=owner))
    # Entry wrappers call the unmodified native body via a prologue trampoline.
    # Curves: a legitimate preset-zero return leaves ZF clear (the successful
    # final float comparison); the full path XORs EAX,EAX, leaving ZF set.
    # The native fixture differentially verifies both return paths and flags.
    for name, start, size, mode in [('curves',0x416120,6,100),('styles',0x4ffb30,5,101)]:
        insns=instructions(start,size)
        assert sum(i.size for i in insns)==size and all(not i.group(cs.CS_GRP_JUMP) and not i.group(cs.CS_GRP_CALL) for i in insns)
        observers.append(dict(name=name,va=start,hex=image[start-0x400000:start-0x400000+size].hex(),prefix='',suffix='',condition=mode,target=0,next=start+size,owner=''))
    # At this point ESI is zero only when the native particle free-list pop failed.
    for name, start, size in [('particle_systems', 0x579c12, 6)]:
        insns = instructions(start, size)
        assert sum(i.size for i in insns) == size
        observers.append(dict(name=name, va=start, hex=image[start-0x400000:start-0x400000+size].hex(),
                              prefix='', suffix=image[start-0x400000:start-0x400000+size].hex(),
                              condition=0, target=0, next=start+size, owner='esi'))
    # Observe the already-executed native preflight result; never call its request accessors again.
    call = instructions(0x575cb1)[0]
    assert call.mnemonic == 'call' and call.operands[0].imm == 0x5756e0
    observers.append(dict(name='model_preflight', va=call.address, hex=call.bytes.hex(),
                          prefix='', suffix='', condition=0, target=0x5756e0, next=0x575cb6, owner=''))
    # Observe each operation's original virtual getParticleCount once, before
    # scratch selection. No count is clamped and no +0x470 ModelPools byte moves.
    operations=(0x578d20,0x578e10,0x578ec0,0x579000,0x579290,0x5794e0,
                0x579600,0x579800,0x579910,0x5799a0,0x579ae0)
    for index,entry in enumerate(operations):
        limit=operations[index+1] if index+1<len(operations) else 0x579bc0
        insns=instructions(entry,min(256,limit-entry))
        calls=[i for i in insns if i.mnemonic=='call' and any(o.type==cs.x86.X86_OP_MEM and o.mem.disp==0x74 for o in i.operands)]
        assert len(calls)==1,(hex(entry),'particle-count call census',len(calls))
        call=calls[0];suffix=[];end=call.address+call.size
        for i in insns:
            if i.address>=end and end-call.address<5:
                assert not i.group(cs.CS_GRP_JUMP) and not i.group(cs.CS_GRP_CALL)
                suffix.append(i);end+=i.size
        assert end-call.address>=5
        observers.append(dict(name='particle_array_%x'%entry,va=call.address,
            hex=image[call.address-0x400000:end-0x400000].hex(),prefix=call.bytes.hex(),
            suffix=b''.join(i.bytes for i in suffix).hex(),condition=1,target=0,next=end,
            owner=call.reg_name(call.operands[0].mem.base)))
    readers = []
    for name, root, indirect, offset, cap, evidence in READS:
        i = instructions(evidence)[0]
        fields = [o.mem.disp for o in i.operands if o.type == cs.x86.X86_OP_MEM]
        # Move live is verified separately against its pop's increment below.
        assert offset in fields, (name, hex(offset), i.mnemonic, i.op_str)
        if i.mnemonic == 'cmp' and i.operands[-1].type == cs.x86.X86_OP_IMM:
            assert i.operands[-1].imm == cap, (name, 'capacity', cap, i.op_str)
        readers.append(dict(name=name, root=root, indirect=indirect, offset=offset, cap=cap,
                            evidence=evidence, evidence_hex=i.bytes.hex()))
    assert any(i.mnemonic == 'mov' and any(o.type == cs.x86.X86_OP_MEM and o.mem.disp == 0x1aef8 for o in i.operands)
               for i in instructions(0x5001d0, 150)), 'FightMove live increment not found'
    return readers, observers, pe, md


def guard_scan(observers, external):
    spans = []
    sources = [p for p in (ROOT/'src').rglob('*') if p.suffix in ('.cpp','.hpp','.h')]
    sources += [p for folder in external for pattern in ('char_sound*','motion_pool*') for p in folder.glob(pattern) if p.suffix in ('.cpp','.hpp','.h')]
    constants={}
    for path in sorted(set(sources)):
        for name,value in re.findall(r'\b(?:constexpr|const)\s+(?:[\w:<>]+\s+)+([a-zA-Z_]\w*)\s*=\s*(0x[\da-fA-F]+|[0-9]+)\s*[uUlL]*\s*;',path.read_text(encoding='utf-8-sig')):
            constants.setdefault(name,set()).add(int(value,0))
    for path in sorted(set(sources)):
        if path.name.startswith('pool_counter_'): continue
        text = path.read_text(encoding='utf-8-sig')
        # Address/guard pairs, including adjacent C++ string literals and byte escapes.
        for m in re.finditer(r'\b(0x[\da-fA-F]+[uUlL]*|[a-zA-Z_]\w*)\s*,\s*(?:(?:\w+::)*bytes_of\s*\(\s*)?((?:"(?:[^"\\]|\\.)*"\s*)+)', text):
            literals = re.findall(r'"((?:[^"\\]|\\.)*)"', m[2])
            value = ''.join(literals)
            if re.fullmatch(r'[\da-fA-F]+', value) and len(value) % 2 == 0:
                n = len(value)//2
            elif re.fullmatch(r'(?:\\x[\da-fA-F]{2})+', value):
                n = len(value)//4
            else: continue
            token=m[1]
            if token.lower().startswith('0x'): va=int(token.rstrip('uUlL'),16)
            elif token in constants and len(constants[token])==1: va=next(iter(constants[token]))
            else: continue
            spans.append((va, va+n, str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)))
        # Ledger rows with fields between the address and its retail bytes ({0x4194e4, 2, "owner_file_live", "81bf.."}).
        for m in re.finditer(r'\{\s*(0x[\da-fA-F]{6,8})\s*,([^{}\n]{0,160}?)"([\da-fA-F]{2,})"',text):
            if len(m[3])%2: continue
            va=int(m[1],16)
            if not (0x401000<=va<0x700000): continue
            spans.append((va,va+len(m[3])//2,str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)))
        for m in re.finditer(r'\b(0x[\da-fA-F]+)[uUlL]*\s*,\s*\{([^{}]+)\}',text):
            parts=[v.strip() for v in m[2].split(',') if v.strip()]
            if parts and all(re.fullmatch(r'(?:0x[\da-fA-F]+|[0-9]+)[uUlL]*',v) and int(v.rstrip('uUlL'),0)<256 for v in parts):
                va=int(m[1],16)
                spans.append((va,va+len(parts),str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)))
    hits = []; stand_down = []
    for o in observers:
        for lo, hi, source in sorted(set(spans)):
            if o['va'] < hi and lo < o['va']+len(bytes.fromhex(o['hex'])):
                row = (o['name'], hex(o['va']), source, hex(lo), hex(hi))
                (stand_down if stands_down(o['name'], source) else hits).append(row)
    return dict(source_files=len(set(sources)),files_with_spans=len(set(s[2] for s in spans)),guard_spans=len(spans),overlaps=hits,stand_down=stand_down)


# Limits wave 3 (w3/integrated): these row ledgers raise the pool the observer watches. With the row installed its
# bytes are no longer retail and pool_counters.cpp's install_observers leaves that observer out (the row counts its
# own refusals); with the row absent the bytes are retail and the observer hooks them as before. Only these pairs.
STAND_DOWN = {
    'script_pool_sites.hpp': ('statements', 'values', 'objects', 'contexts'),
    'w3_style_manager_sites.hpp': ('events',),
    'effect_template_sites.hpp': ('fx_files', 'fx_defs', 'prims_per_effect'),
    'primitives_per_effect_rules.hpp': ('prims_per_effect',),
    'primitive_def_sites.hpp': ('prim_defs',),
    'effect_vector_sites.hpp': ('vectors',),
    'particle_scratch_sites.hpp': ('particle_array_',),
}
def stands_down(name, source):
    return any(name.startswith(n) for n in STAND_DOWN.get(Path(source).name, ()))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe', type=Path)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--verify', action='store_true')
    ap.add_argument('--external-src', type=Path, action='append', default=[])
    ap.add_argument('--evidence', type=Path)
    args = ap.parse_args()
    reads, observers, pe, md = ledger(args.exe)
    header = '#pragma once\n// Generated by scripts/pool_counter_sites.py from the owned retail executable.\n'
    header += '#include <array>\n#include <cstdint>\n#include <string_view>\nnamespace pool_counter_sites {\n'
    header += 'struct reader { const char* name; std::uint32_t root, offset, cap; bool indirect; };\n'
    header += f'inline std::array<reader,{len(reads)}> readers{{{{\n'
    for r in reads:
        header += '    {"%s",0x%x,0x%x,%d,%s},\n'%(r['name'],r['root'],r['offset'],r['cap'],str(r['indirect']).lower())
    header += '}};\ninline void install_power_layout(std::uint32_t root, std::uint32_t live, std::uint32_t cap) {\n    for(auto& r:readers)if(r.root==root && (root==0x7988d4 || root==0x79ba3c)){r.offset=live;r.cap=cap;}\n}\nstruct observer { const char* name; std::uint32_t va; std::string_view hex, prefix, suffix; unsigned condition; std::uint32_t target, next; const char* owner; };\n'
    header += f'inline constexpr std::array<observer,{len(observers)}> observers{{{{\n'
    for o in observers:
        header += '    {"%s",0x%x,"%s","%s","%s",%d,0x%x,0x%x,"%s"},\n'%(o['name'],o['va'],o['hex'],o['prefix'],o['suffix'],o['condition'],o['target'],o['next'],o['owner'])
    header += '}};\n}\n'
    dest = ROOT/'src/pool_counter_sites.hpp'
    if args.check:
        assert dest.read_text(encoding='utf-8') == header, 'generated ledger is stale'
    else:
        dest.write_text(header,encoding='utf-8',newline='\n')
    result = guard_scan(observers, args.external_src)
    result['external_source_sha256'] = {
        str(p): hashlib.sha256(p.read_bytes()).hexdigest()
        for folder in args.external_src for pattern in ('char_sound*', 'motion_pool*')
        for p in folder.glob(pattern) if p.suffix in ('.cpp', '.hpp', '.h')
    }
    if args.verify:
        md.skipdata = True
        interior = {x for o in observers for x in range(o['va']+1,o['va']+len(bytes.fromhex(o['hex'])))}
        incoming=[]
        text = next(s for s in pe.sections if s.Name.rstrip(b'\0')==b'.text')
        for i in md.disasm(text.get_data(), 0x400000+text.VirtualAddress):
            if i.id and (i.group(cs.CS_GRP_JUMP) or i.group(cs.CS_GRP_CALL)):
                for op in i.operands:
                    if op.type == cs.x86.X86_OP_IMM and op.imm in interior: incoming.append((hex(i.address),hex(op.imm)))
        assert not incoming, ('incoming branch',incoming)
        result['incoming_branches']=incoming
        # The thiscall entry observer copies the eight curve arguments. ECX is
        # a scratch pointer to that copy on return; certify it is dead at every
        # direct caller, and that no absolute function pointer bypasses them.
        ecx_dead=[]
        md.skipdata=True
        for call in md.disasm(text.get_data(),0x400000+text.VirtualAddress):
            if call.id and call.mnemonic=='call' and call.operands[0].type==cs.x86.X86_OP_IMM and call.operands[0].imm==0x416120:
                following=pe.get_memory_mapped_image()[call.address+5-0x400000:call.address+5-0x400000+256]
                for i in md.disasm(following,call.address+5):
                    read,write=i.regs_access()
                    if any(reg in read for reg in (cs.x86.X86_REG_ECX,cs.x86.X86_REG_CX,cs.x86.X86_REG_CL,cs.x86.X86_REG_CH)):raise AssertionError(('curve caller uses scratch ECX',hex(call.address),hex(i.address)))
                    if cs.x86.X86_REG_ECX in write:
                        ecx_dead.append((hex(call.address),hex(i.address)));break
                    if i.mnemonic=='call':
                        assert i.operands[0].type==cs.x86.X86_OP_IMM and i.operands[0].imm==0x416560,('curve ECX call liveness unresolved',hex(call.address),hex(i.address))
                        # Getter has no ECX input: first overwrites CL from its
                        # init flag; its cold path loads ECX with the pool base.
                        assert pe.get_memory_mapped_image()[0x16560:0x16566]==bytes.fromhex('8a0d78667000')
                        continue
                    assert i.mnemonic not in ('ret','jmp'),('curve ECX liveness unresolved',hex(call.address),hex(i.address))
        assert len(ecx_dead)==15,('curve caller census',len(ecx_dead))
        assert b'\x20\x61\x41\x00' not in args.exe.read_bytes(),'unclassified absolute curve allocator pointer'
        result['curve_callers_ecx_dead']=ecx_dead
        pointers=[]
        import struct
        for section in pe.sections:
            if section.Characteristics & 0x20000000: continue
            data=section.get_data()
            for offset in range(0,len(data)-3,4):
                value=struct.unpack_from('<I',data,offset)[0]
                if value in interior:pointers.append((hex(0x400000+section.VirtualAddress+offset),hex(value)))
        assert not pointers,('data pointer into observer span',pointers)
        result['incoming_data_pointers']=pointers
    result.update(readers=len(reads),observers=len(observers),retail_sha256=SHA)
    if args.evidence: args.evidence.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
    return bool(result['overlaps'])


if __name__ == '__main__':
    sys.exit(main())
