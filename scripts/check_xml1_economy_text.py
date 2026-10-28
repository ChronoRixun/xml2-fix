"""Read-only constructor entry census/guard check on the owned retail PE.

No game launch and no extracted instructions or data emitted into the repo.
Requires the same pefile/capstone environment as the W2 text gates.
"""
import hashlib
import struct
import sys
from pathlib import Path

import capstone
import pefile


def main():
    raw = Path(sys.argv[1]).read_bytes()
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    assert base == 0x400000
    assert hashlib.sha256(raw).hexdigest() == "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b", "retail SHA-256 differs"
    assert image[0x47bb50-base:0x47bb58-base] == bytes.fromhex("83ec08538b5c2414")
    assert image[0x4806ad-base:0x4806b2-base] == bytes.fromhex("e8cec7ffff")
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    decoder.skipdata = True
    direct = []
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        lo = base + section.VirtualAddress
        for ins in decoder.disasm(image[lo-base:lo-base+section.Misc_VirtualSize], lo):
            if not ins.id:
                continue
            if ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP):
                for operand in ins.operands:
                    if operand.type != capstone.x86.X86_OP_IMM:
                        continue
                    target = operand.imm
                    assert not 0x47bb50 < target < 0x47bb58, f"branch into displaced entry at {ins.address:#x}"
                    if target == 0x47bb50:
                        direct.append(ins.address)
    refs = []
    needle = struct.pack("<I", 0x47bb50)
    start = 0
    while (start := image.find(needle, start)) >= 0:
        refs.append(base + start)
        start += 1
    assert refs == [0x6883f4], f"review constructor pointer aliases: {refs}"
    print("Constructor census: direct calls/jumps", [hex(v) for v in direct], "; vtable pointers", [hex(v) for v in refs])
    print("PASS: two retail guards; no branch into displaced entry; item manager vt+0xe0 reaches entry hook")


if __name__ == "__main__":
    main()
