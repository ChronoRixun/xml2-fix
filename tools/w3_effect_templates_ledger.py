"""Derive W3-4/W3-7 operands from the owned retail image and instruction inventory.

No game is started. Requires capstone/pefile; --check compares the generated header;
--verify also checks DLL consumers and branches into every overwritten span.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import sqlite3
import struct
import capstone as cs
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_ESP, X86_REG_EBP
import pefile

ROOT = Path(__file__).resolve().parents[1]
SHA = '146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b'
FUNCTIONS = {int(x, 16) for x in '''405100 405340 405430 405690 4056a0 405710
405dd0 405f70 4063c0 4072e0 407630 4078d0 407a40 40b080 40b2b0
418a20 418af0 418b60 418c00 418c90 418cf0 418d50 418d90 418f80
419130 419180 4192c0 4193a0 419430 419570 419700'''.split()}

def layout(n):
    words = (n + 31) // 32
    f = dict(ring=n*76+4)
    f['write'] = f['ring']+(n+1)*4
    f.update(read=f['write']+4, free=f['write']+8, bitmap=f['write']+12)
    f['live'] = f['bitmap']+words*4
    f['values'] = f['live']+4
    f['size'] = f['values']+(n+1)*4
    d = dict(bitmap_a=n*68)
    d['ring'] = d['bitmap_a']+words*4
    d['write'] = d['ring']+(n+1)*4
    d.update(read=d['write']+4, free=d['write']+8, bitmap_b=d['write']+12)
    d['live'] = d['bitmap_b']+words*4
    d['ids'] = d['live']+4
    d['mask'] = d['ids']+n*4
    d.update(shift=d['mask']+4, size=d['mask']+8)
    base = 12+f['size']
    fields = {f'file_{k}':v for k,v in f.items() if k!='size'}
    fields.update({f'def_{k}':v for k,v in d.items() if k!='size'})
    fields.update(owner_def=base, owner_mask=base+d['mask'], owner_live=base+d['live'],
                  owner_values=12+f['values'], owner_file_live=12+f['live'],
                  owner_scope=base+64, owner_size=base+d['size'],
                  ring_write=(n+1)*4, ring_read=(n+2)*4, ring_free=(n+3)*4,
                  capacity=n, last=n-1, words=words)
    return fields

# Full instruction spans, including all five unrolled bitmap clears (not just
# the file constructor), derived/checked against the instruction inventory.
CLEARS = [(0x4053d6,0x4053ea,'def_bitmap_a',7,1),
          (0x405ddb,0x405def,'def_bitmap_a',1,0),
          (0x405e09,0x405e1d,'def_bitmap_b',1,2),
          (0x405f9c,0x405fb0,'file_bitmap',1,0),
          (0x4196cf,0x4196e3,'file_bitmap',0,2)]
# W3-7 spans also contain complete instructions needed for a >=5-byte jump.
ELEMENTS = [(0x4188cd,0x4188d7,'append'), (0x407a98,0x407a9f,'classify'),
            (0x40b181,0x40b188,'spawn_cursor'), (0x40b480,0x40b486,'spawn'),
            (0x4189c0,0x4189c4,'release'), (0x418cd0,0x418cd6,'flags')]

def source_scan(root=ROOT):
    bad=[]
    for p in (root/'src').glob('*'):
        if p.suffix not in ('.cpp','.hpp'):continue
        s=p.read_text(encoding='utf-8')
        if re.search(r'0x0*4078d0\b',s,re.I) and re.search(r'0x0*(4874|419c)\b',s,re.I):
            bad.append(str(p.relative_to(root)))
    if bad: raise ValueError('DLL retail effect layout literals: '+', '.join(bad))

def guard_scan(data,root=ROOT):
    rows,clears,*_=data
    written={va:len(hx)//2 for va,_,_,hx,_ in rows}
    written.update({a:b-a for a,b,*_ in clears})
    written[0x4053c4]=5
    for lo,hi,name in ELEMENTS: written[lo]=(0x4189c9 if name=='release' else hi)-lo
    written.update({0x4188b7:1,0x4186f3:5,0x4189d5:5,0x4189db:7})
    failures=[]; count=0
    pattern=re.compile(r'\{\s*(0x[0-9a-fA-F]+)\s*,\s*((?:"[0-9a-fA-F]+"\s*)+)')
    for p in (root/'src').glob('*'):
        if p.suffix not in ('.cpp','.hpp') or p.name=='effect_template_sites.hpp': continue
        s=p.read_text(encoding='utf-8')
        for m in pattern.finditer(s):
            lo=int(m[1],16); hx=''.join(re.findall(r'"([0-9a-fA-F]+)"',m[2]));hi=lo+len(hx)//2
            count+=1
            for va,n in written.items():
                if lo<va+n and va<hi:
                    failures.append(f'{p.name}:{s.count(chr(10),0,m.start())+1} {lo:x}..{hi:x} x {va:x}..{va+n:x}')
    if failures: raise ValueError('guard/site overlaps:\n'+'\n'.join(failures))
    print(f'guard/site scan: {count} literal spans across DLL sources (including ModelPools), {len(written)} W3 write spans, zero overlaps')

def derive(exe,db):
    raw=Path(exe).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=SHA: raise ValueError('unexpected retail exe SHA-256')
    pe=pefile.PE(data=raw); image=pe.get_memory_mapped_image(); base=pe.OPTIONAL_HEADER.ImageBase
    rd=lambda va,n: image[va-base:va-base+n]
    c=sqlite3.connect('file:'+Path(db).as_posix()+'?mode=ro',uri=True)
    dec=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32); dec.detail=True
    ins={}; owners={}
    primitive_functions={0x4186f0,0x418700,0x4188a0,0x4189b0,0x5615b0,0x403d40}
    branch_targets=[]
    try:
        for va,fn,hx,asm in c.execute('select va,fn,bytes,asm from insn order by va'):
            if re.match(r'(?:J\w+|CALL) 0x[0-9a-f]+$',asm): branch_targets.append((va,int(asm.split()[-1],16)))
            if fn not in FUNCTIONS|primitive_functions and not any(x in hx for x in ('74480000','108a0000','9c860000','00450000','fc440000','b4480000','188a0000')): continue
            b=bytes.fromhex(hx)
            decoded=list(dec.disasm(b,va))
            if len(decoded)!=1 or decoded[0].size!=len(b): continue
            i=decoded[0]; ins[va]=i; owners[va]=fn
            if fn in FUNCTIONS and rd(va,len(b))!=b: raise ValueError(f'inventory mismatch {va:x}')
    finally:c.close()
    old=layout(220); by_value={}
    for k,v in old.items():
        if v in by_value: raise ValueError(f'ambiguous layout {k}')
        by_value[v]=k
    rows=[]
    for va,i in ins.items():
        fn=owners[va]
        # Discover owner consumers beyond the seed list by their unique layout operands.
        discovered=any((o.type==X86_OP_MEM and o.mem.disp in (0x4874,0x8a10,0x869c,0x4500,0x44fc,0x48b4)) or
                       (o.type==X86_OP_IMM and o.imm in (0x4874,0x8a18)) for o in i.operands)
        if discovered and fn not in FUNCTIONS: raise ValueError(f'unclassified owner consumer {va:x} fn {fn}')
        if fn not in FUNCTIONS: continue
        for o in i.operands:
            if o.type==X86_OP_MEM:
                v=o.mem.disp
                # Stack storage, CFxManager's independent handle pool and absolute addresses.
                if o.mem.base==X86_REG_ESP or not o.mem.base or v==0x4690: continue
                off,size=i.disp_offset,i.disp_size
            elif o.type==X86_OP_IMM and i.mnemonic not in ('call','jmp') and not i.group(cs.CS_GRP_JUMP):
                v=o.imm; off,size=i.imm_offset,i.imm_size
                if v==7 and i.mnemonic!='mov': continue
            else: continue
            key=by_value.get(v)
            if key:
                if size!=4: raise ValueError(f'unexpected narrow layout operand {va:x} {key}')
                rows.append((va,off,key,bytes(i.bytes).hex(),fn))
            elif 0x3a70<=v<=0x8a18:
                raise ValueError(f'unclassified layout operand {va:x}: {i.mnemonic} {i.op_str}')
    spans=[]
    for lo,hi,key,reg,zero in CLEARS:
        seq=[i for va,i in ins.items() if lo<=va<hi]
        if sum(i.size for i in seq)!=hi-lo or len(seq)!=7: raise ValueError(f'clear boundaries {lo:x}')
        if any(i.mnemonic!='mov' or i.operands[0].type!=X86_OP_MEM or i.operands[0].mem.disp!=j*4 for j,i in enumerate(seq)):
            raise ValueError(f'non-unrolled clear {lo:x}')
        spans.append((lo,hi,key,reg,zero,rd(lo,hi-lo).hex()))
    # Every direct element access through known definition/list bases is classified.
    # Cursor rebasing at spawn_cursor also covers subsequent reads of the same EDI.
    access={0x4188cd,0x4188d5,0x407a98,0x40b181,0x40b1a0,0x40b1e0,0x40b235,0x40b25f,
            0x40b480,0x4189c0,0x418cd0}
    intervals=[(0x4188ca,0x4188d7,'esi'),(0x407a88,0x407a9b,'ecx'),
               (0x40b170,0x40b29b,'edi'),(0x40b473,0x40b483,'edx'),
               (0x4189b7,0x4189c3,'esi'),(0x418cc8,0x418cd3,'eax')]
    found=set()
    for lo,hi,reg in intervals:
        for va,i in ins.items():
            if not lo<=va<hi: continue
            if i.mnemonic=='lea' and all(o.type!=X86_OP_MEM or (o.mem.disp==0 and not o.mem.index) for o in i.operands): continue
            for o in i.operands:
                if o.type==X86_OP_MEM and i.reg_name(o.mem.base)==reg and 0<=o.mem.disp<0x2c:
                    if va not in access: raise ValueError(f'unclassified primitive element {va:x}')
                    found.add(va)
    # The append store uses its calculated EDX alias.
    found.add(0x4188d5)
    if found!=access: raise ValueError(f'primitive access census differs: {found^access}')
    overwritten=[(lo,hi) for lo,hi,*_ in spans]+[(lo,0x4189c9 if name=='release' else hi) for lo,hi,name in ELEMENTS]+[(0x4189db,0x4189e2)]
    for va,t in branch_targets:
        for lo,hi in overwritten:
            if lo<t<hi and not lo<=va<hi: raise ValueError(f'branch into cave span {va:x}->{t:x}')
    for section in pe.sections:
        if section.Name.rstrip(b'\0') not in (b'.text',b'.rdata',b'.data'): continue
        block=section.get_data()
        for off in range(0,len(block)-3,4):
            t=struct.unpack_from('<I',block,off)[0]
            if any(lo<t<hi for lo,hi in overwritten): raise ValueError(f'data pointer into cave span: {section.Name!r}+{off:x}->{t:x}')
    callers={va for va,t in branch_targets if t==0x403d40}
    if callers!={0x4053c4,0x571724}: raise ValueError(f'findNext callers changed: {callers}')
    primitive_callers={va for va,t in branch_targets if t==0x5615b0}
    if not {0x4186f3,0x4189d5}<=primitive_callers: raise ValueError('primitive helper effect callers changed')
    helper=rd(0x403d40,0x99).hex()
    primhelper=rd(0x5615b0,0xd4).hex()
    bounds=[va-0x5615b0+i.imm_offset for va,i in ins.items() if 0x5615b0<=va<0x561684 and any(o.type==X86_OP_IMM and o.imm==10 for o in i.operands)]
    if len(bounds)!=5: raise ValueError('shared ten-slot helper bounds')
    return rows,spans,helper,primhelper,bounds,rd,ins

def emit(data):
    rows,spans,h,ph,bounds,rd,ins=data
    lines=['// Generated by tools/w3_effect_templates_ledger.py; do not edit.', '#pragma once',
           '#include <cstdint>','#include <string_view>','namespace effect_template_sites {',
           'struct operand { std::uint32_t va; unsigned offset; std::string_view field, hex; std::uint32_t function; };',
           'inline constexpr operand operands[]{']
    lines += [f' {{0x{va:x}, {off}, "{key}", "{hx}", 0x{fn:x}}},' for va,off,key,hx,fn in rows]
    lines+=['};','struct clear { std::uint32_t va, end; std::string_view field; unsigned base, zero; std::string_view hex; };','inline constexpr clear clears[]{']
    lines += [f' {{0x{a:x}, 0x{b:x}, "{key}", {reg}, {zero}, "{hx}"}},' for a,b,key,reg,zero,hx in spans]
    lines+=['};',f'inline constexpr std::string_view find_next_hex="{h}";',f'inline constexpr std::string_view primitive_helper_hex="{ph}";',
            'inline constexpr unsigned primitive_bounds[]{'+','.join(str(x) for x in bounds)+'};',
            'struct span { std::uint32_t va, end; std::string_view name, hex; };','inline constexpr span elements[]{']
    for lo,hi,name in ELEMENTS:
        # Release's first instruction and push together have four bytes: also
        # relocate its CALL with its original rel32 target in the cave.
        if name=='release': hi=0x4189c9
        if lo not in ins or hi not in ins: raise ValueError(f'element boundary {lo:x}..{hi:x}')
        lines.append(f' {{0x{lo:x}, 0x{hi:x}, "{name}", "{rd(lo,hi-lo).hex()}"}},')
    lines+=['};','struct guard { std::uint32_t va; std::string_view hex; };', 'inline constexpr guard primitive_guards[]{']
    for lo,n in [(0x4188b4,4),(0x4186f0,8),(0x4189d5,5),(0x4189db,7),(0x418700,0x1b)]:
        lines.append(f' {{0x{lo:x}, "{rd(lo,n).hex()}"}},')
    lines+=['};','}']
    return '\n'.join(lines)+'\n'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',default=os.getenv('XML2_FIX_TEST_EXE','D:/tools/re-projects/bin/XMen2.exe'))
    p.add_argument('--db',default='D:/Projects/agent-work/entity-capacity-impl/round3/native.sqlite')
    p.add_argument('--check',action='store_true'); p.add_argument('--verify',action='store_true')
    a=p.parse_args(); data=derive(a.exe,a.db); result=emit(data); path=ROOT/'src/effect_template_sites.hpp'
    if a.verify: source_scan();guard_scan(data)
    if a.check:
        if not path.exists() or path.read_text()!=result: raise SystemExit('generated header differs')
    else: path.write_text(result,encoding='utf-8',newline='\n')
    print(f'W3 ledger: {len(data[0])} operands, {len(data[1])} bitmap clears, 6 element caves, 11 element accesses, 2 shared helpers; source scan '+('PASS' if a.verify else 'not requested'))

if __name__=='__main__': main()
