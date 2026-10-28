"""Read-only PE guard and hook ownership scan; no game, imports or processes."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b", digest
    u16 = lambda off: struct.unpack_from("<H", data, off)[0]
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    pe = u32(0x3C)
    sections = pe + 24 + u16(pe + 20)
    image = bytearray(u32(pe + 24 + 56))
    for i in range(u16(pe + 6)):
        row = sections + i * 40
        image[u32(row + 12):u32(row + 12) + u32(row + 16)] = data[u32(row + 20):u32(row + 20) + u32(row + 16)]
    root = Path(__file__).resolve().parent.parent
    source = (root / "src/online_menus_guards.hpp").read_text()
    guards = [(int(va, 16), bytes.fromhex(body), what) for va, body, what in
              re.findall(r'\{(0x[0-9a-f]+), "([0-9a-f]+)", "([^"]+)"\}', source)]
    assert guards
    covered = set()
    for va, body, what in guards:
        assert image[va - 0x400000:va - 0x400000 + len(body)] == body, (hex(va), what)
        covered.update(range(va, va + len(body)))
    writes = {0x49FAAC: 5, 0x5F1BB0: 5, 0x6128AB: 5, 0x612D1F: 5, 0x612DDA: 5, 0x5F1F8B: 5, 0x60A095: 5, 0x5B9609: 7, 0x69F688: 4, 0x69F694: 4, 0x69F66C: 4, 0x5C971B: 6, 0x6A23D8: 4, 0x69E454: 4, 0x69E464: 4, 0x5BC3D0: 5, 0x69E440: 4, 0x69E438: 4, 0x69F284: 4, 0x69F350: 4, 0x69F290: 4, 0x69F358: 4, 0x69F2A0: 4, 0x69F368: 4, 0x69F2AC: 4, 0x69F374: 4, 0x6A24AC: 4, 0x69F2A4: 4, 0x69F320: 4, 0x69F328: 4, 0x69F3E8: 4, 0x69F3F0: 4, 0x69F2F4: 4, 0x69F3BC: 4, 0x5BAD1F: 5, 0x5C88BE: 5, 0x6141E0: 5, 0x69F36C: 4, 0x69F314: 4, 0x69F3DC: 4, 0x69E49C: 4}
    touched = {va + i for va, size in writes.items() for i in range(size)}
    assert touched <= covered
    # No other feature writes/guards our mutated bytes, except MAIN_MENU's
    # documented controller call context. Report all static guard intersections.
    overlaps = []
    for path in sorted((root / "src").glob("*")):
        if path.suffix not in (".cpp", ".hpp") or path.name.startswith("online_menus"):
            continue
        for match in re.finditer(r'\{\s*(0x[0-9a-fA-F]+)\s*,\s*"([0-9a-fA-F]{2,})"', path.read_text(errors="replace")):
            va, body = int(match[1], 16), match[2]
            if len(body) % 2 == 0 and touched.intersection(range(va, va + len(body)//2)):
                overlaps.append(dict(file=path.name, va=hex(va), bytes=len(body)//2))
    result = dict(sha256=digest, guards=len(guards), guarded_bytes=len(covered), hook_bytes=len(touched),
                  hook_sites=[hex(v) for v in writes], static_intersections=overlaps)
    print(json.dumps(result, indent=2))
    if args.out:
        args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
