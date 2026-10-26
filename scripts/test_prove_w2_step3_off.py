"""Sparse import-write reconstruction regression; invented bytes only."""
from types import SimpleNamespace
import unittest

from prove_w2_step3_off import reconstruct_image


class ReconstructionTest(unittest.TestCase):
    def test_resolved_import_byte_can_equal_callback_byte(self):
        base = 0x400000
        raw = bytearray(64)
        raw[16:20] = bytes.fromhex("90292d00")
        raw[24:28] = bytes.fromhex("b2292d00")
        before = bytearray(raw)
        before[16:20] = bytes.fromhex("80675b75")
        before[24:28] = bytes.fromhex("20304050")
        pe = SimpleNamespace(
            OPTIONAL_HEADER=SimpleNamespace(ImageBase=base, SizeOfImage=len(before)),
            DIRECTORY_ENTRY_IMPORT=[SimpleNamespace(imports=[
                SimpleNamespace(address=base + 16), SimpleNamespace(address=base + 24)])],
            get_memory_mapped_image=lambda: raw)
        # Low byte 0x80 is omitted: it already equals the resolved import.
        changes = {base + 17: 0xc2, base + 18: 0x3f, base + 19: 0x59, base + 4: 0xe9}
        after = reconstruct_image(pe, before, changes)
        self.assertEqual(after[16:20], bytes.fromhex("80c23f59"))
        self.assertEqual(after[24:28], raw[24:28])  # untouched imports stay raw
        self.assertEqual(after[4], 0xe9)
        expected = bytearray(raw)
        expected[16:20] = bytes.fromhex("80c23f59")
        expected[4] = 0xe9
        self.assertEqual(after, expected)
        with self.assertRaises(AssertionError):
            reconstruct_image(pe, before[:-1], changes)
        # Retail can end before its last section's file-alignment padding.
        pe.OPTIONAL_HEADER.SizeOfImage -= 4
        self.assertEqual(reconstruct_image(pe, before[:-4], changes), expected)


if __name__ == "__main__":
    unittest.main()
