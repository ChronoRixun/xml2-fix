"""E4 absent-key OFF proof against an exact baseline DLL/map.

Same arguments and whole-page/generated-allocation comparison as
prove_w2_step3_off.py. This capture directly names and validates the actual
108-byte GCAP wire array (after 256 20-byte entry slots); the host also knows
how to find that wire array from an object RVA by its GCAP magic.
All test copies set NoActivate=1; no game entry point is executed.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import pefile
import prove_w2_step3_off as proof


def capture(tag,dll_dir,host,retail,engine,data,out):
    folder=out/tag;folder.mkdir(parents=True,exist_ok=True)
    shutil.copytree(data,folder/'Data',dirs_exist_ok=True)
    shutil.copyfile(dll_dir/'dinput.dll',folder/'dinput.dll')
    mode=os.environ.get('XML2_FIX_OFF_CASE','w2')
    configurations={
        'e10a2':'[Save]\nFormat=lc\n[Limits]\nEntityCapacity=1\n',
        'retail':'[Save]\nFormat=lc\n',
        'native':'',
        'w2':'[Save]\nFormat=lc\n[Limits]\nEntityCapacity=1\nSaveLayout=w2\n',
        'w2zero':'[Save]\nFormat=lc\n[Limits]\nEntityCapacity=1\nSaveLayout=w2\n[Game]\nXml1Economy=0\n',
    }
    (folder/'xml2-fix.ini').write_text(configurations[mode]+'\n[Discord]\nEnabled=0\n[Test]\nNoActivate=1\n',encoding='utf-8',newline='\n')
    symbols=proof.symbols(dll_dir/'dinput.map')
    record=next(a for a,n in symbols if n.startswith('?capabilities@') and '3Vrecord@gameplay_capabilities@@A' in n)
    wire_rva=record-0x10000000+256*20
    code=folder/'code'
    if code.exists():
        assert code.resolve().is_relative_to(out.resolve())
        shutil.rmtree(str(code.resolve()))
    code.mkdir()
    before=folder/'before-image.bin'
    env=dict(os.environ,UNION_CODE_OUT=str(code),UNION_PRIVATE_DESKTOP='1',UNION_IMAGE_OUT=str(before))
    command=[str(host),str(folder/'dinput.dll'),str(retail),str(engine),str(out/(tag+'.tsv')),hex(wire_rva),str(out/(tag+'.gcap')),'108']
    p=subprocess.run(command,cwd=folder,env=env,capture_output=True,creationflags=0x08004000,timeout=180)
    (out/(tag+'-host.log')).write_bytes(p.stdout+p.stderr)
    assert p.returncode==0,(tag,p.returncode,p.stdout[-500:],p.stderr[-500:])
    wire=(out/(tag+'.gcap')).read_bytes()
    assert len(wire)==108 and wire[:4]==b'GCAP',(tag,'actual GCAP wire not finalized')
    log=(folder/'xml2-fix.log').read_text(errors='replace')
    assert 'Actors: 96 installed' not in log
    if mode in ('w2','w2zero'):assert 'rule 47 NativeSaveLayout verified' in log
    changes={};modules={}
    for line in (out/(tag+'.tsv')).read_text().splitlines():
        fields=line.split('\t')
        if fields[0]=='I':modules[fields[1]]=int(fields[2],16)
        elif fields[0]=='W' and fields[1]=='XMen2.exe':changes[int(fields[2],16)]=int(fields[4],16)
    image=proof.reconstruct_image(pefile.PE(str(retail)),before.read_bytes(),changes)
    dll=pefile.PE(str(dll_dir/'dinput.dll')).get_memory_mapped_image()
    blocks={int(p.stem,16):p.read_bytes() for p in code.glob('*.bin') if re.fullmatch(r'[0-9a-fA-F]+',p.stem)}
    backings={int(p.stem[5:],16):p.read_bytes() for p in code.glob('data-*.bin')}
    return image,changes,symbols,modules['dinput.dll'],dll,blocks,backings


if __name__=='__main__':
    proof.capture=capture
    proof.main()
    print('E4 Actors key absent: complete watched image, referenced backings, generated code and actual GCAP wire unchanged')
