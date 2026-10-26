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


def symbols(path):
    out=[]
    for line in path.read_text(errors="replace").splitlines():
        m=re.match(r"\s+[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})(?:\s|$)",line,re.I)
        if m:out.append((int(m[2],16),m[1]))
    return sorted(out)

def capture(tag,dll_dir,host,retail,engine,data,out):
    folder=out/tag;folder.mkdir(parents=True,exist_ok=True)
    shutil.copytree(data,folder/"Data",dirs_exist_ok=True)
    shutil.copyfile(dll_dir/"dinput.dll",folder/"dinput.dll")
    (folder/"xml2-fix.ini").write_text("[Save]\nFormat=lc\n[Limits]\nEntityCapacity=1\n",encoding="utf-8")
    sym=symbols(dll_dir/"dinput.map")
    cap=[a for a,n in sym if n.startswith("?capabilities@") and "3Vrecord@gameplay_capabilities@@A" in n]
    assert len(cap)==1,cap
    cmd=[str(host),str(folder/"dinput.dll"),str(retail),str(engine),str(out/(tag+".tsv")),hex(cap[0]-0x10000000),str(out/(tag+".gcap")),"108"]
    code=folder/"code";code.mkdir(exist_ok=True)
    for previous in code.glob("*.bin"):os.remove(str(previous.resolve()))
    env=os.environ.copy();env["UNION_CODE_OUT"]=str(code)
    r=subprocess.run(cmd,cwd=folder,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=0x08004000,timeout=180)
    (out/(tag+"-host.log")).write_bytes(r.stdout)
    assert r.returncode==0,(tag,r.returncode,r.stdout[-1000:])
    log=(folder/"xml2-fix.log").read_text(errors="replace")
    assert "entity capacity: installed" in log,(tag,"capacity did not install")
    assert "native layout E10A2" in log,(tag,"OFF did not keep E10A2")
    assert "save manager backing" in log
    changes={};modules={}
    for line in (out/(tag+".tsv")).read_text().splitlines():
        fields=line.split("\t")
        if fields[0]=="I":modules[fields[1]]=int(fields[2],16)
        elif fields[0]=="W" and fields[1]=="XMen2.exe":changes[int(fields[2],16)]=int(fields[4],16)
    pe=pefile.PE(str(retail));image=bytearray(pe.get_memory_mapped_image())
    for va,b in changes.items():image[va-0x400000]=b
    dll=pefile.PE(str(dll_dir/"dinput.dll")).get_memory_mapped_image()
    blocks={int(p.stem,16):p.read_bytes() for p in code.glob("*.bin")}
    return image,changes,sym,modules["dinput.dll"],dll,blocks

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
    a,wa,sa,ba,da,ca=capture("baseline",ref,host,retail,engine,data,out)
    b,wb,sb,bb,db,cb=capture("step3-off",mine,host,retail,engine,data,out)
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
    scan=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);scan.detail=True
    starts=sorted({va-back for va in set(wa)|set(wb) for back in range(5)})
    for va in starts:
        for image,blocks,roots,used in ((a,ca,roots_a,watched_a),(b,cb,roots_b,watched_b)):
            off=va-0x400000
            if off<0:continue
            ins=next(scan.disasm(bytes(image[off:off+16]),va,count=1),None)
            if not ins or ins.mnemonic not in ("call","jmp") or not ins.operands or ins.operands[0].type!=x86.X86_OP_IMM:continue
            target=ins.operands[0].imm
            for start,body in blocks.items():
                if start<=target<start+len(body):
                    roots[start]=min(roots.get(start,va),va)
                    if va&~0xfff in pages:used.add(start)
    normalized=set();targets=0
    pe=pefile.PE(str(retail));text=next(s for s in pe.sections if s.Name.startswith(b".text"))
    lo,hi=text.VirtualAddress,text.VirtualAddress+text.Misc_VirtualSize
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
    # Only changed locations can introduce a relocated target. Decode one
    # instruction at each changed byte and accept paired direct branch targets.
    for va in starts:
        off=va-0x400000
        if not lo<=off<hi:continue
        ia=next(md.disasm(bytes(a[off:off+16]),va,count=1),None)
        ib=next(md.disasm(bytes(b[off:off+16]),va,count=1),None)
        if not ia or not ib or ia.mnemonic!=ib.mnemonic or ia.size!=ib.size:continue
        if ia.mnemonic not in ("call","jmp") or not ia.operands or not ib.operands:continue
        if ia.operands[0].type!=x86.X86_OP_IMM or ib.operands[0].type!=x86.X86_OP_IMM:continue
        ta,tb=token(ia.operands[0].imm,sa,ba,da,ca,roots_a),token(ib.operands[0].imm,sb,bb,db,cb,roots_b)
        if ta and tb and ta==tb:
            normalized.update(range(off+ia.imm_offset,off+ia.imm_offset+ia.imm_size));targets+=1
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
        for ins in scan.disasm(bytes(left),start):
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
                if op.type==x86.X86_OP_MEM and other_op.type==x86.X86_OP_MEM and not op.mem.base and not op.mem.index and not other_op.mem.base and not other_op.mem.index:
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
    print("normalized image SHA256",hashlib.sha256(bytes(a[i] for i in watched)).hexdigest(),hashlib.sha256(bytes(b[i] for i in watched)).hexdigest())
    assert not differences,f"{len(differences)} differences: see {out/'differences.txt'}"
    print("W2 absent: identical baseline literals, control-transfer symbols, pages and agreement; registry unchanged")
if __name__=="__main__":main()
