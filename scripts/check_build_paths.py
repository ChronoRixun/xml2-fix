"""Scan printable ASCII/UTF-16 PE strings for local drive paths.

A drive is a letter, a colon and a slash or backslash with no letter or digit
before it: C:\\x, /D:/x and file:///E:/x match; URL schemes (http://, wss://)
and words such as mode:/ do not. Every hit fails except bytes proven to belong
to the PE base-relocation table: relocation words can spell e.g. T:\\ by
accident; they are loader data, not text.
"""
from pathlib import Path
import argparse
import json
import re
import struct

DRIVE = re.compile(r'(?<![A-Za-z0-9])[A-Za-z]:[\\/]')


def relocation_range(data):
    """Validate the x86 relocation directory before classifying its raw bytes."""
    if data[:2] != b'MZ':
        return (0, 0)
    try:
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe+4] != b'PE\0\0':
            return (0, 0)
        machine, count = struct.unpack_from('<HH', data, pe+4)
        optsize = struct.unpack_from('<H', data, pe+20)[0]
        opt = pe+24
        if machine != 0x14c or struct.unpack_from('<H', data, opt)[0] != 0x10b:
            return (0, 0)
        image_size = struct.unpack_from('<I', data, opt+56)[0]
        rva, size = struct.unpack_from('<II', data, opt+96+5*8)
        for i in range(count):
            section = opt+optsize+i*40
            va, rawsize, raw = struct.unpack_from('<III', data, section+12)
            if va <= rva and rva+size <= va+rawsize:
                first = raw+rva-va
                end = first+size
                cursor = first
                while cursor < end:
                    page, block = struct.unpack_from('<II', data, cursor)
                    if page % 4096 or block < 8 or block % 2 or cursor+block > end:
                        return (0, 0)
                    for off in range(cursor+8, cursor+block, 2):
                        word = struct.unpack_from('<H', data, off)[0]
                        if word >> 12 not in (0, 3) or page+(word & 4095) >= image_size:
                            return (0, 0)
                    cursor += block
                return (first, end) if cursor == end and end <= len(data) else (0, 0)
    except (IndexError, struct.error):
        pass
    return (0, 0)


def scan(data):
    first, end = relocation_range(data)
    hits = []
    for encoding, pattern in (('ascii', rb'[\x20-\x7e]{7,}'), ('utf-16le', rb'(?:[\x20-\x7e]\x00){7,}')):
        for match in re.finditer(pattern, data):
            text = match.group().decode(encoding)
            # A UTF-16 run can begin with the last letter of an ASCII string and its NUL ("p\0" + "O\0:\0/\0"):
            # that letter is not part of the wide string, so it must not hide a drive at the start of it.
            if not (DRIVE.search(text) or (encoding == 'utf-16le' and DRIVE.match(text[1:]))):
                continue
            kind = 'pe-base-relocations' if first <= match.start() and match.end() <= end else 'local-path'
            hits.append(dict(offset=hex(match.start()), encoding=encoding, kind=kind, text=text))
    return hits


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('dll', type=Path)
    args = parser.parse_args()
    hits = scan(args.dll.read_bytes())
    paths = [hit for hit in hits if hit['kind'] == 'local-path']
    print(json.dumps(dict(file=str(args.dll), local_paths=len(paths), broad_hits=len(hits), hits=hits), indent=2))
    return bool(paths)


if __name__ == '__main__':
    raise SystemExit(main())
