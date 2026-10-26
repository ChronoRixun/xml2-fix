"""Derive W3-5/6 operand ledgers from the owner's retail executable (never redistributed).

python -B tools/w3_effect_pools_ledger.py --check --verify [--exe XMen2.exe]
Requires pefile and capstone. No database, disassembly dump or game installation is written.
Entry points below are semantic roots from DESIGN revision 2, not operand addresses.
Within each root we follow its control flow, register aliases and getter results. Every
owner-relative metadata displacement must be classified; a new field fails closed.
"""
import argparse
import hashlib
import os
from pathlib import Path

import capstone as cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
import pefile

ROOT = Path(__file__).resolve().parents[1]
SHA = '146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b'
MD = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_32)
MD.detail = True

ROWS = {
    'effect_vector': dict(n=710, stride=24, presets=84, owner=0x706680,
        getter=0x4165a0, guard=0x70b648, ctor=0x416400,
        methods={0x4159b0:'findNext', 0x415d60:'clear', 0x415e50:'pop',
                 0x415fb0:'free', 0x4160b0:'ring_fill', 0x416210:'allocate',
                 0x416400:'construct', 0x416530:'destroy'},
        consumers=[0x4060d0,0x406130,0x406180,0x4167f0],
        thunk=0x67d9e0),
    'primitive_def': dict(n=816, stride=0x118, presets=0, owner=0x70b64c,
        getter=0x418860, guard=0x70b64c, ctor=0x418720,
        methods={0x4183a0:'findNext', 0x418440:'clear', 0x4184f0:'pop',
                 0x418560:'free', 0x4185f0:'ring_fill', 0x418660:'allocate',
                 0x418720:'construct', 0x4187d0:'release_pointer',0x418850:'destroy'},
        consumers=[0x4188a0], thunk=0x4189f0),
}


def layout(n, stride, presets):
    words = (n+31)//32
    a = n*stride
    ring = a+words*4
    write = ring+(n+1)*4
    b = write+12
    live = b+words*4
    tail = live+4
    if presets:
        return dict(capacity=n, last_index=n-1, bitmap_words=words, bitmap_a=a,
            ring=ring, ring_write=write, ring_read=write+4, ring_count=write+8,
            bitmap_b=b, live=live, presets=tail, preset_count=tail+presets*4,
            size=tail+presets*4+4)
    mask=tail+n*4
    return dict(capacity=n,last_index=n-1,bitmap_words=words,bitmap_a=a,ring=ring,
        ring_write=write,ring_read=write+4,ring_count=write+8,bitmap_b=b,live=live,
        ids=tail,mask=mask,shift=mask+4,size=mask+8)


class Image:
    def __init__(self, path):
        self.raw=Path(path).read_bytes()
        self.sha=hashlib.sha256(self.raw).hexdigest()
        assert self.sha==SHA, 'wrong retail executable: '+self.sha
        self.pe=pefile.PE(data=self.raw)
        assert self.pe.OPTIONAL_HEADER.ImageBase==0x400000
        self.data=self.pe.get_memory_mapped_image()
        self.text=next(s for s in self.pe.sections if s.Name.rstrip(b'\0')==b'.text')
        self.lo=0x400000+self.text.VirtualAddress
        self.hi=self.lo+self.text.Misc_VirtualSize
        # Decode every instruction, resuming after embedded bytes. Call and reference
        # census is independent of the explicitly rooted owner methods.
        MD.skipdata=True
        self.all=list(MD.disasm(self.bytes(self.lo,self.hi-self.lo),self.lo))
        MD.skipdata=False
        self.calls={}
        self.functions={}
        for i in self.all:
            if i.id and i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM:
                self.calls.setdefault(i.operands[0].imm,[]).append(i.address)

    def bytes(self,va,n): return self.data[va-0x400000:va-0x400000+n]

    def fn(self, start):
        """Reachable instructions, including both sides of conditional branches."""
        if start in self.functions:return self.functions[start]
        todo=[start]; seen={}
        while todo:
            at=todo.pop()
            while at not in seen:
                i=next(MD.disasm(self.bytes(at,16),at,count=1),None)
                assert i and i.id, f'undecodable owner instruction {at:x}'
                seen[at]=i
                target=i.operands[0].imm if i.operands and i.operands[0].type==X86_OP_IMM else None
                if i.mnemonic.startswith('ret'): break
                if i.mnemonic.startswith('j'):
                    assert target is not None
                    # A tail jump into another function is followed by its own root.
                    if abs(target-start)<0x1000: todo.append(target)
                    if i.mnemonic=='jmp': break
                at+=i.size
                assert len(seen)<3000, f'runaway function {start:x}'
        self.functions[start]=sorted(seen.values(),key=lambda i:i.address)
        return self.functions[start]


def constructor_model(im,spec):
    """Read the two native clears, ring initialization and tail construction.

    Offsets are extracted by instruction role/order before comparing with any
    calculated layout. This is independent of the operand classifier below.
    """
    ctor=im.fn(spec['ctor'])
    fill=im.fn(next(f for f,r in spec['methods'].items() if r=='ring_fill'))
    capacities=[o.imm for i in fill if i.mnemonic=='cmp' for o in i.operands if o.type==X86_OP_IMM]
    assert len(capacities)==2 and capacities[0]==capacities[1]
    n=capacities[0]
    clears=[i.operands[1].mem.disp for i in ctor if i.mnemonic=='lea' and
        i.operands[0].type==X86_OP_REG and i.reg_name(i.operands[0].reg)=='edi']
    assert len(clears)==2
    a,b=clears
    clear_counts=[i.operands[1].imm for i in ctor if i.mnemonic=='mov' and
        i.operands[0].type==X86_OP_REG and i.reg_name(i.operands[0].reg)=='ecx' and i.operands[1].type==X86_OP_IMM][:2]
    assert len(clear_counts)==2 and clear_counts[0]==clear_counts[1]
    words=clear_counts[0]
    positions=[i.operands[0].mem.disp for i in fill[:6] if i.mnemonic=='mov' and i.operands[0].type==X86_OP_MEM]
    assert len(positions)==3
    w,r,count=positions
    ring_operands=[o.mem.disp for i in fill for o in i.operands if o.type==X86_OP_MEM and o.mem.index]
    assert len(set(ring_operands))==1
    ring=ring_operands[0]
    fill_va=fill[0].address
    call=next(j for j,i in enumerate(ctor) if i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==fill_va)
    live=ctor[call-1].operands[0].mem.disp
    if spec['presets']:
        tail=next(i.operands[1].mem.disp for i in ctor[call+1:] if i.mnemonic=='lea' and i.operands[1].type==X86_OP_MEM)
        preset_count=next(i.operands[0].mem.disp for i in reversed(ctor) if i.mnemonic=='mov' and i.operands[0].type==X86_OP_MEM and
            i.operands[1].type==X86_OP_IMM and i.operands[1].imm==1)
        presets=next(i.operands[1].imm for i in ctor if i.mnemonic=='mov' and i.operands[0].type==X86_OP_MEM and
            i.operands[0].mem.base==cs.x86.X86_REG_ESP and i.operands[1].type==X86_OP_IMM)
        assert presets==spec['presets'] and preset_count==tail+presets*4
        result=dict(capacity=n,last_index=n-1,bitmap_words=words,bitmap_a=a,ring=ring,ring_write=w,ring_read=r,
            ring_count=count,bitmap_b=b,live=live,presets=tail,preset_count=preset_count,size=preset_count+4)
    else:
        tail=next(i.operands[1].mem.disp for i in ctor[call+1:] if i.mnemonic=='lea' and i.operands[1].type==X86_OP_MEM)
        masks=[i.operands[0].mem.disp for i in ctor[call+1:] if i.mnemonic=='mov' and i.operands[0].type==X86_OP_MEM and
            i.operands[0].mem.base==cs.x86.X86_REG_ESI][:2]
        assert len(masks)==2
        getter=im.fn(spec['getter'])
        size=next(i.operands[0].imm for i in getter if i.mnemonic=='push' and i.operands[0].type==X86_OP_IMM and i.imm_size==4)
        result=dict(capacity=n,last_index=n-1,bitmap_words=words,bitmap_a=a,ring=ring,ring_write=w,ring_read=r,
            ring_count=count,bitmap_b=b,live=live,ids=tail,mask=masks[0],shift=masks[1],size=size)
    assert a//n==spec['stride'] and a%n==0
    assert result==layout(spec['n'],spec['stride'],spec['presets']), (result,layout(spec['n'],spec['stride'],spec['presets']))
    return result


def derive(im, name, spec):
    l=constructor_model(im,spec)
    field_offsets={v:k for k,v in l.items() if k not in ('capacity','last_index','bitmap_words','size')}
    sites=[]; guards={}; classified=set(); alias_evidence=[];owner_edges=set()
    def guard(i, why): guards[i.address]=(bytes(i.bytes).hex(),why)
    functions=dict(spec['methods'])
    functions.update({va:'consumer' for va in spec['consumers']})
    functions[spec['getter']]='getter'
    functions[spec['thunk']]='shutdown'
    getter_calls=im.calls.get(spec['getter'],[])
    assert getter_calls, 'getter has no callers'
    # Getter consumers outside these methods are decoded too. Find their function
    # starts by prologue/alignment and require that the caller belongs to its CFG.
    # Most are primitive construction/destruction or effect load/unload; they carry
    # the result to methods but do not address pool metadata directly.
    for call in getter_calls:
        if any(call in {i.address for i in im.fn(f)} for f in functions): continue
        candidates=[]
        for f in sorted(set(im.calls) | set(range(max(im.lo,call-0x3000)&~15,call+1,16))):
            if not max(im.lo,call-0x3000)<=f<=call: continue
            if f in im.calls or im.bytes(f-1,1)==b'\xcc' or im.bytes(f,3) in (b'\x6a\xff\x68',b'\x64\xa1\x00'):
                ins=im.fn(f)
                if call in {i.address for i in ins}: candidates.append(f)
        assert candidates, f'unclassified getter caller {call:x}'
        functions[min(candidates)]='getter_consumer'
    for f,role in sorted(functions.items()):
        instructions=im.fn(f)
        # Owner registers are seeded only for native owner methods. Consumer
        # ownership starts at a getter call, never from a matching displacement.
        # Forward data-flow over the CFG, not a linear alias walk: early refusal
        # epilogues pop nonvolatile registers before the successful branch.
        insmap={i.address:i for i in instructions}
        incoming={f: {'ecx'} if f in spec['methods'] and role!='findNext' else set()}
        todo=[f]
        while todo:
            at=todo.pop();i=insmap[at];ops=i.operands;after=set(incoming[at])
            _,written=i.regs_access()
            after-={i.reg_name(r) for r in written}
            if i.mnemonic=='mov' and len(ops)==2 and ops[0].type==X86_OP_REG:
                if ops[1].type==X86_OP_REG and i.reg_name(ops[1].reg) in incoming[at]:
                    after.add(i.reg_name(ops[0].reg))
                if spec['presets'] and ops[1].type==X86_OP_IMM and ops[1].imm==spec['owner']:
                    after.add(i.reg_name(ops[0].reg))
                if not spec['presets'] and ops[1].type==X86_OP_MEM and ops[1].mem.base==0 and ops[1].mem.disp==spec['owner']:
                    after.add(i.reg_name(ops[0].reg))
            if i.mnemonic=='call':
                if ops[0].type==X86_OP_IMM and 'ecx' in incoming[at]:owner_edges.add((f,ops[0].imm))
                after-={'eax','ecx','edx'}
                if ops[0].type==X86_OP_IMM and ops[0].imm==spec['getter']:after.add('eax')
                if role=='getter' and not spec['presets'] and ops[0].type==X86_OP_IMM and ops[0].imm==0x5605e0:after.add('eax')
            if i.mnemonic=='jmp' and ops[0].type==X86_OP_IMM and 'ecx' in incoming[at] and (ops[0].imm in spec['methods'] or ops[0].imm not in insmap):
                owner_edges.add((f,ops[0].imm))
            successors=[]
            if not i.mnemonic.startswith('ret'):
                if i.mnemonic.startswith('j'):successors.append(ops[0].imm)
                if i.mnemonic!='jmp':successors.append(at+i.size)
            for nxt in successors:
                if nxt not in insmap:continue
                if nxt not in incoming:
                    incoming[nxt]=set(after);todo.append(nxt)
                elif not after<=incoming[nxt]:
                    incoming[nxt]|=after;todo.append(nxt)
        owned=False
        for pos,i in enumerate(instructions):
            aliases=set(incoming.get(i.address,set()))
            ops=i.operands
            for op in ops:
                if op.type==X86_OP_MEM:
                    m=op.mem
                    regs={i.reg_name(m.base),i.reg_name(m.index)}
                    off=m.disp
                    if regs & aliases:
                        owned=True
                        if l['bitmap_a'] <= off < l['size']:
                            assert off in field_offsets, f'unclassified {name} owner operand {i.address:x}: {i.op_str}'
                            field=field_offsets[off]
                            assert i.disp_size==4
                            sites.append((i.address,bytes(i.bytes).hex(),i.disp_offset,4,field,off))
                            classified.add((i.address,i.disp_offset))
                            alias_evidence.append((i.address,','.join(sorted(regs & aliases)),field))
                    elif off in field_offsets and m.base not in (0,cs.x86.X86_REG_ESP,cs.x86.X86_REG_EBP):
                        # Never silently accept a numeric hit without an owner alias.
                        raise AssertionError(f'untraced {name} field {i.address:x}: {i.op_str}')
                if op.type==X86_OP_IMM and i.imm_size:
                    value=op.imm & 0xffffffff
                    field=None
                    if f in spec['methods'] and role!='destroy':
                        if value==spec['n']: field='capacity'
                        elif value==spec['n']-1: field='last_index'
                        elif value==l['bitmap_words'] and role in ('clear','construct'):
                            assert i.mnemonic=='mov' and i.reg_name(ops[0].reg)=='ecx'
                            assert any(j.mnemonic=='rep stosd' for j in instructions[pos+1:pos+4])
                            field='bitmap_words'
                    if role=='getter' and value==l['size']: field='size'
                    if value==spec['n'] and any(o.type==X86_OP_MEM and o.mem.disp==l['live'] and
                            {i.reg_name(o.mem.base),i.reg_name(o.mem.index)} & aliases for o in ops):
                        field='capacity'
                    if name=='effect_vector' and value==spec['owner']: field='object'
                    if field:
                        assert i.imm_size==4
                        sites.append((i.address,bytes(i.bytes).hex(),i.imm_offset,4,field,value))
                        classified.add((i.address,i.imm_offset))
            if i.mnemonic=='call':
                if role in ('getter','construct','destroy','clear'): guard(i,role+' call')
            if role=='getter': guard(i,'getter')
            if role=='shutdown': guard(i,'shutdown')
            if name=='effect_vector' and role in ('construct','destroy','allocate','consumer','getter_consumer'):
                if any(o.type==X86_OP_IMM and o.imm in (83,84,24) for o in ops): guard(i,'preset bound / record stride')
        if f in spec['methods'] and role not in ('findNext','destroy'):
            assert owned, f'method has no owner flow: {f:x}'
    # Every metadata operand in the owned helper ranges must have a classified row,
    # even if an alias-tracker change would otherwise have hidden it.
    for f in spec['methods']:
        for i in im.fn(f):
            for o in i.operands:
                if o.type==X86_OP_MEM and l['bitmap_a']<=o.mem.disp<l['size']:
                    assert (i.address,i.disp_offset) in classified, f'unclassified owner range {i.address:x}'
    find=next(f for f,r in spec['methods'].items() if r=='findNext')
    clear=next(f for f,r in spec['methods'].items() if r=='clear')
    assert len(im.calls.get(find,[]))==1
    assert im.calls[find][0] in {i.address for i in im.fn(clear)}
    assert im.data.find(find.to_bytes(4,'little'))<0, 'findNext has an absolute pointer user'
    assert not any(i.id and i.mnemonic=='jmp' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==find for i in im.all), 'findNext has a tail caller'
    # Entry-point annotations above are accepted only if native call edges prove
    # ownership from the actual getter/static owner, heap allocation, shutdown,
    # and all getter consumers. Merely having a familiar number is insufficient.
    reached=set(functions)-set(spec['methods'])
    for root in reached-{spec['getter'],spec['thunk']}:
        assert any(i.address in getter_calls for i in im.fn(root)), f'root does not consume getter: {root:x}'
    for _ in range(len(functions)):
        reached|={to for source,to in owner_edges if source in reached}
    expected=set(spec['methods'])-{find}
    assert expected<=reached, f'owner call graph not closed: {[hex(f) for f in expected-reached]}'
    # Reader 0x406130 passes record zero (the owner base) on the unset-bit
    # fallback to the oriented-vector arithmetic routine. It consumes a record,
    # not pool metadata; follow and classify that edge explicitly.
    record_edges={(0x406130,0x4036c0)} if spec['presets'] else set()
    for source,target in owner_edges & record_edges:
        assert not any(o.type==X86_OP_MEM and l['bitmap_a']<=o.mem.disp<l['size']
            for i in im.fn(target) for o in i.operands), 'record reader became a metadata consumer'
    unknown_edges={(a,b) for a,b in owner_edges if b not in spec['methods']} - record_edges
    assert not unknown_edges, f'unclassified owner method edges: {[(hex(a),hex(b)) for a,b in unknown_edges]}'
    if not spec['presets']:
        known={i.address for f in functions for i in im.fn(f)}
        for i in im.all:
            if i.id and any(o.type==X86_OP_MEM and o.mem.base==0 and o.mem.disp==spec['owner'] for o in i.operands):
                assert i.address in known, f'unclassified singleton reference {i.address:x}'
    # Recover constructor field values independently from the layout arithmetic.
    ctor=im.fn(spec['ctor'])
    ctordisps={o.mem.disp for i in ctor for o in i.operands if o.type==X86_OP_MEM}
    required=set(field_offsets)
    # ring itself is initialized by the constructor's native ring-fill helper.
    fill=next(f for f,r in spec['methods'].items() if r=='ring_fill')
    ctordisps|={o.mem.disp for i in im.fn(fill) for o in i.operands if o.type==X86_OP_MEM}
    assert required<=ctordisps, (name,required-ctordisps)
    if name=='effect_vector':
        # The only whole-image pointers to the static owner must be generated rows.
        refs={i.address+i.imm_offset for i in im.all if i.id and any(o.type==X86_OP_IMM and o.imm==spec['owner'] for o in i.operands)}
        rawrefs={0x400000+p for p in range(len(im.data)-3) if im.data[p:p+4]==spec['owner'].to_bytes(4,'little')}
        emitted={va+off for va,h,off,size,field,v in sites if field=='object'}
        assert rawrefs==refs==emitted and len(refs)==3, (rawrefs,refs,emitted)
        for va in (0x416a30,0x417bb0):
            ins=im.fn(va)
            allocs=[j for j,i in enumerate(ins) if i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm in (0x416210,0x415fb0)]
            assert len(allocs)==7, (hex(va),len(allocs))
            for j in allocs:
                guard(ins[j],'seven vector allocations / frees')
                if va==0x416a30:
                    store=next(i for i in ins[j+1:j+8] if i.mnemonic=='mov' and i.operands[0].type==X86_OP_MEM and i.operands[0].size==2 and i.operands[1].type==X86_OP_REG and i.reg_name(i.operands[1].reg)=='ax')
                    guard(store,'16-bit vector id store')
                else:
                    loads=[i for i in ins[:j] if i.mnemonic=='movzx' and i.operands[-1].type==X86_OP_MEM and i.operands[-1].size==2]
                    assert loads
                    guard(loads[-1],'zero-extended vector id read')
        parser=im.fn(0x417140)
        parser_alloc=next(j for j,i in enumerate(parser) if i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==0x416210)
        store=next(i for i in parser[parser_alloc+1:] if i.mnemonic=='mov' and i.operands[0].type==X86_OP_MEM and i.operands[0].size==2)
        guard(store,'parser 16-bit vector id store')
    # A decoder-wide census fails on any missing metadata consumer.
    for i in im.all:
        if not i.id: continue
        if any(o.type==X86_OP_MEM and o.mem.disp in field_offsets for o in i.operands):
            assert (i.address,i.disp_offset) in classified, f'unlisted {name} metadata consumer {i.address:x}: {i.op_str}'
    sites=sorted(set(sites))
    # Guard rows that overlap a write use exactly the same retail bytes.
    guardrows=sorted((a,h,w) for a,(h,w) in guards.items())
    lines=['#pragma once','// Generated by tools/w3_effect_pools_ledger.py; do not edit.',
           '// Retail SHA-256: '+im.sha,'#include "effect_pools_rules.hpp"',
           f'namespace {name}_sites {{','using namespace effect_pools_rules;',
           f'inline constexpr std::array<site,{len(sites)}> sites{{{{']
    for va,h,off,size,field,value in sites:
        lines.append(f'    {{0x{va:x},"{h}",{off},{size},field::{field},0x{value:x}}},')
    lines+=['}};',f'inline constexpr std::array<guard,{len(guardrows)}> guards{{{{']
    for va,h,w in guardrows: lines.append(f'    {{0x{va:x},"{h}","{w}"}},')
    lines+=['}};',f'inline constexpr std::array<std::uint32_t,{len(l)}> constructor_model{{{{',
            '    '+','.join(f'0x{v:x}' for v in l.values()),'}};']
    p='vectors' if spec['presets'] else 'primitive_defs'
    for label,n in [('stock',spec['n']),('raised',4096 if spec['presets'] else 2048),('maximum',16384 if spec['presets'] else 8192)]:
        d=layout(n,spec['stride'],spec['presets'])
        order=('capacity','bitmap_words','bitmap_a','ring','ring_write','ring_read','ring_count','bitmap_b','live','ids','mask','shift','presets','preset_count','size')
        lines+=[f'inline constexpr descriptor {label}_model{{'+','.join(f'0x{d.get(k,0):x}' for k in order)+'};',
                f'static_assert({label}_model==describe(pool::{p},{n}));']
    lines+=[
            f'inline constexpr std::array<std::uint32_t,{len(getter_calls)}> getter_callers{{{{',
            '    '+','.join(f'0x{v:x}' for v in getter_calls),'}};',
            f'inline constexpr std::uint32_t find_next_caller=0x{im.calls[find][0]:x};','}']
    return '\n'.join(lines)+'\n',len(sites),len(guardrows),len(alias_evidence)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',default=os.environ.get('XML2_FIX_TEST_EXE',r'D:/tools/re-projects/bin/XMen2.exe'))
    p.add_argument('--check',action='store_true');p.add_argument('--verify',action='store_true')
    args=p.parse_args();im=Image(args.exe)
    print('Retail SHA-256:',im.sha)
    for name,spec in ROWS.items():
        content,sites,guards,flows=derive(im,name,spec)
        path=ROOT/'src'/f'{name}_sites.hpp'
        if args.check:
            assert path.exists() and path.read_bytes()==content.encode(), 'generated file differs: '+str(path)
        else: path.write_bytes(content.encode())
        print(f'PASS {name}: {sites} operand rows, {guards} guards, {flows} traced owner operands; constructor model and private findNext verified')


if __name__=='__main__': main()
