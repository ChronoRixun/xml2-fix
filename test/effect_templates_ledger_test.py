"""Negative census tests; executable/inventory mutations stay entirely in memory."""
import hashlib
from pathlib import Path
import os
import shutil
import sqlite3
import sys
import unittest
from unittest.mock import patch
import uuid
import pefile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import w3_effect_templates_ledger as ledger

EXE=Path(os.getenv('XML2_FIX_TEST_EXE','D:/tools/re-projects/bin/XMen2.exe'))
DB=Path(os.getenv('XML2_FIX_TEST_DB','D:/Projects/agent-work/entity-capacity-impl/round3/native.sqlite'))

class LedgerTests(unittest.TestCase):
    def test_layout(self):
        for n,base,size,mask in ((220,0x4874,0x8a18,0x419c),(512,0xa868,0x14104,0x9894),(2048,0x2a128,0x50344,0x26214)):
            with self.subTest(n=n):
                d=ledger.layout(n)
                self.assertEqual((d['owner_def'],d['owner_size'],d['def_mask']),(base,size,mask))

    def mutated(self,va,hx,match):
        raw=bytearray(EXE.read_bytes());pe=pefile.PE(data=raw)
        offset=pe.get_offset_from_rva(va-0x400000);b=bytes.fromhex(hx);raw[offset:offset+len(b)]=b
        conn=sqlite3.connect('file:'+DB.as_posix()+'?mode=ro',uri=True)
        class Inventory:
            def close(self):pass
            def execute(self,query):
                for row in conn.execute(query):
                    if row[0]==va:row=(row[0],row[1],hx,row[3])
                    yield row
        original=Path.read_bytes
        def read(path):return bytes(raw) if path==EXE else original(path)
        try:
            with patch.object(ledger,'SHA',hashlib.sha256(raw).hexdigest()),patch.object(Path,'read_bytes',read),patch.object(ledger.sqlite3,'connect',return_value=Inventory()):
                with self.assertRaisesRegex(ValueError,match):ledger.derive(EXE,DB)
        finally:conn.close()

    def test_unclassified_pool_operand(self):
        self.mutated(0x405107,'8b96743a0000','unclassified layout operand')

    def test_unclassified_primitive_element(self):
        self.mutated(0x407a8d,'8b4104','unclassified primitive element')

    def test_dll_literal_and_guard_overlap(self):
        root=(ROOT/'build'/('ledger-negative-'+uuid.uuid4().hex)).resolve()
        self.assertTrue(root.is_relative_to(ROOT/'build'))
        (root/'src').mkdir(parents=True)
        try:
            p=root/'src'/'injected.cpp';p.write_text('auto getter=0x4078d0; auto pool=0x4874;\n')
            with self.assertRaisesRegex(ValueError,'DLL retail effect layout literals'):ledger.source_scan(root)
            p.write_text('const guard g{0x405109,"ff"};\n')
            data=ledger.derive(EXE,DB)
            with self.assertRaisesRegex(ValueError,'guard/site overlaps'):ledger.guard_scan(data,root)
        finally:shutil.rmtree(root)

if __name__=='__main__':unittest.main()
