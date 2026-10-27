"""T12-T25 game-free native core gates. Never starts the game or an engine."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',required=True);parser.add_argument('--exe',required=True);parser.add_argument('--out',required=True)
    args=parser.parse_args();host=Path(args.host).resolve();exe=Path(args.exe).resolve();out=Path(args.out).resolve();out.mkdir(parents=True,exist_ok=True)
    rows=int(re.search(r'std::array<edit,(\d+)>', (ROOT/'src/e4_core_sites.hpp').read_text())[1])
    results=[]
    def run(mode,extra=(),expected=0):
        folder=out/mode;folder.mkdir(exist_ok=True)
        command=[str(host),str(exe),mode,*map(str,extra)]
        p=subprocess.run(command,cwd=folder,capture_output=True,timeout=60,creationflags=0x08004000)
        (folder/'host.log').write_bytes(p.stdout+p.stderr)
        result=dict(mode=mode,exit=p.returncode,expected=expected)
        assert p.returncode==expected,(mode,p.returncode,p.stdout[-2000:],p.stderr[-500:])
        if expected==77:assert b'invariant broken' in p.stdout or b'construction escaped' in p.stdout,mode
        results.append(result)
        return p.stdout
    negative=['off','invalid']+[f'{kind}{i}' for kind in ('missing','wrong','unverified') for i in range(6)]+[f'guard{i}' for i in range(rows)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for completed in concurrent.futures.as_completed([pool.submit(run,mode) for mode in negative]):completed.result()
    print(f'PASS T25/T3: {len(negative)} absent/invalid/prerequisite/retail-guard refusal cases',flush=True)
    run('on')
    telemetry=json.loads((out/'on/e4-core-observation.json').read_text())
    assert telemetry['native_reserved']==0 and telemetry['pending_reservations']==[0,0,0] and telemetry['foldbacks']>0
    run('reservation');run('aliases');run('destructors');run('backstop',expected=77)
    for phase in range(4):
        for suffix in ('','-step'):run(f'throw{phase}{suffix}',expected=77)
    run('throw4')
    print('PASS T12/T13/T16-T20/T23/T24: native boundary, churn, aliases, reservation and exception gates',flush=True)
    traces={}
    for mode in ('stock-trace','trace0','trace1','trace2','trace3'):
        path=out/(mode+'.trace');run(mode,[path]);traces[mode]=path.read_bytes()
    assert all(traces[name]==traces['stock-trace'] for name in ('trace0','trace1','trace2','trace3')),'T14/T21 stock team/search/accept/brain traces differ'
    print('PASS T14/T21: stock and four encodings;',len(traces['trace0']),'identical normalized native trace bytes',flush=True)
    brains=[]
    for mode in ('stock-brain','brain0','brain1','brain2','brain3'):
        path=out/(mode+'.trace');run(mode,[path]);brains.append(path.read_bytes())
    assert all(body==brains[0] for body in brains),'T9/T21 native brain states differ under token variants'
    print('PASS T8/T9/T19/T21: three native brain paths, real null/team-destruction cases, actual cave borrow return, complete FX state and four encodings',flush=True)
    folded=[]
    for variant in range(4):
        mode=f'foldtrace{variant}';trace=out/(mode+'.trace');state=out/(mode+'.state');run(mode,[trace,state]);folded.append(trace.read_bytes())
    assert all(body==folded[0] for body in folded),'T21 post-fold encoding traces differ'
    clone=out/'stockclone.trace';run('stockclone',[clone,out/'foldtrace0.state']);assert clone.read_bytes()==folded[0],'T15 stock clone evolution differs'
    print('PASS T15/T21: stock-layout folded-state clone and four encodings;',len(folded[0]),'identical trace bytes',flush=True)
    reference=out/'tref.post';pre=(out/'foldtrace0.state.pre').read_bytes();run('tref',[out/'foldtrace0.state.pre',reference])
    actual=(out/'foldtrace0.state.post').read_bytes();expected=reference.read_bytes();assert len(actual)==len(expected)==32*0x61c
    hero_differences=0;stride=8+0x844+0x61c
    for i in range(32):
        record=pre[8+i*stride:8+(i+1)*stride];extended=int.from_bytes(record[:4],'little');faction=int.from_bytes(record[8+0x800:8+0x804],'little')
        left=bytearray(actual[i*0x61c:(i+1)*0x61c]);right=bytearray(expected[i*0x61c:(i+1)*0x61c])
        if extended and faction==29:
            assert left[0x538:0x53c]==b'\0'*4 and right[0x538:0x53c]==record[-0x61c+0x538:-0x61c+0x53c]
            right[0x538:0x53c]=b'\0'*4;hero_differences+=1
        assert left==right,('T22 non-membership bytes differ',i)
    assert hero_differences>0
    print('PASS T22: 50,048 complete controller bytes equal actual retail timeout, except stated hero token',flush=True)
    summary=dict(cases=sorted(results,key=lambda r:r['mode']),negative_cases=len(negative),native_trace_sha256=hashlib.sha256(traces['trace0']).hexdigest(),brain_trace_sha256=hashlib.sha256(brains[0]).hexdigest(),fold_trace_sha256=hashlib.sha256(folded[0]).hexdigest(),hero_exceptions=hero_differences)
    (out/'results.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('PASS E4 core host gates:',len(results),'process cases',flush=True)


if __name__=='__main__':main()
