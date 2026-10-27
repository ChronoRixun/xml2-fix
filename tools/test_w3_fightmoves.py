"""Static ledger contract, with the retail executable as read-only input."""
import importlib.util
import os
from pathlib import Path
import unittest
import re
import subprocess
import sys
import json

ROOT = Path(__file__).resolve().parents[1]

class FightMovesLedger(unittest.TestCase):
    def test_generated_ledger_and_complete_scan(self):
        path = ROOT / 'tools/w3_fightmoves_ledger.py'
        self.assertTrue(path.exists(), 'FightMoves generator is required')
        spec = importlib.util.spec_from_file_location('ledger', path)
        ledger = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ledger)
        header, rows = ledger.generate(os.environ['XML2_FIX_TEST_EXE'], verify=True)
        self.assertEqual(header, (ROOT / 'src/w3_fightmoves_sites.hpp').read_text())
        self.assertGreater(len(rows), 50)
        self.assertIn('0x500b94', header)
        self.assertIn('0x500de0', header)

    def test_guard_vs_foreign_sites_including_styles(self):
        spec = importlib.util.spec_from_file_location('ledger', ROOT / 'tools/w3_fightmoves_ledger.py')
        ledger = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ledger)
        _, rows = ledger.generate(os.environ['XML2_FIX_TEST_EXE'])
        touched = {va+off+i for va,raw,off,width,value,field in rows for i in range(width)} | set(range(0x500de0,0x500de6))
        owned={'fight_moves.cpp','fight_moves.hpp','fight_moves_layout.hpp','w3_fightmoves_sites.hpp'}
        sources=[p for p in (ROOT/'src').iterdir() if p.suffix in ('.hpp','.cpp') and p.name not in owned]
        styles=Path(os.environ.get('XML2_FIX_STYLES_SOURCE','D:/Projects/agent-work/w3-styles/xml2-fix'))
        sources += [styles/'src/w3_style_manager_sites.hpp',styles/'src/style_manager_plan.hpp']
        overlaps=[];count=0
        for source in sources:
            for m in re.finditer(r'\{\s*(0x[0-9a-fA-F]+)\s*,\s*"([0-9a-fA-F]+)"',source.read_text(encoding='utf-8')):
                va=int(m[1],16);size=len(m[2])//2
                if len(m[2])%2:continue
                # Exact runtime descriptor_guard transformation, not an overlap exemption.
                if source.name=='move_triggers_rules.hpp':
                    if va==0x4f7a9d:size=1
                    if va==0x4f6260:size=6
                count+=1
                hit=touched & set(range(va,va+size))
                if hit:overlaps.append((str(source),hex(va),list(map(hex,sorted(hit)))))
        self.assertFalse(overlaps,overlaps)
        print(f'PASS guard-vs-site: {count} foreign guard/site spans, {len(touched)} FightMoves write bytes, zero overlaps (MoveTriggers, rules 42/46/48, styles e093257)')

HOST = os.environ.get('XML2_FIX_FIGHT_MOVES_HOST')
if HOST:
    class FightMovesNative(unittest.TestCase):
        def test_native_matrix(self):
            modes=['off','stock','invalid','base','exists','badguard','protect','rollback','readback','retain',
                   'retail','negative','333','1024','2048','combo','counters','failfast']
            modes += [f'guard{i}' for i in range(67)]
            rows=[]
            for mode in modes:
                with self.subTest(mode=mode):
                    p=subprocess.run([HOST,os.environ['XML2_FIX_TEST_EXE'],mode],capture_output=True,text=True,creationflags=0x08000000)
                    expected=0xc0000602 if mode=='failfast' else 0
                    self.assertEqual(p.returncode&0xffffffff,expected,p.stdout+p.stderr)
                    rows.append(dict(mode=mode,exit=p.returncode,expected=expected,output=p.stdout.strip()))
                    print(f'PASS native {mode}: {p.stdout.strip() or "expected fail-fast 0xc0000602"}')
            if path:=os.environ.get('XML2_FIX_FIGHT_MOVES_RESULTS'):
                Path(path).write_text(json.dumps(rows,indent=2),encoding='utf-8',newline='\n')

if __name__ == '__main__':
    unittest.main()
