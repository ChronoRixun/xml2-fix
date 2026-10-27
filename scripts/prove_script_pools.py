"""W3-8 OFF / 080c union proof in the synthetic retail-image host; no game.

Inputs are supplied explicitly; output and copied inputs stay in build/.
The private native runner must accept <exe> <log> <arguments...>, ISO_CWD.
The registry is exported read-only before/after each case and must be identical.
"""
from pathlib import Path
import argparse
import collections
import configparser
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import pefile

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','ref','dll','host','engine','ini','data','ui','runner'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    out=ROOT/'build/script-pools-proof';out.mkdir(parents=True,exist_ok=True)
    pe=pefile.PE(str(args.exe));image=pe.get_memory_mapped_image()
    key=r'HKCU\Software\Activision\X-Men Legends 2'
    def export(path):
        subprocess.run(['reg','export',key,str(path),'/y'],check=True,capture_output=True)
    def capture(tag,source,setting,minimal=False):
        folder=out/tag;folder.mkdir(exist_ok=True)
        shutil.copytree(args.data,folder/'Data',dirs_exist_ok=True)
        shutil.copytree(args.ui,folder/'UI',dirs_exist_ok=True)
        shutil.copyfile(source/'dinput.dll',folder/'dinput.dll')
        cfg=configparser.ConfigParser(strict=False,interpolation=None);cfg.optionxform=str
        cfg.read(args.ini,encoding='utf-8')
        if minimal:
            cfg.clear()
            for section in ('Game','Limits','Online'):cfg.add_section(section)
        for section in ('Test','TestMode'):cfg.remove_section(section)
        for option in ('Host','JoinCode','Broker','BrokerPin'):cfg.remove_option('Online',option)
        if not cfg.has_section('Discord'):cfg.add_section('Discord')
        cfg['Discord']['Enabled']='0'
        cfg['Game']['SaveFolder']='ScriptPools union fixture'
        cfg.remove_option('Limits','ScriptPools')
        if setting is not None:cfg['Limits']['ScriptPools']=setting
        with (folder/'xml2-fix.ini').open('w',encoding='utf-8',newline='\n') as f:cfg.write(f)
        matches=[line for line in (source/'dinput.map').read_text().splitlines()
                 if '?capabilities@' in line and '@gameplay_agreement@@3Vrecord@gameplay_capabilities@@A' in line]
        assert len(matches)==1,matches
        rva=int(matches[0].split()[-2],16)-0x10000000
        before=folder/'registry-before.reg';after=folder/'registry-after.reg';export(before)
        env=dict(os.environ,ISO_CWD=str(folder),ISO_TIMEOUT='180',TEMP=str(out),TMP=str(out))
        cmd=[sys.executable,'-B',str(args.runner),str(args.host),str(folder/'host.log'),
             str(folder/'dinput.dll'),str(args.exe),str(args.engine),str(folder/'out.tsv'),hex(rva),str(folder/'agreement.bin'),'108']
        try:
            result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=200)
        finally:export(after)
        assert before.read_bytes()==after.read_bytes(),tag+' registry changed'
        assert result.returncode==0,(tag,result.returncode,result.stdout,result.stderr,(folder/'host.log').read_text(errors='replace'))
        log=(folder/'xml2-fix.log').read_text(errors='replace')
        assert 'unverified' not in log.lower() and 'startup refused' not in log.lower(),(tag,log[-3000:])
        wire=(folder/'agreement.bin').read_bytes();assert len(wire)==108 and wire[:6]==b'GCAP\x04\0'
        assert ('rule 59 (ScriptPools) revision 1 {4096,0,0,0}' in log)==(setting=='x1')
        if setting=='x1':
            assert 'ScriptPools: committed' in log
            assert 'forced teams: 0x' not in log and 'value makers do not match' not in log
        writes={};bases={}
        for line in (folder/'out.tsv').read_text().splitlines():
            row=line.split('\t')
            if row[0]=='I':bases[row[1].lower()]=int(row[2],16)
            elif row[0]=='W' and row[1]=='XMen2.exe':writes[int(row[2],16)]=int(row[4],16)
        count=struct.unpack_from('<H',wire,6)[0]
        print(f'{tag}: {count} verified rules, {len(writes)} retail image writes, registry equal',flush=True)
        return dict(tag=tag,writes=writes,base=bases['dinput.dll'],
                    size=pefile.PE(str(source/'dinput.dll'),fast_load=True).OPTIONAL_HEADER.SizeOfImage,
                    wire=wire.hex(),rules=count,registry_equal=True,
                    dll_sha256=hashlib.sha256((source/'dinput.dll').read_bytes()).hexdigest())
    def byte(row,a):return row['writes'].get(a,image[a-0x400000] if 0<=a-0x400000<len(image) else None)
    def word(row,a):
        b=[byte(row,a+i) for i in range(4)]
        return None if None in b else struct.unpack('<I',bytes(b))[0]
    def kind(row,a):
        v=word(row,a)
        if v is None:return None
        if row['base']<=v<row['base']+row['size']:return 'DLL absolute pointer'
        if row['base']<=((a+4+v)&0xffffffff)<row['base']+row['size']:return 'DLL relative branch'
        if not 0x400000<=v<0xb00000 and 0x10000<=v<0x60000000:return 'heap pointer'
        return None
    def compare(a,b):
        diff=sorted(x for x in a['writes'].keys()|b['writes'].keys() if byte(a,x)!=byte(b,x))
        unknown=[];normal=collections.Counter()
        for x in diff:
            why=next((kind(a,s) for s in range(x-3,x+1) if kind(a,s) and kind(a,s)==kind(b,s)),None)
            if why:normal[why]+=1
            else:unknown.append(hex(x))
        return dict(raw_differing_bytes=len(diff),relocation_bytes=dict(normal),unexplained=unknown)
    runs=[capture('base-absent',args.ref,None),capture('base-absent2',args.ref,None),
          capture('new-absent',args.dll,None),capture('new-absent2',args.dll,None),
          capture('new-stock620',args.dll,'620'),capture('new-x1',args.dll,'x1')]
    comparisons={f'{a}:{b}':compare(runs[a],runs[b]) for a,b in ((0,1),(2,3),(0,2),(1,3),(2,4))}
    for tag,result in comparisons.items():
        print(tag,result,flush=True);assert not result['unexplained'],(tag,result)
    assert len({r['wire'] for r in runs[:-1]})==1,'OFF agreement differs from base'
    sites=[(int(a,16),len(bytes.fromhex(h))) for a,h in re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"',(ROOT/'src/script_pool_sites.hpp').read_text())]
    for run in runs[:-1]:
        for start,n in sites:assert all(byte(run,start+i)==image[start+i-0x400000] for i in range(n)),(run['tag'],hex(start))
    assert runs[-1]['wire']!=runs[2]['wire'] and runs[-1]['rules']==runs[2]['rules']+1
    runs.append(capture('new-key-only',args.dll,'x1',minimal=True))
    result=dict(reference_commit='69f5115',off_script_bytes_changed=0,off_agreement_identical=True,
                comparisons=comparisons,runs=[{k:v for k,v in r.items() if k!='writes'} for r in runs])
    (out/'proof.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('PASS ScriptPools OFF: 0 site bytes, base agreement identical; 080c+x1 and key-only installs verify rule 59',flush=True)


if __name__=='__main__':main()
