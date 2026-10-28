"""Compare step-3 OFF installs against the exact pre-change checkout, in the union host.

Usage: python scripts/prove_w2_step3_off.py <baseline DLL dir> <current DLL dir>
       <union_host.exe> <retail exe> <engine dir> <Data input dir> <output dir> <W2 pages file>
All outputs (including the copied Data) go under output dir. No game launches.
Raw literals must match byte for byte; DLL-bound function addresses are compared
by linker symbol and addend because code locations move in a rebuilt DLL.
"""
from pathlib import Path
import os
import bisect
import hashlib
import re
import shutil
import struct
import subprocess
import sys
import pefile
import capstone
from capstone import x86


def code_extent(body):
    # Allocations include zero page padding. Keep enough trailing bytes for any
    # final x86 instruction; all padding still participates in the raw equality
    # check below. Zero padding cannot contain a non-null backing reference.
    return body[:min(len(body),len(body.rstrip(b"\0"))+15)]


def symbols(path):
    out=[]
    for line in path.read_text(errors="replace").splitlines():
        m=re.match(r"\s+[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})(?:\s|$)",line,re.I)
        if m:out.append((int(m[2],16),m[1]))
    return sorted(out)

def reconstruct_image(pe, before, changes):
    image = bytearray(pe.get_memory_mapped_image())
    assert len(before) == pe.OPTIONAL_HEADER.SizeOfImage, "pre-patch image size differs from retail SizeOfImage"
    # Imports resolve BEFORE the host snapshot; its write log is sparse. A
    # callback byte equal to the resolved import byte is omitted even if it
    # differs from the raw PE. Recover each touched import slot in full before
    # applying the writes. Untouched imports stay raw (unrelated loader ASLR).
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in entry.imports:
            if any(imp.address + n in changes for n in range(4)):
                off = imp.address - pe.OPTIONAL_HEADER.ImageBase
                assert 0 <= off <= len(before) - 4, "import slot outside captured image"
                image[off:off + 4] = before[off:off + 4]
    for va, value in changes.items():
        image[va - pe.OPTIONAL_HEADER.ImageBase] = value
    return image


def capture(tag,dll_dir,host,retail,engine,data,out):
    folder=out/tag;folder.mkdir(parents=True,exist_ok=True)
    shutil.copytree(data,folder/"Data",dirs_exist_ok=True)
    shutil.copyfile(dll_dir/"dinput.dll",folder/"dinput.dll")
    mode=os.environ.get("XML2_FIX_OFF_CASE","e10a2")
    configurations={"e10a2":"[Save]\nFormat=lc\n[Limits]\nEntityCapacity=1\n","retail":"[Save]\nFormat=lc\n","native":"",
                    "w2":"[Save]\nFormat=lc\n[Limits]\nEntityCapacity=1\nSaveLayout=w2\n",
                    "w2zero":"[Save]\nFormat=lc\n[Limits]\nEntityCapacity=1\nSaveLayout=w2\n[Game]\nXml1Economy=0\n"}
    assert mode in configurations,"XML2_FIX_OFF_CASE must be e10a2, retail, native, w2 or w2zero"
    (folder/"xml2-fix.ini").write_text(configurations[mode]+"\n[Discord]\nEnabled=0\n",encoding="utf-8")
    sym=symbols(dll_dir/"dinput.map")
    cap=[a for a,n in sym if n.startswith("?capabilities@") and "3Vrecord@gameplay_capabilities@@A" in n]
    assert len(cap)==1,cap
    cmd=[str(host),str(folder/"dinput.dll"),str(retail),str(engine),str(out/(tag+".tsv")),hex(cap[0]-0x10000000),str(out/(tag+".gcap")),"108"]
    code=folder/"code";code.mkdir(exist_ok=True)
    for previous in code.glob("*.bin"):os.remove(str(previous.resolve()))
    env=os.environ.copy();env["UNION_CODE_OUT"]=str(code);env["UNION_PRIVATE_DESKTOP"]="1"
    before_image=folder/"before-image.bin";env["UNION_IMAGE_OUT"]=str(before_image)
    r=subprocess.run(cmd,cwd=folder,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=0x08004000,timeout=180)
    (out/(tag+"-host.log")).write_bytes(r.stdout)
    assert r.returncode==0,(tag,r.returncode,r.stdout[-1000:])
    log=(folder/"xml2-fix.log").read_text(errors="replace")
    if mode in ("w2","w2zero"):
        assert "rule 47 NativeSaveLayout verified" in log,(tag,"W2 prerequisite did not verify")
        assert "rule 52" not in log,(tag,"economy OFF registered rule52")
    elif mode=="e10a2":
        assert "entity capacity: installed" in log,(tag,"capacity did not install")
        assert "native layout E10A2" in log,(tag,"OFF did not keep E10A2")
        assert "save manager backing" in log
    elif mode=="retail":
        assert "native layout retail" in log and "entity capacity: installed" not in log,(tag,"OFF did not keep RETAIL")
    else:
        assert "native layout W2" not in log and "LC ([Save] Format=lc)" not in log,(tag,"native OFF unexpectedly installed LC")
    changes={};modules={}
    for line in (out/(tag+".tsv")).read_text().splitlines():
        fields=line.split("\t")
        if fields[0]=="I":modules[fields[1]]=int(fields[2],16)
        elif fields[0]=="W" and fields[1]=="XMen2.exe":changes[int(fields[2],16)]=int(fields[4],16)
    pe=pefile.PE(str(retail));image=reconstruct_image(pe,before_image.read_bytes(),changes)
    dll=pefile.PE(str(dll_dir/"dinput.dll")).get_memory_mapped_image()
    blocks={int(p.stem,16):p.read_bytes() for p in code.glob("*.bin") if re.fullmatch(r"[0-9a-fA-F]+",p.stem)}
    data={int(p.stem[5:],16):p.read_bytes() for p in code.glob("data-*.bin")}
    return image,changes,sym,modules["dinput.dll"],dll,blocks,data

def main():
    ref,mine,host,retail,engine,data,out,page_file=map(lambda s:Path(s).resolve(),sys.argv[1:])
    out.mkdir(parents=True,exist_ok=True)
    # Engine dependencies must never load the installed proxy before the host
    # chooses the DLL under test. Copy only engine DLLs into our output tree.
    source_engine=engine;engine=out/"engine";engine.mkdir(exist_ok=True)
    for dll in source_engine.glob("*.dll"):
        if dll.name.lower()!="dinput.dll":shutil.copyfile(dll,engine/dll.name)
    key=r"HKCU\Software\Activision\X-Men Legends 2"
    before=out/"registry-before.reg";after=out/"registry-after.reg"
    exported=subprocess.run(["reg","export",key,str(before),"/y"],capture_output=True).returncode==0
    assert exported,"registry snapshot unavailable; cannot certify the OFF proof"
    a,wa,sa,ba,da,ca,data_a=capture("baseline",ref,host,retail,engine,data,out)
    b,wb,sb,bb,db,cb,data_b=capture("step3-off",mine,host,retail,engine,data,out)
    if exported:
        assert subprocess.run(["reg","export",key,str(after),"/y"],capture_output=True).returncode==0
        assert before.read_bytes()==after.read_bytes(),"union host altered registry"
    assert (out/"baseline.gcap").read_bytes()==(out/"step3-off.gcap").read_bytes(),"agreement changed while W2 absent"
    # Resolve a DLL address by linker symbol + addend. Static data symbols and
    # compiler literals with renamed discriminator hashes need no matching when
    # an installed address names a public patch adapter (the expected case).
    def token(value,syms,base,dll,blocks,roots):
        if 0x400000<=value<0xb00000:return ("retail",value)
        # MSVC incremental-link jump islands precede real function bodies.
        rva=value-base
        if 0x1000<=rva<0x40000 and dll[rva]==0xe9:
            value=value+5+struct.unpack_from("<i",dll,rva+1)[0]
        backings,data_roots=(data_a,data_roots_a) if blocks is ca else (data_b,data_roots_b)
        for start,body in backings.items():
            if start<=value<start+len(body):
                root=data_roots.get(start)
                return ("guarded data",root,value-start) if root is not None else None
        for start,body in blocks.items():
            if start<=value<start+len(body):
                return ("generated bridge",roots.get(start,"unreferenced"),value-start)
        value=value-base+0x10000000
        index=bisect.bisect_right(syms,(value,chr(0x10ffff)))-1
        if index<0:return None
        addr,name=syms[index]
        if value-addr>0x10000:return None
        return re.sub(r"\?A0x[0-9a-fA-F]+@", "?ANON@", name),value-addr
    pages={int(line,16) for line in page_file.read_text().splitlines() if line.strip()}
    roots_a={};roots_b={};watched_a=set();watched_b=set()
    data_roots_a={};data_roots_b={}
    scan=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);scan.detail=True
    starts=sorted({va-back for va in set(wa)|set(wb) for back in range(5)})
    for va in starts:
        for image,blocks,roots,used in ((a,ca,roots_a,watched_a),(b,cb,roots_b,watched_b)):
            off=va-0x400000
            if off<0:continue
            ins=next(scan.disasm(bytes(image[off:off+16]),va,count=1),None)
            if not ins or not (ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)) or not ins.operands or ins.operands[0].type!=x86.X86_OP_IMM:continue
            target=ins.operands[0].imm
            for start,body in blocks.items():
                if start<=target<start+len(body):
                    roots[start]=min(roots.get(start,va),va)
                    if va&~0xfff in pages:used.add(start)
    # Give guarded RW allocations stable identities from the native/cave
    # instruction that references them. An indexed absolute operand (for example
    # [edx*2 + wide_objective_indices]) still refers to that backing. Validate a
    # one-to-one match and EVERY initial backing byte before normalizing a pointer.
    def backing_references(left,right,key):
        if not left or not right or left.mnemonic!=right.mnemonic or left.size!=right.size:return
        for index,(op,other) in enumerate(zip(left.operands,right.operands)):
            if op.type==other.type==x86.X86_OP_IMM:values=op.imm & 0xffffffff,other.imm & 0xffffffff
            elif op.type==other.type==x86.X86_OP_MEM and op.mem.base==other.mem.base and op.mem.index==other.mem.index and op.mem.scale==other.mem.scale:values=op.mem.disp & 0xffffffff,other.mem.disp & 0xffffffff
            else:continue
            matching=[]
            for value,backings in zip(values,(data_a,data_b)):
                matching.append([start for start,body in backings.items() if start<=value<start+len(body)])
            # A misaligned candidate can coincidentally decode a literal that
            # points into ONE process's allocation. It is not a paired reference
            # and must not assign a backing identity or normalize any byte.
            if len(matching[0])!=1 or len(matching[1])!=1:continue
            identity=key+f":op{index}"
            for identities,found in zip((data_roots_a,data_roots_b),matching):
                start=found[0];identities[start]=min(identities.get(start,identity),identity)
    for va in starts:
        off=va-0x400000
        if off<0:continue
        left=next(scan.disasm(bytes(a[off:off+16]),va,count=1),None)
        right=next(scan.disasm(bytes(b[off:off+16]),va,count=1),None)
        backing_references(left,right,f"game:{va:08x}")
    for start,root in roots_a.items():
        paired=[p for p,r in roots_b.items() if r==root]
        assert len(paired)==1,(root,"ambiguous code root",paired)
        other=paired[0]
        for left in scan.disasm(code_extent(ca[start]),start):
            off=left.address-start
            right=next(scan.disasm(cb[other][off:off+16],other+off,count=1),None)
            backing_references(left,right,f"cave:{root:08x}:{off:08x}")
    for start,root in data_roots_a.items():
        paired=[p for p,r in data_roots_b.items() if r==root]
        assert len(paired)==1,(root,"ambiguous guarded backing",paired)
        assert data_a[start]==data_b[paired[0]],(root,"guarded backing bytes differ")
    assert set(data_roots_a.values())==set(data_roots_b.values()),"guarded backing reference set differs"
    normalized=set();targets=0
    pe=pefile.PE(str(retail));text=next(s for s in pe.sections if s.Name.startswith(b".text"))
    lo,hi=text.VirtualAddress,text.VirtualAddress+text.Misc_VirtualSize
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
    # Only changed locations can introduce a relocated target. Decode one
    # instruction at each changed byte. Direct branches include W2's conditional
    # refusal bridges; absolute memory operands include its first_type2 cell.
    # Normalize only when both resolve to the same named symbol/root + addend.
    for va in starts:
        off=va-0x400000
        if not lo<=off<hi:continue
        ia=next(md.disasm(bytes(a[off:off+16]),va,count=1),None)
        ib=next(md.disasm(bytes(b[off:off+16]),va,count=1),None)
        if not ia or not ib or ia.mnemonic!=ib.mnemonic or ia.size!=ib.size:continue
        if not ia.operands or not ib.operands:continue
        if (ia.group(capstone.CS_GRP_CALL) or ia.group(capstone.CS_GRP_JUMP)) and ia.operands[0].type==x86.X86_OP_IMM and ib.operands[0].type==x86.X86_OP_IMM:
            ta,tb=token(ia.operands[0].imm,sa,ba,da,ca,roots_a),token(ib.operands[0].imm,sb,bb,db,cb,roots_b)
            if ta and tb and ta==tb:
                normalized.update(range(off+ia.imm_offset,off+ia.imm_offset+ia.imm_size));targets+=1
        for op,other_op in zip(ia.operands,ib.operands):
            if op.type==x86.X86_OP_IMM and other_op.type==x86.X86_OP_IMM:
                ta=token(op.imm & 0xffffffff,sa,ba,da,ca,roots_a)
                tb=token(other_op.imm & 0xffffffff,sb,bb,db,cb,roots_b)
                # Native registration sites PUSH/MOV a DLL callback, rather
                # than CALL it directly. Require both values inside their own
                # DLL images and the exact same symbol/addend (or a byte-checked
                # guarded backing), never an arbitrary opaque immediate.
                named_dll_pointer=ba<=op.imm & 0xffffffff<ba+len(da) and bb<=other_op.imm & 0xffffffff<bb+len(db)
                if ta and tb and ta==tb and (ta[0]=="guarded data" or named_dll_pointer):
                    normalized.update(range(off+ia.imm_offset,off+ia.imm_offset+ia.imm_size));targets+=1
            if op.type==x86.X86_OP_MEM and other_op.type==x86.X86_OP_MEM and op.mem.base==other_op.mem.base and op.mem.index==other_op.mem.index and op.mem.scale==other_op.mem.scale:
                ta=token(op.mem.disp & 0xffffffff,sa,ba,da,ca,roots_a)
                tb=token(other_op.mem.disp & 0xffffffff,sb,bb,db,cb,roots_b)
                if ta and tb and ta==tb:
                    normalized.update(range(off+ia.disp_offset,off+ia.disp_offset+ia.disp_size));targets+=1
    # Vtable slots are aligned absolute DWORDs, rather than rel32 instructions.
    for off in sorted({(va-0x400000)&~3 for va in set(wa)|set(wb)}):
        if lo<=off<hi:continue
        va,vb=struct.unpack_from("<I",a,off)[0],struct.unpack_from("<I",b,off)[0]
        ta,tb=token(va,sa,ba,da,ca,roots_a),token(vb,sb,bb,db,cb,roots_b)
        if ta and tb and ta==tb:
            normalized.update(range(off,off+4));targets+=1
    # Compare every byte of every page the W2 union can write. Unrelated
    # background/input state outside these pages is outside this proof.
    watched=[off for page in sorted(pages) for off in range(page-0x400000,min(page-0x400000+0x1000,len(a)))]
    differences=[off for off in watched if a[off]!=b[off] and off not in normalized]
    # Relocated bridge addresses are accepted only when their complete code
    # allocation is identical after resolving its external rel32 calls.
    bridge_count=0
    for start,root in roots_a.items():
        if start not in watched_a:continue
        matches=[q for q,r in roots_b.items() if r==root]
        assert len(matches)==1,(root,matches)
        other=matches[0];left=bytearray(ca[start]);right=bytearray(cb[other]);assert len(left)==len(right),(root,"size")
        neutral=set()
        for ins in scan.disasm(code_extent(bytes(left)),start):
            off=ins.address-start
            paired=next(scan.disasm(bytes(right[off:off+16]),other+off,count=1),None)
            if not paired or ins.mnemonic!=paired.mnemonic:continue
            if not ins.operands or not paired.operands:continue
            # Every immediate transfer (including Jcc) and absolute operand
            # keeps its named target; its encoded address may move with ASLR.
            for op,other_op in zip(ins.operands,paired.operands):
                if op.type==x86.X86_OP_IMM and other_op.type==x86.X86_OP_IMM:
                    ta=token(op.imm & 0xffffffff,sa,ba,da,ca,roots_a)
                    tb=token(other_op.imm & 0xffffffff,sb,bb,db,cb,roots_b)
                    if ta and ta==tb:neutral.update(range(off+ins.imm_offset,off+ins.imm_offset+ins.imm_size))
                if op.type==x86.X86_OP_MEM and other_op.type==x86.X86_OP_MEM and op.mem.base==other_op.mem.base and op.mem.index==other_op.mem.index and op.mem.scale==other_op.mem.scale:
                    ta=token(op.mem.disp & 0xffffffff,sa,ba,da,ca,roots_a)
                    tb=token(other_op.mem.disp & 0xffffffff,sb,bb,db,cb,roots_b)
                    if ta and ta==tb:neutral.update(range(off+ins.disp_offset,off+ins.disp_offset+ins.disp_size))
        for off in neutral:left[off]=right[off]=0
        if left!=right:
            diffs=[i for i,(x,y) in enumerate(zip(left,right)) if x!=y]
            print("bridge differences",hex(root),hex(start),hex(other),len(diffs),[hex(i) for i in diffs[:24]],flush=True)
            (out/"bridge-left.bin").write_bytes(left);(out/"bridge-right.bin").write_bytes(right)
        assert left==right,(hex(root),"generated code changed")
        bridge_count+=1
    (out/"differences.txt").write_text("\n".join(f"{off+0x400000:08x} {a[off]:02x} {b[off]:02x}" for off in differences)+"\n")
    for off in normalized:a[off]=b[off]=0
    print("OFF agreement SHA256",hashlib.sha256((out/"baseline.gcap").read_bytes()).hexdigest())
    print(f"OFF: {len(pages)} full W2 pages ({len(watched)} bytes); {targets} equal relocated targets; {bridge_count} identical generated allocations; {len(differences)} other byte differences")
    print(f"OFF guarded data: {len(data_roots_a)} referenced backings, {sum(len(data_a[p]) for p in data_roots_a)} initial bytes identical")
    print("normalized image SHA256",hashlib.sha256(bytes(a[i] for i in watched)).hexdigest(),hashlib.sha256(bytes(b[i] for i in watched)).hexdigest())
    assert not differences,f"{len(differences)} differences: see {out/'differences.txt'}"
    label="Economy OFF" if os.environ.get("XML2_FIX_OFF_CASE") in ("w2","w2zero") else "W2 absent"
    print(label+": identical baseline literals, control-transfer symbols, pages and agreement; registry unchanged")
if __name__=="__main__":main()
