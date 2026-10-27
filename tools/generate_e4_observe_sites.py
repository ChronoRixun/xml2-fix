"""Generate guarded instrumentation spans from the owned retail image + audited instruction DB.
No game execution. Usage: --exe FILE --db FILE [--check].
"""
import argparse, hashlib, json, sqlite3
from pathlib import Path
import capstone, pefile
P=argparse.ArgumentParser();P.add_argument('--exe',required=True);P.add_argument('--db',required=True);P.add_argument('--check',action='store_true');a=P.parse_args()
p=Path(a.exe);digest=hashlib.sha256(p.read_bytes()).hexdigest()
assert digest=='146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b',digest
e=pefile.PE(str(p));db=sqlite3.connect('file:'+Path(a.db).resolve().as_posix()+'?mode=ro',uri=True)
D=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);D.detail=True
rows={r[0]:r for r in db.execute('select va,fn,bytes,asm,flow,targets from insn')}
incoming={}
for va,fn,h,asm,flow,targets in rows.values():
 for t in json.loads(targets):
  if len(t)==8: incoming.setdefault(int(t,16),set()).add(va)
S=[]
def add(name,point,kind,mode='before',arg=0): S.append(dict(name=name,point=point,kind=kind,mode=mode,arg=arg))
add('step',0x401d70,'step')
add('next_allocation',0x4615a0,'checkpoint')
add('admitted',0x461674,'admitted')
add('factory_entry',0x423a90,'factory_entry')
add('factory_failed',0x423ae5,'factory_failed')
add('actor_init',0x423ad0,'actor_init','around')
add('controller_pop',0x522a1f,'controller_pop')
add('controller_token',0x42394d,'controller_token','after')
add('factory_published',0x423ade,'factory_published')
add('actor_release',0x461700,'checkpoint')
add('actor_slot_free',0x461790,'actor_free')
add('controller_release',0x522ed0,'controller_release')
add('controller_free',0x522f57,'controller_free')
add('resolver_hit',0x5251af,'resolve_hit','after')
add('resolver_refused',0x5251b5,'resolve_refused')
add('team_resolver_hit',0x525152,'team_resolve_hit','after')
add('team_resolver_refused',0x525158,'team_resolve_refused')
add('reset',0x523660,'reset')
# Reset's final RET also ends its non-quiescent interval.
f=0x523660
for va,r in rows.items():
 if r[1]==f and r[3].startswith('RET'): add('reset_done_'+hex(va),va,'reset_done')
add('update_borrow',0x52264f,'borrow_update','around')
add('event_borrow',0x50e1f6,'borrow_event','around')
handlers='504a51 504f64 504fc5 504fd2 505011 5051a3 505fb5 5062ca 5062ed 506324 506333 506357 50637c 5095a2 509791 509e69 50afcd 50b08d 50b0d3 50c450 50cb12 50ce4c 50ce57 50cef5 50cf3b 50d095 50fd51 511ca9 512f2f 51315b 51316a 513318 5134f4 51360c 51372c 5137f5 514141 5150ce 516c09 516c37 516ca2 5170cc 517235 518038'
for h in handlers.split(): add('handler_'+h,int(h,16),'borrow_handler','around')
add('damage_entry',0x499b10,'damage_enter')
for va in (0x499b73,0x499b7a): add('damage_exit_'+hex(va),va,'damage_exit','after' if va==0x499b73 else 'before')
# Damage exits must sample AL after the native success/failure assignment.
S[-2]['point']=0x499b74;S[-1]['point']=0x499b7b;S[-1]['mode']='after'
for f in (0x525a30,0x525ba0,0x525ce0,0x525700,0x525fa0):
 add('writer_enter_'+hex(f),f,'writer_enter',arg=f)
 for va,r in rows.items():
  if r[1]==f and r[3].startswith('RET'):add('writer_exit_'+hex(va),va,'writer_exit',arg=f)
# Record actual field stores, including a store of an unchanged value.
fields=[0x34,0x38,0x3c,0x2e4,0x2b5,0x18,0x154,0x17c,0x1c,0x2c0,0x2e8,0x2ec,0x340,0x344,0x350]
x87_stores={'fst','fstp','fstpt','fist','fistp','fisttp','fbstp','fstenv','fnstenv','fsave','fnsave','fstcw','fnstcw','fstsw','fnstsw'}
regs={'edi':0,'esi':1,'ebp':2,'ebx':4,'edx':5,'ecx':6,'eax':7}
for va,r in rows.items():
 if r[1] not in (0x525a30,0x525ba0,0x525ce0,0x525700,0x525fa0,0x508400,0x50b4c0,0x5253e0):continue
 decoded=list(D.disasm(bytes.fromhex(r[2]),va))
 if not decoded:continue
 i=decoded[0]
 if not i.operands:continue
 o=i.operands[0]
 if o.type!=capstone.x86.X86_OP_MEM or not ((o.access&capstone.CS_AC_WRITE)or i.mnemonic.lower()in x87_stores):continue
 mem=o.mem;reg=i.reg_name(mem.base)
 if mem.disp not in fields or reg not in regs or mem.index:continue
 add('field_store_'+hex(va),va,'field_store','after',fields.index(mem.disp)|(regs[reg]<<8))
add('timeout',0x527640,'timeout_enter')
for va,r in rows.items():
 if r[1]==0x527640 and r[3].startswith('RET'):add('timeout_done_'+hex(va),va,'timeout_exit')
add('reregister',0x512862,'reregister','around')
add('team_free',0x5251f0,'team_free')
add('team_free_done',0x525262,'team_free_done')
for va in (0x5068cb,0x5073a2): add('formation_read_'+hex(va),va,'formation_read')
# Decode a complete patch span, never crossing incoming branches or function entries.
# Prefer forward extension; when a short call's successor is a branch target, include a prefix.
for s in S:
 point=s['point'];r=rows[point];fn=r[1]
 own=sorted(x for x,v in rows.items() if v[1]==fn);at=own.index(point)
 choices=[]
 for back in range(8):
  if at-back<0:continue
  start=at-back;end=at+1
  while end<len(own) and own[end-1]+len(bytes.fromhex(rows[own[end-1]][2]))-own[start]<5:end+=1
  ins=own[start:end]
  if ins[-1]+len(bytes.fromhex(rows[ins[-1]][2]))-ins[0]<5:continue
  if any(ins[i]+len(bytes.fromhex(rows[ins[i]][2]))!=ins[i+1] for i in range(len(ins)-1)):continue
  hi=ins[-1]+len(bytes.fromhex(rows[ins[-1]][2]))
  if any(any(src<ins[0] or src>=hi for src in incoming.get(v,set())) for v in ins[1:]):continue
  if any(rows[v][3].startswith('RET') for v in ins[:-1]):continue
  choices.append(ins)
 assert choices,s
 ins=min(choices,key=lambda x:(sum(len(bytes.fromhex(rows[v][2])) for v in x),point-x[0]))
 s['va']=ins[0];s['fn']=fn;s['parts']=[]
 for v in ins:
  h=rows[v][2];assert bytes.fromhex(h)==e.get_data(v-0x400000,len(bytes.fromhex(h)))
  i=list(D.disasm(bytes.fromhex(h),v));assert len(i)==1 and i[0].size==len(bytes.fromhex(h))
  i=i[0];target=0;op='raw'
  if i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL):
   if i.operands[0].type==capstone.x86.X86_OP_IMM:
    target=i.operands[0].imm;op='call' if i.mnemonic=='call' else 'jmp' if i.mnemonic=='jmp' else 'jcc'
    assert op!='jcc' or h[:2] not in ('e0','e1','e2','e3'),s
  s['parts'].append(dict(va=v,hex=h,op=op,target=target,cc=((int(h[:2],16)&15) if len(h)==4 else (int(h[2:4],16)&15)) if op=='jcc' else 0))
 s['hex']=''.join(rows[v][2] for v in ins);s['next']=ins[-1]+len(bytes.fromhex(rows[ins[-1]][2]))
# Adjacent short instructions can share one detour. Keep each observation
# point, but compose overlapping displacement spans instead of dropping a store.
S.sort(key=lambda s:s['va'])
groups=[]
for s in S:
 if groups and s['va']<max(x['next']for x in groups[-1]):groups[-1].append(s)
 else:groups.append([s])
for group in groups:
 lo=min(s['va']for s in group);hi=max(s['next']for s in group)
 assert len({s['fn']for s in group})==1,group
 parts={i['va']:i for s in group for i in s['parts']}
 assert not any(any(src<lo or src>=hi for src in incoming.get(v,set()))for v in sorted(parts)[1:]),group
 merged=[parts[v]for v in sorted(parts)];h=''.join(i['hex']for i in merged)
 for s in group:s.update(va=lo,next=hi,parts=merged,hex=h)

functions=[]
for f in sorted(set(s['fn'] for s in S)):
 own=sorted(v for v,r in rows.items() if r[1]==f);hi=own[-1]+len(bytes.fromhex(rows[own[-1]][2]));raw=e.get_data(f-0x400000,hi-f)
 functions.append(dict(lo=f,hi=hi,hex=raw.hex(),sha256=hashlib.sha256(raw).hexdigest()))
kinds=list(dict.fromkeys(s['kind'] for s in S))
out=['#pragma once','// Generated by tools/generate_e4_observe_sites.py; retail SHA-256 '+digest,'#include <array>','#include <cstdint>','namespace e4_observe_sites {','enum class kind {'+','.join(kinds)+'};','struct instruction {unsigned va; const char* hex; unsigned op,target,cc;};']
for n,s in enumerate(S):
 out.append('inline constexpr instruction code%d[]={'%n+','.join('{0x%x,"%s",%d,0x%x,%d}'%(i['va'],i['hex'],['raw','call','jmp','jcc'].index(i['op']),i['target'],i['cc']) for i in s['parts'])+'};')
out+=['struct site {const char* name; unsigned va,point,next; kind type; unsigned mode,arg; const char* hex; const instruction* code; unsigned count;};',f'inline constexpr std::array<site,{len(S)}> sites{{{{']
for n,s in enumerate(S):out.append('{"%s",0x%x,0x%x,0x%x,kind::%s,%d,0x%x,"%s",code%d,%d},'%(s['name'],s['va'],s['point'],s['next'],s['kind'],['before','after','around'].index(s['mode']),s['arg'],s['hex'],n,len(s['parts'])))
out+=['}};','struct region {unsigned lo,hi; const char* hash;};',f'inline constexpr std::array<region,{len(functions)}> regions{{{{']
for f in functions:out.append('{0x%x,0x%x,"%s"},'%(f['lo'],f['hi'],f['sha256']))
out+=['}};','}'];result='\n'.join(out)+'\n';dest=Path(__file__).resolve().parents[1]/'src/e4_observe_sites.hpp'
if a.check:assert dest.read_text()==result,'generated header differs'
else:dest.write_text(result,newline='\n')
print(f'E4 sites: {len(S)} observation points in {len(groups)} spans, {len(functions)} function regions, 44 embedded-handler brackets; check={a.check}')
