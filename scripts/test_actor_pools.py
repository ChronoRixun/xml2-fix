"""Run the stage-1 game-free gates. Never starts XMen2.exe or an engine.

python scripts/test_actor_pools.py --exe <retail> --db <native.sqlite> --audit <stage0.md>
Build first: cmake --build build --config Release --target xml2_actor_pools_host
  xml2_actor_ai_descriptor_test xml2_patch_transaction_test -- /nr:false /m:2
"""
import argparse
import subprocess
from pathlib import Path
import sys


def main():
    a=argparse.ArgumentParser();a.add_argument('--exe',required=True);a.add_argument('--db',required=True)
    a.add_argument('--audit',required=True);a.add_argument('--build',default='build');args=a.parse_args()
    root=Path(__file__).resolve().parents[1];build=(root/args.build).resolve();bin=build/'bin/Release'
    def run(name,command):
        r=subprocess.run([str(x) for x in command],cwd=root,capture_output=True,text=True,timeout=120)
        (build/f'{name}.log').write_text(r.stdout+r.stderr,encoding='utf-8',newline='\n')
        print(f'{name}: exit {r.returncode}')
        if r.returncode:print(r.stdout+r.stderr)
        assert r.returncode==0,name
        return r.stdout
    run('actor-ledger',[sys.executable,'-B',root/'scripts/entity_capacity_b_sites.py','--exe',args.exe,'--db',args.db,'--check'])
    run('actor-audit',[sys.executable,'-B',root/'test/actor_pools_ledger_test.py','--audit',args.audit])
    run('actor-descriptor',[bin/'xml2_actor_ai_descriptor_test.exe'])
    for mode in ('off','invalid','guard','allocation','protect','stock','on'):
        run('actor-'+mode,[bin/'actor_pools_host.exe',args.exe,mode,build/f'actor-{mode}-trace.bin'])
    stock=(build/'actor-stock-trace.bin').read_bytes();raised=(build/'actor-on-trace.bin').read_bytes()
    assert stock==raised,'T7 accept/refuse/live-count sequences differ'
    print(f'PASS T7 exact differential: {len(stock)} identical trace bytes, 4000 steps per pool')
    run('actor-transaction',[bin/'xml2_patch_transaction_test.exe'])
    print('PASS stage1 T1-T11 game-free gates')


if __name__=='__main__':main()
