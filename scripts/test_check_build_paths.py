"""Negative controls: real path text fails; validated relocation words do not."""
import struct
import unittest
from check_build_paths import scan


class BuildPathsTest(unittest.TestCase):
    def test_paths_fail_and_urls_do_not_match(self):
        # The UTF-16 path directly follows "gns.cpp\0", so its run starts "pO:/": the "p" must not hide the drive.
        data = (b'D:\\private\\gns.cpp\0' + 'O:/private/protobuf.h\0'.encode('utf-16le')
                + b'http://protobuf.dev/programming-guides/enum/#cpp\0'
                + b'http://example.test/D:/private/file.cpp\0')
        hits = scan(data)
        self.assertEqual([h['kind'] for h in hits], ['local-path'] * 3)

    def test_url_schemes_and_words_before_colon_slash_are_not_drives(self):
        # The drive letter needs a left boundary: "http://" ends in "p:/", "wss://" in "s:/", "mode:/" in "e:/".
        for text in ('See https://github.com/ChronoRixun/xml2-fix', 'relay wss://relay.example.org/v1',
                     'http://protobuf.dev/programming-guides/enum/#cpp', 'mode:/fast enabled',
                     'ftp://files.example.org/a', 'key:\\value pair'):
            with self.subTest(text=text):
                self.assertEqual(scan(text.encode('ascii') + b'\0'), [])
                self.assertEqual(scan(text.encode('utf-16le') + b'\0\0'), [])

    def test_drive_after_separator_or_quote_still_fails(self):
        for text in ('"C:\\build\\src\\a.cpp"', 'file:///C:/build/src/a.cpp', 'at /D:/private/x.h',
                     '(E:\\work\\b.cpp)', 'see https://example.org/ then Z:/tmp/c.cpp'):
            with self.subTest(text=text):
                self.assertEqual([h['kind'] for h in scan(text.encode('ascii') + b'\0')], ['local-path'])
                self.assertEqual([h['kind'] for h in scan(text.encode('utf-16le') + b'\0\0')], ['local-path'])

    def test_relocation_validation_does_not_hide_real_path(self):
        data = bytearray(0x400)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 0x80)
        data[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<HH', data, 0x84, 0x14c, 1)
        struct.pack_into('<H', data, 0x94, 224)
        opt = 0x98
        struct.pack_into('<H', data, opt, 0x10b)
        struct.pack_into('<I', data, opt+56, 0x4000)
        struct.pack_into('<II', data, opt+96+40, 0x2000, 20)
        struct.pack_into('<III', data, opt+224+12, 0x2000, 0x200, 0x200)
        struct.pack_into('<II6H', data, 0x200, 0x1000, 20, 0x3a44, 0x3a4c, 0x3a54, 0x3a5c, 0x3a64, 0)
        data[0x280:0x280+19] = b'O:/private/file.cpp'
        hits = scan(data)
        self.assertEqual([h['kind'] for h in hits].count('pe-base-relocations'), 1)
        self.assertEqual([h['kind'] for h in hits].count('local-path'), 1)
        # An invalid relocation kind revokes the structural-data exception.
        struct.pack_into('<H', data, 0x208, 0x4a44)
        self.assertEqual([h['kind'] for h in scan(data)].count('pe-base-relocations'), 0)


if __name__ == '__main__':
    unittest.main()
