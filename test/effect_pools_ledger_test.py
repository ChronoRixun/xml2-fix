"""Generator fail-closed controls using only the owner's executable, read-only."""
import importlib.util
import os
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ledger',ROOT/'tools/w3_effect_pools_ledger.py')
ledger=importlib.util.module_from_spec(spec);spec.loader.exec_module(ledger)


class LedgerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=ledger.Image(os.environ.get('XML2_FIX_TEST_EXE','D:/tools/re-projects/bin/XMen2.exe'))

    def test_generated_tables_and_constructor_models(self):
        for name,row in ledger.ROWS.items():
            text,count,guards,flows=ledger.derive(self.image,name,row)
            self.assertEqual((ROOT/'src'/f'{name}_sites.hpp').read_bytes(),text.encode())
            self.assertGreater(count,60);self.assertGreater(guards,30);self.assertGreater(flows,50)

    def test_unknown_metadata_displacement_is_refused(self):
        # Change one decoded owner's metadata displacement by a byte while keeping
        # it inside the owner's metadata range. The generator must diagnose the
        # unclassified field, rather than quietly retaining the retail offset.
        im=self.image
        original=im.data
        for name,row in ledger.ROWS.items():
            pop=next(f for f,r in row['methods'].items() if r=='pop')
            ins=next(i for i in im.fn(pop) if i.disp_size==4)
            offset=ins.address-0x400000+ins.disp_offset
            changed=bytearray(original);changed[offset]^=1;im.data=bytes(changed);im.functions.clear()
            try:
                with self.assertRaisesRegex(AssertionError,'unclassified .* owner operand'):
                    ledger.derive(im,name,row)
            finally:im.data=original;im.functions.clear()


if __name__=='__main__':unittest.main()
