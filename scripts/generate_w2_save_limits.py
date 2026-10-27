"""W2 E14/E7 instruction plan. Inputs are owned retail PE and SPEC-old sites.json.
Emits instruction guards, authored edits and relocation metadata, never function bodies.
"""
from pathlib import Path
import sys, json, struct, hashlib
import pefile, capstone
from capstone import x86

ROOT = Path(__file__).resolve().parent.parent
pe = pefile.PE(sys.argv[1]); image = pe.get_memory_mapped_image()
assert hashlib.sha1(Path(sys.argv[1]).read_bytes()).hexdigest() == '7d95cdb4a9a599b982147a3389ea7aff211cb82b'
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
ledger = json.loads(Path(sys.argv[2]).read_text())
def raw(a,n): return image[a-0x400000:a-0x400000+n]
def ins(a): return next(md.disasm(raw(a,16),a,count=1))
def decode(a,b): return list(md.disasm(raw(a,b-a),a))
def p32(v): return struct.pack('<I',v&0xffffffff)
patches={}; classes={}; extra={}; spans=[]; replacements=[]
def edit(a,b,kind,n=None):
    if isinstance(b,str): b=bytes.fromhex(b)
    n=n or ins(a).size
    patches[a]=(n,b.ljust(n,b'\x90')); classes[a]=kind
def operand(i,v,disp=False):
    b=bytearray(i.bytes); o,s=(i.disp_offset,i.disp_size) if disp else (i.imm_offset,i.imm_size)
    assert s and -(1<<(s*8-1))<=v<(1<<(s*8)), (hex(i.address),v,s)
    b[o:o+s]=(v&((1<<(s*8))-1)).to_bytes(s,'little'); return b
def replace(lo,hi,target,kind): replacements.append((lo,hi,target,kind))
def call(a,target,kind,n=None):
    edit(a,'e800000000',kind,n);extra[a]=target
def preserve_call(args,target,dest=None):
    # pushfd/pushad; authored pushes use saved input registers via ESP.
    b=bytearray.fromhex('9c60')
    for argument,(reg,delta) in enumerate(args):
        # Saved register offsets from PUSHAD's ESP; adjust for preceding arguments.
        off={'eax':28,'ecx':24,'edx':20,'ebx':16,'ebp':8,'esi':4,'edi':0}[reg]
        # Capture current input using MOV EAX,[ESP+off], optional pointer delta.
        off += 4 * argument
        b+=bytes.fromhex('8b4424')+bytes([off])
        if delta:b+=b'\x05'+p32(delta)
        b+=b'\x50'
    b+=b'\xb8'+p32(0xfeed0000+target)+bytes.fromhex('ffd0')
    if args:b+=bytes.fromhex('83c4')+bytes([4*len(args)])
    if dest:b+=bytes.fromhex('894424')+bytes([{'eax':28,'ecx':24,'edx':20}[dest]])
    return b+bytes.fromhex('619d')

# Objective manager keeps record stride and +0x1aa live data unchanged. The
# complete manager is relocated, with the enlarged pool still at manager+8.
N=127; stride=0x1ac; bitmap=8+N*stride; count=bitmap+16; tail=count+4
manager_tail=8+tail; manager_size=manager_tail+0x2d8
for r in ledger['objectives']: assert raw(int(r['addr'],16),len(bytes.fromhex(r['bytes'])))==bytes.fromhex(r['bytes'])
for r in ledger['objectives']:
    a=int(r['addr'],16);i=ins(a)
    if 0x489680<=a<0x4898d1:continue
    if i.disp in (0x7d64,0x7d70):edit(a,operand(i,bitmap if i.disp==0x7d64 else count,True),'objective pool fields')
    if a==0x4899e0:
        b=operand(i,8+count,True);b[i.imm_offset]=127;edit(a,b,'per-act capacity')
    if i.imm_size and not i.group(capstone.CS_GRP_CALL) and not i.group(capstone.CS_GRP_JUMP):
        v=i.operands[-1].imm if i.operands[-1].type==x86.X86_OP_IMM else None
        if v==75 and a not in (0x4892ec,0x489a8c,0x4899e0):edit(a,operand(i,127),'per-act capacity')
        if v==300:edit(a,operand(i,1024),'objective state capacity')
        if v==299:edit(a,operand(i,1024),'objective registration cap')
        if v==25:edit(a,operand(i,64),'objective files')
# Address markers point at separately owned state, name/act/base-index tables,
# their count fields, and the side-mission stack. Sweep all executable owners.
data={0x72b118:1,0x72b248:2,0x72b56c:3,0x72b570:4,0x72b58c:5,0x72b590:6,0x72b5ac:7,0x72b5b0:8,0x72ba8c:9}
text=next(s for s in pe.sections if s.Name.startswith(b'.text'))
md.skipdata=True
allcode=[];lo=0x400000+text.VirtualAddress;hi=lo+text.Misc_VirtualSize
cache=ROOT/'build/w2-save-limits-decode.json'
if cache.exists():
    allcode=[(a,bytes.fromhex(b)) for a,b in json.loads(cache.read_text())]
    assert all(raw(a,len(b))==b for a,b in allcode)
    lo=hi
while lo<hi:
    stop=min(lo+0x8000,hi);end=lo
    for i in md.disasm(raw(lo,min(stop+16,hi)-lo),lo):
        if i.address>=stop:break
        allcode.append((i.address,bytes(i.bytes)));end=i.address+i.size
    assert end>lo;lo=end
md.skipdata=False
cache.parent.mkdir(exist_ok=True)
cache.write_text(json.dumps([(a,b.hex()) for a,b in allcode]))
for a,b in allcode:
    i=next(md.disasm(b,a,count=1),None)
    if i is None or not i.id:continue
    new=None
    if i.disp in data:
        new=bytearray(patches.get(a,(i.size,bytes(i.bytes)))[1]);new[i.disp_offset:i.disp_offset+i.disp_size]=p32(0xfeed1000+data[i.disp])
    if i.imm_size and not i.group(capstone.CS_GRP_CALL) and not i.group(capstone.CS_GRP_JUMP):
        v=int.from_bytes(i.bytes[i.imm_offset:i.imm_offset+i.imm_size],'little')
        if v in data:
            new=bytearray(patches.get(a,(i.size,bytes(i.bytes)))[1]);new[i.imm_offset:i.imm_offset+i.imm_size]=p32(0xfeed1000+data[v])
    if new:edit(a,new,'objective data relocation')
# Every old pool-field displacement in the whole retail text belongs to this
# objective owner, including four journal/menu readers outside its method range.
pool_fields={0x7d64:bitmap,0x7d6c:8+bitmap,0x7d70:count,0x7d78:8+count,0x7d7c:manager_tail,0x8050:manager_size-4}
for a,b in allcode:
    i=next(md.disasm(b,a,count=1),None)
    if i is not None and i.id and i.disp in pool_fields:
        new=bytearray(patches.get(a,(i.size,bytes(i.bytes)))[1]);new[i.disp_offset:i.disp_offset+i.disp_size]=p32(pool_fields[i.disp])
        edit(a,new,'objective pool field / external UI consumer')
# File names are 64x32 plus four bytes pad and count. Its shared constructor
# has just this objective owner; no other class's same-numbered field changes.
edit(0x488d19,operand(ins(0x488d19),64*32+4,True),'file-name count offset')
# Widen the two per-file base-index sites and scale their table index by two.
edit(0x489252,b'\x66\x89\x1c\x55'+p32(0xfeed1006),'u16 file base store')
edit(0x489bdf,b'\x0f\xb7\x14\x5d'+p32(0xfeed1006),'u16 file base read')
call(0x67a875,16,'64-entry file acts constructor')
call(0x67a895,17,'64-entry file bases constructor')
# Wide-index readers. The final reader uses a cursor at record+0x1a9.
for r in ledger['objectives']:
    a=int(r['addr'],16);i=ins(a)
    if i.mnemonic=='movzx' and i.disp==0x1a9:
        reg=i.reg_name(i.operands[1].mem.base); dest=i.reg_name(i.operands[0].reg)
        edit(a,preserve_call([(reg,0)],1,dest),'wide objective index')
edit(0x489f16,preserve_call([('esi',-0x1a9)],1,'eax'),'wide objective cursor')
# The only index store shared with objective_text. The W2 description
# contribution omits this write and resets by the same full index callback.
edit(0x489a01,bytes.fromhex('8890a9010000')+preserve_call([('edx',0),('eax',0)],2),'wide objective registration')
# The global registrar returns the cumulative number of registered objective
# definitions. Observe its return without changing flags/registers/ret8, so
# step8 status reports global use rather than the current act's loaded pool.
edit(0x48864b,raw(0x48864b,4)+preserve_call([('eax',0)],18),'registered objective count observer')
# Allocation/reuse/reset manage the side table, even with descriptions disabled.
replace(0x489620,0x489678,3,'objective allocate')
replace(0x488c20,0x488cf8,4,'objective release bitmap')
replace(0x489680,0x4898d1,5,'retail-order objective heapsort')
replace(0x488430,0x488514,13,'objective field-copy helper')
# Constructor initializes four bitmap words (three in retail); side table is
# reset by manager getter and pool reset. Leave the rest of the constructor native.
edit(0x48a005,'89018941045789410889410c','objective constructor bitmap',9)
for a,b in allcode:
    if not 0x488d90<=a<0x48a100:continue
    i=ins(a)
    if i.disp==0x7d7c:edit(a,operand(i,manager_tail,True),'objective manager tail')
    if i.imm_size and i.mnemonic=='add' and i.operands[-1].type==x86.X86_OP_IMM and i.operands[-1].imm==0x7d7c:edit(a,operand(i,manager_tail),'objective manager tail')
    if i.disp==0x8050:edit(a,operand(i,manager_size-4,True),'objective manager flags')
edit(0x489fff,operand(ins(0x489fff),8+bitmap,True),'objective constructor bitmap base')
edit(0x48a016,operand(ins(0x48a016),8+count,True),'objective constructor count')
edit(0x48a0ed,b'\x68'+p32(manager_size),'objective manager allocation')
replace(0x48a0e0,0x48a121,6,'DLL objective manager getter')
replace(0x48a0b0,0x48a0d9,7,'DLL objective manager shutdown')
# Snapshot width: three pad bytes follow the objective bytes, then six floats.
snapshot_owners=[(0x4874b0,0x487571),(0x489300,0x4894d0),(0x48a170,0x48a3d0),(0x5f4400,0x5f4497)]
for a,b in allcode:
    if any(l<=a<h for l,h in snapshot_owners):
        i=ins(a)
        if i.disp in range(0x254,0x26c,4):edit(a,operand(i,i.disp+0x2d4,True),'snapshot vector offset')
        if i.mnemonic=='add' and i.imm_size and i.operands[-1].type==x86.X86_OP_IMM and i.operands[-1].imm==0x254:edit(a,operand(i,0x528),'snapshot vector offset')
for a in (0x4892ec,0x489a8c):edit(a,b'\xb9'+p32(256),'snapshot objective copy')
for a in (0x4892f8,0x489a98):edit(a,'90','remove retail 301st byte copy')
edit(0x5f3fb2,operand(ins(0x5f3fb2),1024),'side snapshot clear')
edit(0x5f3fc4,b'\xb9'+p32(256),'side snapshot copy')
edit(0x5f3fcd,'90','side snapshot no 301st byte')
# Side-stack constructor/copies/pop and push frame. The push's stack grows by
# the snapshot delta; every old argument/cookie location follows it.
for a,b in allcode:
    if not (0x489ad0<=a<0x489d40 or 0x48a130<=a<0x48a170 or 0x5f2ce0<=a<0x5f2d20 or 0x5f3630<=a<0x5f3728 or 0x5f44a0<=a<0x5f4900):continue
    i=ins(a)
    if i.disp==0x4dc:edit(a,operand(i,0xa84,True),'side stack count')
    if i.imm_size and i.mnemonic in ('push','imul') and i.operands[-1].type==x86.X86_OP_IMM and i.operands[-1].imm==0x26c:edit(a,operand(i,0x540),'side stack frame')
    if i.mnemonic=='lea' and i.disp==-0x26c:edit(a,operand(i,-0x540,True),'side stack previous frame')
for a in (0x46bcea,0x46e402,0x46e423):edit(a,operand(ins(a),0xa88),'side stream size')
for a in (0x46bcfa,0x46e412):edit(a,operand(ins(a),0xa88//4),'side stream copy')
# CMap's [MAP: MISSION STATE] stream write (0x484b49) and read (0x484c20) move the whole W2 snapshot; at 0x26c the
# loader read the following profile/script variables 0x2d4 bytes early and the saver's stream failed lcsave's W2 reader.
for a in (0x484b49,0x484c20):edit(a,operand(ins(a),0x540),'mission state stream size')
for i in decode(0x5f3630,0x5f3728):
    if i.disp_size and i.operands and any(o.type==x86.X86_OP_MEM and o.mem.base==x86.X86_REG_ESP for o in i.operands) and i.disp>=0x278:edit(i.address,operand(i,i.disp+0x2d4,True),'side push frame argument/cookie')
    if i.mnemonic in ('sub','add') and i.op_str.startswith('esp,') and i.operands[-1].imm==0x278:edit(i.address,operand(i,0x54c),'side push stack frame')
edit(0x5f36e7,b'\xb9'+p32(0x540//4),'side push copy')
for l,h,frame,threshold in ((0x5f44a0,0x5f457a,0x274,0x270),(0x5f4580,0x5f46e4,0x2f4,0x2f0),(0x5f46f0,0x5f4763,0x270,0x270)):
    for i in decode(l,h):
        if i.disp_size and any(o.type==x86.X86_OP_MEM and o.mem.base==x86.X86_REG_ESP for o in i.operands) and i.disp>=threshold:edit(i.address,operand(i,i.disp+0x2d4,True),'side pop frame argument/cookie')
        if i.mnemonic in ('sub','add') and i.op_str.startswith('esp,') and i.operands[-1].imm==frame:edit(i.address,operand(i,frame+0x2d4),'side pop stack frame')

# Talent pool layouts. The leading record bitmap in A/C is an unused one-word
# template member in retail; reserve eight words in each W2 pool as well.
def pool(n,stride,generations=False):
    bitmap=n*stride;ring=bitmap+(n+31)//32*4;head=ring+n*4+4;buckets=head+12;live=buckets+(n+31)//32*4
    d=dict(bitmap=bitmap,ring=ring,head=head,tail=head+4,ringcount=head+8,buckets=buckets,live=live,size=live+4)
    if generations:d.update(generations=live+4,mask=live+4+n*4,shift=live+8+n*4,size=live+16+n*4)
    return d
A=pool(256,44);B=pool(256,36,True);C=pool(256,16)
a_outer=0x364;a_pool=a_outer+12;a_index=a_pool+A['size'];b_pool=a_index+256*2+4;c_outer=b_pool+B['size'];c_pool=c_outer+12;c_index=c_pool+C['size'];registry_size=c_index+256*4+4
oldA={0x1130:'bitmap',0x1134:'ring',0x12c8:'head',0x12cc:'tail',0x12d0:'ringcount',0x12d4:'buckets',0x12e4:'live'}
oldB={0xe10:'bitmap',0xe20:'ring',0xfb4:'head',0xfb8:'tail',0xfbc:'ringcount',0xfc0:'buckets',0xfd0:'live',0xfd4:'generations',0x1164:'mask',0x1168:'shift',0xfd8:'generation1'}
B['generation1']=B['generations']+4
oldC={0x640:'bitmap',0x644:'ring',0x7d8:'head',0x7dc:'tail',0x7e0:'ringcount',0x7e4:'buckets',0x7f4:'live'}
outer={0x1654:a_pool+A['live'],0x1658:a_index,0x1724:b_pool,0x26e4:b_pool+B['buckets'],0x26f4:b_pool+B['live'],0x26f8:b_pool+B['generations'],0x2888:b_pool+B['mask'],0x288c:b_pool+B['shift'],0x2890:c_outer,0x2894:c_outer+4,0x2898:c_outer+8,0x289c:c_pool,0x3090:c_pool+C['live'],0x3094:c_index}
# 0x4be010 / 0x4be9c0 are ratl pool<16-byte, 100>::pop / clear, identical-code
# folded for every such pool: 0x4d6b00 and 0x4fd8d0 (pop), 0x460580, 0x4d6ff0
# and 0x4d8170 (clear) are foreign owners. Both bodies stay retail; only the
# talent registry's own C-pool edges call private 256-entry copies (below).
SHARED_POOL=((0x4be010,0x4be06f),(0x4be9c0,0x4bea1d))
def shared_pool(a):return any(l<=a<h for l,h in SHARED_POOL)
for i in decode(0x4bdfb0,0x4c06a0):
    d=i.disp;new=None;a=i.address
    if shared_pool(a):continue
    # The pool-specific offsets are unique in this owner range, apart from
    # the two appended index-table offsets, classified by the outer object.
    if d in outer:new=outer[d]
    if d in oldA:new=A[oldA[d]]
    if d in oldB:new=B[oldB[d]]
    if d in oldC:new=C[oldC[d]]
    if d==0x12f4:new=12+A['size'] # relative to A outer (registry +0x364)
    if d==0x804:new=12+C['size']  # relative to C outer
    if new is not None:edit(a,operand(i,new,True),'talent registry layout')
# Fixed shared-ring helpers still have 100-entry offsets and are used by
# foreign classes. Replace only the three talent-owned call edges.
for a,target in ((0x4befbf,8),(0x4bfba2,9),(0x4bfc32,9)):
    call(a,target,'private 256-entry ring helper')
for a,target in ((0x4bfce9,19),(0x4bfe10,20),(0x4c043d,20)):
    assert ins(a).mnemonic=='call' and ins(a).operands[0].imm==(0x4be010 if target==19 else 0x4be9c0)
    call(a,target,'private 256-entry talent pool helper')
# 27 classified capacity/last-index operands (26 in the design's E7-02
# list plus the full-loop-owned one); M/cap/stack are separate classifications.
caps=[0x4bdfe4,0x4beabe,0x4bead5,0x4beae5,0x4befa6,0x4be58f,0x4be5ec,0x4be62d,0x4be650,0x4be676,0x4be6b8,0x4be7e4,0x4beb03,0x4beede,0x4beef5,0x4bef05,0x4bef60,0x4bef7b,0x4bf319,0x4bf358,0x4bfaf5,0x4bfed1,0x4be044,0x4be9ee,0x4bea05,0x4bea15,0x4c0115]
for a in caps:
    i=ins(a);v=i.operands[-1].imm;assert v in (99,100)
    if shared_pool(a):continue # guarded retail in the ledger, never edited
    nv=255 if v==99 else 256
    if i.imm_size==4:new=operand(i,nv)
    else:
        assert i.bytes[0]==0x83
        new=bytes([0x81])+i.bytes[1:i.imm_offset]+p32(nv)
    edit(a,new,'talent capacity / last-index')
for a in (0x4c0584,0x4c083c):edit(a,b'\x68'+p32(registry_size),'whole talent registry allocation')
for a in (0x4c0589,0x4c0841):call(a,14,'DLL talent registry allocation')
call(0x4c0907,15,'DLL talent registry lifetime')
# All bitmap clears (including resets, not just the constructor).
for a,hi,reg in ((0x4befcc,0x4befd7,'ecx'),(0x4bf2cb,0x4bf2d6,'ecx'),(0x4bf2f3,0x4bf2fe,'eax'),(0x4bf9f6,0x4bfa01,'ecx'),(0x4bfe05,0x4bfe10,'edx'),(0x4c042c,0x4c0437,'edx')):
    # Preserve flags and registers; EAX need not contain zero in every reset.
    mov={'ecx':'8bf9','eax':'8bf8','edx':'8bfa'}[reg]
    edit(a,bytes.fromhex('9c60'+mov+'33c0b908000000f3ab619d'),'eight-dword talent bitmap clear',hi-a)
# Reset's five-wide generation loop is replaced as a complete owner range,
# including its backward branch. The replacement preserves registers.
replace(0x4bfa20,0x4bfafe,10,'256 generation loop')
for a in list(patches):
    if 0x4bfa20<=a<0x4bfafe:del patches[a];classes.pop(a,None)
edit(0x4bdc12,'e90000000090','talent M helper tail',6);extra[0x4bdc12]=11
call(0x4c05de,12,'shared cap tail thunk')
edit(0x4c0644,b'\x8d\x88'+p32(200),'talent exclusive endpoint')
# The shared helper itself is guarded whole even though only its tail changes.
replacements.append((0x4bdc00,0x4bdc18,0,'talent helper guard'))
replacements.append((0x4bdfb0,0x4c06a0,0,'talent registry method family'))

# Do not emit edits inside a complete replacement, but retain every guard.
for a in list(patches):
    if any(lo<=a<hi and target for lo,hi,target,_ in replacements):del patches[a];classes.pop(a,None)

# Same-size edits stay in place; growing edits get whole-instruction caves.
for a,(n,b) in patches.items():
    if len(b)>n:
        hi=a+n
        while hi-a<5:hi+=ins(hi).size
        spans.append((a,hi))
functions=[(int(s.split()[0],16),int(s.split()[1])) for s in (Path(sys.argv[2]).parent/'funcs.txt').read_text().splitlines()]
branches=[]
for a,b in allcode:
    for i in md.disasm(b,a):
        if i.id and (i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)) and i.operands[0].type==x86.X86_OP_IMM:branches.append((a,i.operands[0].imm))
for iteration in range(20):
    merged=[]
    for a,b in sorted(spans):
        if merged and a<merged[-1][1]:merged[-1]=(merged[-1][0],max(b,merged[-1][1]))
        else:merged.append((a,b))
    spans=merged
    incoming=[(s,t,a,b) for s,t in branches for a,b in spans if a<t<b and not a<=s<b]
    if not incoming:break
    for s,t,a,b in incoming:
        owners=[(l,l+n) for l,n in functions if l<=a<b<=l+n]
        if not owners:
            # Tiny template functions absent from Ghidra's list: walk back to
            # the prior INT3 alignment and forward to the next one.
            l=a
            while raw(l-1,1)!=b'\xcc':l-=1
            h=next(i.address for i in decode(l,l+0x2000) if i.mnemonic=='int3')
            owners=[(l,h)]
        l,h=min(owners,key=lambda v:v[1]-v[0]);spans.append((l,h))
else:raise RuntimeError('cave promotion failed')
# Whole replacement entry cannot have live incoming interior edges.
for l,h,target,k in replacements:
    if target:assert not [(s,t) for s,t in branches if l<t<h and not l<=s<h],(k,'external interior entry')
lines=['// Generated by generate_w2_save_limits.py: retail instruction guards and authored edits.','#pragma once','#include <cstdint>','#include <vector>','namespace save_limits_sites {','struct row {unsigned va;const char* old;const char* value;const char* kind;unsigned target;};','inline const std::vector<row> direct{']
for a,(n,b) in sorted(patches.items()):
    if not any(l<=a<h for l,h in spans):lines.append(f' {{{a:#x},"{raw(a,n).hex()}","{b.hex()}","{classes[a]}",{extra.get(a,0)}}},')
lines+=['};','struct change {unsigned offset,size;const char* value;unsigned target;};','struct block {unsigned lo,hi;const char* hash;std::vector<change> edits;};','inline const std::vector<block> blocks{']
for l,h in spans:
    lines.append(f' {{{l:#x},{h:#x},"{hashlib.sha256(raw(l,h-l)).hexdigest()}",{{');covered=set()
    for i in decode(l,h):
        a=i.address
        if a in covered:continue
        if a in patches:n,b=patches[a];t=extra.get(a,0);covered.update(range(a,a+n))
        elif (i.group(capstone.CS_GRP_CALL) or i.group(capstone.CS_GRP_JUMP)) and i.operands[0].type==x86.X86_OP_IMM:
            n=i.size;t=i.operands[0].imm
            if i.bytes[0] in (0xe8,0xe9,0xeb):b=bytes.fromhex('e800000000' if i.bytes[0]==0xe8 else 'e900000000')
            elif 0x70<=i.bytes[0]<=0x7f:b=bytes([0x0f,i.bytes[0]+0x10])+p32(0)
            else:b=i.bytes[:2]+p32(0)
        else:continue
        lines.append(f'  {{{a-l:#x},{n},"{b.hex()}",{t:#x}}},')
    lines.append(' }},')
lines+=['};','struct replacement {unsigned lo,hi,target;const char* hash;const char* kind;};','inline const std::vector<replacement> replacements{']
for l,h,t,k in replacements:lines.append(f' {{{l:#x},{h:#x},{t},"{hashlib.sha256(raw(l,h-l)).hexdigest()}","{k}"}},')
lines+=['};','struct guard {unsigned va;const char* bytes;};','inline const std::vector<guard> ledger{']
for r in ledger['objectives']:lines.append(f' {{{int(r["addr"],16):#x},"{r["bytes"]}"}},')
for a in caps+[0x4c0584,0x4c083c,0x4c0644,0x4c05d9,0x4c05de,0x4c07c5]:lines.append(f' {{{a:#x},"{ins(a).bytes.hex()}"}},')
lines+=['};',f'inline constexpr unsigned objective_bitmap={bitmap:#x},objective_count={count:#x},objective_manager_size={manager_size:#x};',f'inline constexpr unsigned talent_a={a_pool:#x},talent_b={b_pool:#x},talent_c={c_pool:#x},talent_size={registry_size:#x};']
for name,d in [('a',A),('b',B),('c',C)]:
    for k,v in d.items():lines.append(f'inline constexpr unsigned {name}_{k}={v:#x};')
lines.append('}')
(ROOT/'src/save_limits_sites.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
print('objective ledger',len(ledger['objectives']),'talent capacities',len(caps),'edits',len(patches),'caves',len(spans),'replacements',len(replacements),'manager',hex(manager_size),'registry',hex(registry_size))
