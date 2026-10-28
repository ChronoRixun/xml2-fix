"""Generate E4 Stage 2's role-scoped retail operand ledger, never native bodies.

Inputs are the owned executable and audited instruction DB, both read-only.
The actor/capacity rows are independently listed by the stage-0 audit; CAI
layout operands are decoded only in the CAI pool functions, excluding replacements.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import capstone
import pefile

ROOT = Path(__file__).resolve().parents[1]
REPLACED = {0x522a70, 0x522ad0, 0x5251f0, 0x5234c0, 0x523320}
PRIMARY = dict(zip((0xc380,0xc384,0xc408,0xc40c,0xc410,0xc414,0xc418,0xc41c,0xc49c,0xc4a0),
                   (0x24a80,0x24a8c,0x24c10,0x24c14,0x24c18,0x24c1c,0x24c28,0x24c2c,0x24dac,0x24db0)))
TEAM = dict(zip((0x9a0,0x9a4,0x9b8,0x9bc,0x9c0,0x9c4,0x9c8,0x9cc,0x9dc,0x9e0),
                (0xe700,0xe70c,0xe890,0xe894,0xe898,0xe89c,0xe8a8,0xe8ac,0xea2c,0xea30)))
OBJECT = {0xc4a8:0x24db8,0xce84:0x337e4,0xce70:0x33660}
ACTOR = {
 0x10904:(0x31b04,'46146d 46163a 4617e9'),
 0x10988:(0x31c88,'461424 461440 461451 46145f 4614ff 4617b8 4617c9 4617d7'),
 0x1098c:(0x31c8c,'46142a 461505 461634'),
 0x10990:(0x31c90,'461430 461446 461457 46150b 4617be 4617cf'),
 0x10994:(0x31c94,'461511 461655 46165c 46179c 461898'),
 0x10998:(0x31ca0,'461517 46166e 4617f0 4617f8 461892'),
 0x1099c:(0x31ca4,'461578 4615b0 46172f'),
 0x109a0:(0x31ca8,'461522 4615e7 4615f8 46174a'),
}
CAPACITIES = {
 'actor':('46144e 461475 46157e 4615b0 4617c6',32),
 'primary':('5227cc 52280d 522835 522856 522897 522a44 522f26 522f9e 522fc5 523938 5250bf 525174',32),
 'team':('522756 522797 52279c 5228cc 52290d 522934 522956 522998 522aa4 522afe 522b25 522b46 522bc3 522c3e 522c6a 522c8c 522ccd 522cd2 522cd7 522d43 522db3 522e23 523354 523380 5233a6 5233e7 5233ec 5233f1 523648 525117 525231',4),
}
SEEDS = {'actor':('461465 4617e1',31),'primary':('522f41 522fb5 5238ed',31),'team':('522b15 5235fd 52524c',3)}
CLEARS = {0x461511,0x461898,0x5228a1,0x5238b6,0x5238ce,0x5229a5,0x5235c6,0x5235de}

def generate(exe, database):
    raw=Path(exe).read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b'
    pe=pefile.PE(data=raw); image=pe.get_memory_mapped_image()
    db=sqlite3.connect('file:'+Path(database).resolve().as_posix()+'?mode=ro',uri=True)
    rows={r[0]:r for r in db.execute('select va,fn,bytes,asm,flow,targets from insn')}
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    edits={}; owners=set()
    def ins(va):
        r=rows[va];b=bytes.fromhex(r[2]);assert image[va-0x400000:va-0x400000+len(b)]==b,hex(va)
        i=list(decoder.disasm(b,va));assert len(i)==1 and i[0].size==len(b),hex(va)
        return i[0]
    def add(va,offset,size,old,new,role,dynamic=False):
        r=rows[va]
        if r[1] in REPLACED:return
        b=bytes.fromhex(r[2]);assert int.from_bytes(b[offset:offset+size],'little')==old,(hex(va),role)
        key=(va,offset);assert key not in edits or edits[key]['value']==new,key
        edits[key]=dict(va=va,offset=offset,size=size,value=new,role=role,hex=r[2],dynamic=dynamic,owner=r[1]);owners.add(r[1])
    def displacement(va,old,new,role):
        i=ins(va);assert i.disp==old,(hex(va),i.op_str,hex(old));add(va,i.disp_offset,i.disp_size,old,new,role)
    for old,(new,sites) in ACTOR.items():
        for s in sites.split():displacement(int(s,16),old,new,'actor-field')
    # Phase A owns these entire instructions, so its contribution must be
    # regenerated with this operand rather than patched in a second transaction.
    for va in (0x461588,0x4615db,0x46176d):displacement(va,0x7e6d0,0x1aa51c,'generic-object-live')
    for va,r in rows.items():
        # 5226b0 is the team-generation advance helper called by 5251c0.
        # It is part of this pool even though it precedes the first-live scan.
        if not (0x5226b0<=va<0x5252b2) or r[1] in REPLACED:continue
        i=ins(va)
        if not i.disp_size:continue
        mapping=PRIMARY if i.disp in PRIMARY else TEAM if i.disp in TEAM else OBJECT
        if i.disp in mapping:displacement(va,i.disp,mapping[i.disp],'primary-field' if mapping is PRIMARY else 'team-field')
    for role,(sites,old) in CAPACITIES.items():
        for s in sites.split():
            va=int(s,16);i=ins(va);add(va,i.imm_offset,i.imm_size,old,96,role+'-capacity')
    assert all(any(e['va']==va for e in edits.values()) for va in (0x5226b6,0x5226c4,0x5226cd,0x5226d4,0x5226e3))
    for role,(sites,old) in SEEDS.items():
        for s in sites.split():
            va=int(s,16);i=ins(va);add(va,i.imm_offset,i.imm_size,old,95,role+'-seed')
    for va,old,new in ((0x5252f7,0x78ba14,4),(0x525311,0x797eb8,0x24db8),(0x525328,0x78ba10,0),(0x67df90,0x78ba10,0)):
        i=ins(va);add(va,i.imm_offset,i.imm_size,old,new,'cai-address',True)
    i=ins(0x525302);add(i.address,i.disp_offset,i.disp_size,0x78ba10,0,'cai-address',True)
    # Clears and whole-function replacements carry independent region hashes.
    specials=[]
    def region(va,kind):
        r=rows[va]; fn=r[1];owned=[x for x in rows.values() if x[1]==fn]
        hi=max(x[0]+len(x[2])//2 for x in owned) if kind=='replace' else va+len(r[2])//2
        b=image[va-0x400000:hi-0x400000]
        assert len(b)>=5
        head=b'';at=va
        while len(head)<5:head+=bytes.fromhex(rows[at][2]);at=va+len(head)
        specials.append(dict(va=va,hi=hi,kind=kind,head=head.hex(),hash=hashlib.sha256(b).hexdigest(),owner=fn))
        owners.add(fn)
    for va in sorted(CLEARS):region(va,'clear')
    for va in sorted(REPLACED):region(va,'replace')
    # The admission cave replaces CMP and its JZ. All other observer hooks
    # preserve complete instructions through the observation ledger.
    b=image[0x4615b0-0x400000:0x4615b9-0x400000]
    specials.append(dict(va=0x4615b0,hi=0x4615b9,kind='admit',head=b.hex(),hash=hashlib.sha256(b).hexdigest(),owner=0x4615a0))
    b=bytes.fromhex(rows[0x523060][2])
    specials.append(dict(va=0x523060,hi=0x523066,kind='backstop',head=b.hex(),hash=hashlib.sha256(b).hexdigest(),owner=0x523060))
    b=bytes.fromhex(rows[0x461669][2])
    specials.append(dict(va=0x461669,hi=0x46166e,kind='actor-pop',head=b.hex(),hash=hashlib.sha256(b).hexdigest(),owner=0x461630))
    # Read-only factory queries (definition / class lookups and the actor-size
    # getter). The save-time demand probe that called them was removed in
    # 739cc0f (conservative 0x4 marking); they stay pinned as owner regions so
    # the 46-owner hash ledger and its retail checks are unchanged.
    owners.update((0x4612e0,0x4611a0,0x4625c0,0x41d310))
    regions=[]
    for fn in sorted(owners):
        rs=[r for r in rows.values() if r[1]==fn];hi=max(r[0]+len(r[2])//2 for r in rs)
        regions.append(dict(lo=fn,hi=hi,hash=hashlib.sha256(image[fn-0x400000:hi-0x400000]).hexdigest()))
    for r in rows.values():
        if r[1] in REPLACED:continue
        for target in json.loads(r[5]):
            if len(target)!=8:continue
            t=int(target,16)
            for s in specials:
                assert not(s['va']<t<s['hi'] and r[1]!=s['owner']),('incoming branch',hex(r[0]),hex(t))
    result=dict(edits=sorted(edits.values(),key=lambda r:(r['va'],r['offset'])),specials=specials,regions=regions)
    return result

def header(data):
    out=['#pragma once','// Generated by scripts/e4_core_sites.py from role-scoped owned retail inputs.',
         '#include <array>','#include <cstdint>','namespace e4_core_sites {',
         'struct edit {unsigned va,offset,size,value;bool dynamic;const char* hex;const char* role;};',
         f'inline constexpr std::array<edit,{len(data["edits"])}> edits{{{{']
    for r in data['edits']:out.append('{0x%x,%d,%d,0x%x,%s,"%s","%s"},'%(r['va'],r['offset'],r['size'],r['value'],str(r['dynamic']).lower(),r['hex'],r['role']))
    out+=['}};','struct special {unsigned va,hi;const char* kind;const char* head;const char* hash;};',f'inline constexpr std::array<special,{len(data["specials"])}> specials{{{{']
    for r in data['specials']:out.append('{0x%x,0x%x,"%s","%s","%s"},'%(r['va'],r['hi'],r['kind'],r['head'],r['hash']))
    out+=['}};','struct region {unsigned lo,hi;const char* hash;};',f'inline constexpr std::array<region,{len(data["regions"])}> regions{{{{']
    for r in data['regions']:out.append('{0x%x,0x%x,"%s"},'%(r['lo'],r['hi'],r['hash']))
    return '\n'.join(out+['}};','}',''])

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True);p.add_argument('--db',required=True);p.add_argument('--check',action='store_true');p.add_argument('--json')
    a=p.parse_args();d=generate(a.exe,a.db);text=header(d);target=ROOT/'src/e4_core_sites.hpp'
    if a.check:assert target.read_text(encoding='utf-8')==text,'generated header differs'
    else:target.write_text(text,encoding='utf-8',newline='\n')
    if a.json:Path(a.json).write_text(json.dumps(d,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('E4 core ledger:',len(d['edits']),'operand rows,',len(d['specials']),'special spans,',len(d['regions']),'owner regions')
