"""Re-derive W3-8's guarded operands/caves from the owned retail executable.

Usage: python tools/generate_script_pool_sites.py <XMen2.exe> --check --verify
Requires capstone and pefile. Never copies the executable into the repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import capstone as cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM
import pefile

ROOT = Path(__file__).resolve().parents[1]
SHA = '146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b'


def layout(base, n, stride):
    w = (n+31)//32
    built = n*stride
    ring = built+4*w
    write = ring+4*(n+1)
    used = write+12
    live = used+4*w
    return dict(base=base, capacity=n, stride=stride, words=w, built=built,
                ring=ring, write=write, read=write+4, free=write+8, used=used,
                live=live, size=live+4)


STOCK = [layout(0xc714,620,56),layout(0x5db8,1556,12),
         layout(0x15918,120,560),layout(0x2f2f4,30,1476)]
RAISED=[]
tail=0x3a284
for old,n in zip(STOCK,(4096,8192,512,128)):
    new=layout(tail,n,old['stride']); RAISED.append(new); tail+=new['size']


def derive(exe):
    raw=exe.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==SHA, 'unsupported retail executable'
    pe=pefile.PE(data=raw); image=pe.get_memory_mapped_image()
    comparison_slot=int.from_bytes(image[0x272564:0x272568],'little')
    imports={s.address:(d.dll,s.name) for d in pe.DIRECTORY_ENTRY_IMPORT for s in d.imports}
    assert image[0x272562:0x272564]==b'\xff\x25' and imports[comparison_slot]==(b'MSVCR71.dll',b'_stricmp')
    md=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32); md.detail=True; md.skipdata=True
    section=next(s for s in pe.sections if s.Name.startswith(b'.text'))
    ins=list(md.disasm(section.get_data(),0x400000+section.VirtualAddress))
    by={i.address:i for i in ins if i.id}
    # Scope owner operands here. Cave spans are decoded again from their known
    # instruction boundaries below, rather than from intervening switch data.
    owner=[i for i in ins if i.id and 0x4d4ea0<=i.address<0x4da600]
    direct={}
    relative={}
    for old,new in zip(STOCK,RAISED):
        direct[old['base']]=new['base']
        direct[old['base']+old['live']]=new['base']+new['live']
        for field in ('built','ring','write','read','free','used','live'):
            relative[old[field]]=new[field]
        # Unrolled four-word bitmaps in the compiled-script constructor.
        if old['capacity']==120:
            for field in ('built','used'):
                for j in (4,8,12):relative[old[field]+j]=new[field]+j
    edits={}
    def put(i,offset,size,value,role):
        row=edits.setdefault(i.address,dict(va=i.address,before=i.bytes.hex(),after=bytearray(i.bytes),role=[]))
        row['after'][offset:offset+size]=value.to_bytes(size,'little');row['role'].append(role)
    # All owner-relative operands and pointer arithmetic, across .text, not just
    # the constructor. Relative template fields are restricted to script code.
    for i in ins:
        if not i.id:continue
        for o in i.operands:
            if o.type==X86_OP_MEM and o.mem.base and o.mem.disp in direct and 0x4d4ea0<=i.address<0x4da600:
                put(i,i.disp_offset,i.disp_size,direct[o.mem.disp],'owner field')
            elif o.type==X86_OP_MEM and o.mem.base and o.mem.disp in relative and 0x4d4ea0<=i.address<0x4da600:
                put(i,i.disp_offset,i.disp_size,relative[o.mem.disp],'pool field')
            elif o.type==X86_OP_IMM and i.mnemonic in ('add','sub') and o.imm in direct:
                assert 0x4d4ea0<=i.address<0x4da600 or i.address in (0x674803,0x67482d,0x67483b,0x674849,0x674873,0x67489d,0x6748ab,0x6748b9), ('unclassified owner arithmetic',hex(i.address))
                put(i,i.imm_offset,i.imm_size,direct[o.imm],'pool pointer arithmetic')
    # Capacity and last-index immediates, scoped to their native pool methods.
    scopes={
        0:[(0x4d4f40,0x4d4fd9),(0x4d5280,0x4d52d6),(0x4d5a20,0x4d5a82),
           (0x4d6180,0x4d61e2),(0x4d7470,0x4d74f0),(0x4d7790,0x4d7812),(0x4d7e60,0x4d7ee9)],
        1:[(0x4d4ea0,0x4d4f39),(0x4d5220,0x4d5274),(0x4d52f0,0x4d538f),
           (0x4d58b0,0x4d5912),(0x4d5ff0,0x4d607c),(0x4d60b0,0x4d6112),
           (0x4d6510,0x4d65ca),(0x4d78a0,0x4d7a04),(0x4d8b30,0x4d8d90),
           (0x4d8ea0,0x4d9030),(0x4d9430,0x4d94cb)],
        2:[(0x4d5a90,0x4d5aef),(0x4d61f0,0x4d624d),(0x4d7c90,0x4d7d10),
           (0x4d7d10,0x4d7deb),(0x4d86c0,0x4d8700)],
        3:[(0x4d5c40,0x4d5c9f),(0x4d6250,0x4d62ad),(0x4d7b10,0x4d7be4),
           (0x4d7c10,0x4d7c90),(0x4d8700,0x4d8740)]}
    wide={}
    for k,ranges in scopes.items():
        n=STOCK[k]['capacity']; nn=RAISED[k]['capacity']
        for i in owner:
            if not any(a<=i.address<b for a,b in ranges):continue
            for o in i.operands:
                if o.type==X86_OP_IMM and o.imm in (n,n-1) and i.mnemonic in ('cmp','mov'):
                    v=nn-(o.imm==n-1)
                    if i.imm_size==1:
                        assert i.bytes[0]==0x83 and i.mnemonic=='cmp'
                        wide[i.address]=bytes([0x81,i.bytes[1]])+struct.pack('<I',v)
                    else:put(i,i.imm_offset,i.imm_size,v,'capacity/last index')
    # REP bitmap clears, including both constructor and reset paths.
    for va,k in [(0x4d5382,1),(0x4d74e5,0),(0x4d8078,0),(0x4d80a1,1),
                 (0x4d83e0,1),(0x4d83ff,1),(0x4d84ae,0),(0x4d84bb,0)]:
        i=next(md.disasm(image[va-0x400000:va-0x400000+16],va));by[va]=i
        assert i.mnemonic=='mov' and i.operands[1].imm==STOCK[k]['words']
        put(i,i.imm_offset,i.imm_size,RAISED[k]['words'],'bitmap word count')
    va=0x4d8795;i=by[va];put(i,1,4,tail,'allocation size')
    caves=[]
    def span(va,minimum=5):
        result=[];size=0
        while size<minimum:
            i=next(md.disasm(image[va+size-0x400000:va+size-0x400000+16],va+size))
            by[i.address]=i;result.append(i);size+=i.size
        return result
    def cave(va,body,fixups,minimum=5,role='imm8 widening'):
        ss=span(va,minimum); before=b''.join(i.bytes for i in ss)
        for i in ss:edits.pop(i.address,None)
        caves.append(dict(va=va,before=before.hex(),after=body.hex(),fixups=fixups,role=role))
        return len(before)
    def jump(body,fixups,target,prefix=b'\xe9'):
        body+=prefix;fixups.append((len(body),target));body+=b'\0'*4
    for va,replacement in sorted(wide.items()):
        body=bytearray();fixups=[];ss=span(va)
        for i in ss:
            if i.address==va:body+=replacement
            elif i.group(cs.CS_GRP_JUMP) or i.group(cs.CS_GRP_CALL):
                assert i.operands[0].type==X86_OP_IMM
                prefix=b'\xe8' if i.mnemonic=='call' else b'\xe9' if i.mnemonic=='jmp' else bytes([0x0f,0x80+(i.bytes[0]&15)]) if i.size==2 else i.bytes[:2]
                jump(body,fixups,i.operands[0].imm,prefix)
            else:body+=edits.get(i.address,{}).get('after',i.bytes)
        jump(body,fixups,va+sum(i.size for i in ss));cave(va,body,fixups)
    # Clear every bitmap word; preserving the complete machine state also
    # preserves constructor, destructor and bulk-reset calling conventions.
    for va,k,field,reg,disp in [
        (0x4d84e9,2,'built',1,None),(0x4d8515,2,'used',1,None),
        (0x4d85ef,3,'built',1,None),(0x4d8607,3,'used',1,None),
        (0x4d7bdc,3,'built',6,0),(0x4d7dde,2,'built',6,0),
        (0x4d8005,3,'used',7,None),(0x4d8051,2,'used',1,0)]:
        off=RAISED[k][field] if disp is None else disp
        body=bytearray(b'\x9c\x60\x8d'+bytes([0xb8|reg])+struct.pack('<I',off)+b'\x31\xc0\xb9'+struct.pack('<I',RAISED[k]['words'])+b'\xfc\xf3\xab\x61\x9d')
        fixups=[];size=sum(i.size for i in span(va));jump(body,fixups,va+size)
        cave(va,body,fixups,role='complete bitmap clear')
    # Observe each native refusal branch, with unchanged return/stack behavior.
    # 1 and 2 are symbolic DLL callback addresses, resolved by make_plan().
    for va,k in [(0x4d7e6e,0),(0x4d6518,1),(0x4d6578,1),(0x4d9450,1),
                 (0x4d86d1,2),(0x4d8711,3)]:
        first=by[va];branch=by.get(va+first.size) or span(va+first.size)[0]
        assert branch.mnemonic=='je'
        existing=next((c for c in caves if c['va']==va),None)
        if existing:caves.remove(existing)
        body=bytearray(wide.get(va,edits.get(va,{}).get('after',first.bytes)))
        fixups=[];size=first.size+branch.size
        jump(body,fixups,va+size,b'\x0f\x85')
        body+=b'\x9c\x60\x6a'+bytes([k]);jump(body,fixups,1,b'\xe8')
        body+=b'\x83\xc4\x04\x61\x9d';jump(body,fixups,branch.operands[0].imm)
        cave(va,body,fixups,size,'refusal observer')
    # Refuse the compiler's null literal before its first virtual call. The
    # native failure epilogue returns 0 and its caller releases the line node.
    va=0x4d89f1;body=bytearray(b'\x85\xc9\x75\x18\x9c\x60\x6a\x04');fixups=[]
    jump(body,fixups,1,b'\xe8');body+=b'\x83\xc4\x04\x61\x9d';jump(body,fixups,0x4d8aa7)
    # Fill JNZ from measured body size (the safe path is after the refusal).
    body[3]=len(body)-4
    body+=b'\x8b\x11\xff\x52\x08';jump(body,fixups,va+5)
    cave(va,body,fixups,role='null value guard')
    va=0x4da450;body=bytearray(b'\x9c\x60\x51');fixups=[];jump(body,fixups,2,b'\xe8')
    body+=b'\x83\xc4\x04\x61\x9d';ss=span(va,8);body+=b''.join(i.bytes for i in ss)
    jump(body,fixups,va+8);cave(va,body,fixups,8,'per-script refusal log reset')
    # The filename cache is a separate unchecked 120-node tree. Enlarging the
    # compiled-object pool alone would let its 121st insertion reuse node 0.
    # Preserve the entire native cache layout; adapt only its four frontends.
    for va,target in [(0x4d6980,3),(0x4d71f0,4),(0x4d94d0,5)]:
        body=bytearray();fixups=[];jump(body,fixups,target)
        cave(va,body,fixups,role='compiled filename cache adapter')
    # Clear the side cache at exactly the native reset phase: after local
    # globals are released, immediately before the native cache root is reset.
    va=0x4d7fc8;body=bytearray();fixups=[]
    jump(body,fixups,6,b'\xe8');jump(body,fixups,va+5)
    cave(va,body,fixups,role='compiled filename cache reset observer')
    rows=[]
    for r in edits.values():
        r['after']=r['after'].hex();r['role']='/'.join(r['role']);r['fixups']=[]
        rows.append(r)
    rows+=caves;rows.sort(key=lambda r:r['va'])
    for a,b in zip(rows,rows[1:]):assert a['va']+len(bytes.fromhex(a['before']))<=b['va'],'overlapping sites'
    # No incoming branch may land in an overwritten cave interior.
    for i in ins:
        if not i.id or not (i.group(cs.CS_GRP_JUMP) or i.group(cs.CS_GRP_CALL)):continue
        if i.operands[0].type!=X86_OP_IMM:continue
        target=i.operands[0].imm
        for r in caves:
            assert not r['va']<target<r['va']+len(bytes.fromhex(r['before'])), (hex(i.address),hex(target),'cave interior')
    # No save-global displacement may be rewritten. Also reject pointers to
    # moved owner fields outside the script subsystem, rather than guessing.
    for r in rows:
        for i in span(r['va'],len(bytes.fromhex(r['before']))):
            for o in i.operands:
                if o.type==X86_OP_MEM:assert not 0x26550<=o.mem.disp<0x2f2f4
    for i in ins:
        if not i.id or 0x4d4ea0<=i.address<0x4da600:continue
        for o in i.operands:
            if o.type==X86_OP_MEM and o.mem.base and o.mem.disp in direct:
                raise AssertionError(('unclassified outside owner reference',hex(i.address),i.op_str))
    # Close the cache-consumer ledger independently of the four-pool operands.
    # All direct readers are behind the three adapted frontends. Construction
    # and reset retain their native map fields and offsets, unused by the shim.
    cache_refs={0x4d69a3,0x4d69a9,0x4d69d2,0x4d6c16,0x4d7258,0x4d7fcd,0x4d7fd3,
                0x4d8593,0x4d8599,0x4d859f,0x4d85a5,0x4d85b4,0x4d94f2,0x4d94f8,0x4d9525}
    seen=set()
    for i in ins:
        if not i.id:continue
        for op in i.operands:
            hit=(op.type==X86_OP_MEM and op.mem.base and op.mem.disp in (0x26f70,0x26f74,0x26f78,0x26f7c,0x7fb4)) or (
                 op.type==X86_OP_IMM and i.mnemonic in ('add','sub') and op.imm in (0x26f70,0x26f7c))
            if hit:
                assert i.address in cache_refs,('unclassified compiled-cache consumer',hex(i.address),i.op_str)
                seen.add(i.address)
    assert seen==cache_refs,('missing compiled-cache consumers',cache_refs-seen)
    return rows,image


def generate(rows,image):
    lines=['#pragma once','// Generated by tools/generate_script_pool_sites.py; do not edit.',
           '// Retail SHA-256: '+SHA,'#include <array>','#include <cstdint>','#include <string_view>',
           'namespace script_pool_sites {',
           'inline constexpr std::uint32_t cache_compare=0x672562;',
           'inline constexpr std::string_view cache_compare_before="'+image[0x272562:0x272568].hex()+'";',
           'struct fixup { unsigned offset; std::uint32_t target; };',
           'struct site { std::uint32_t va; std::string_view before, after; bool cave; std::array<fixup,4> fixups; unsigned fixup_count; };',
           f'inline constexpr std::array<site,{len(rows)}> sites{{{{']
    for r in rows:
        assert len(r['fixups'])<=4
        fixes=','.join('{'+str(o)+','+hex(t)+'}' for o,t in r['fixups'])
        lines += ['    // '+r['role'],f'    {{{hex(r["va"])},"{r["before"]}","{r["after"]}",{str(bool(r["fixups"])).lower()},{{{{{fixes}}}}},{len(r["fixups"])}}},']
    return '\n'.join(lines+['}};','}'])+'\n'


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('exe',type=Path)
    p.add_argument('--check',action='store_true');p.add_argument('--verify',action='store_true')
    p.add_argument('--json',type=Path);a=p.parse_args();rows,image=derive(a.exe)
    text=generate(rows,image);target=ROOT/'src/script_pool_sites.hpp'
    if a.check:assert target.read_text(encoding='utf-8')==text,'generated ledger differs'
    else:target.write_text(text,encoding='utf-8',newline='\n')
    if a.verify:
        for r in rows:assert image[r['va']-0x400000:r['va']-0x400000+len(bytes.fromhex(r['before']))].hex()==r['before']
    if a.json:a.json.write_text(json.dumps(dict(stock=STOCK,raised=RAISED,size=tail,sites=rows),indent=2)+'\n',encoding='utf-8',newline='\n')
    print(f'PASS ScriptPools: {len(rows)} sites, {sum(bool(r["fixups"]) for r in rows)} caves; retail spans, branches, preserved globals verified; size {tail:#x}')


if __name__=='__main__':main()
