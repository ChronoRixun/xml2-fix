"""Check the prices/sales guards and preserved contexts on the owner's retail PE.

Read-only. No game launch, extracted listing or owned binary is emitted.
"""
from pathlib import Path
import hashlib
import re
import struct
import sys

import capstone
import pefile


def main():
    raw = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b"
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    source = (Path(__file__).resolve().parents[1] / "src/xml1_economy_prices.cpp").read_text()
    guards = [(int(a, 16), bytes.fromhex(b)) for a, b in re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"', source)]
    assert len(guards) == 7
    guards.append((0x5d2639, bytes.fromhex("5357e8c0e3eaff")))
    for va, expected in guards:
        assert image[va - 0x400000:va - 0x400000 + len(expected)] == expected, hex(va)
    # All three initializers enter the same cache calculator, including load.
    for caller in (0x481b6e, 0x481d13, 0x482bd3):
        at = caller - 0x400000
        assert image[at] == 0xe8
        assert caller + 5 + struct.unpack_from("<i", image, at + 1)[0] == 0x481170
    assert image[0x80dde:0x80de4] == bytes.fromhex("83c408c20800")
    assert image[0x1d2628:0x1d2639] == bytes.fromhex("8a86e818000083c424a8010f859a000000")
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    decoder.skipdata = True
    displaced = [(0x481170, 0x481176), (0x480dd8, 0x480dde), (0x5a9b8d, 0x5a9b92),
                 (0x5d3ee9, 0x5d3eef), (0x5d404b, 0x5d4051), (0x5d2639, 0x5d2640)]
    cache_callers = set()
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        lo = section.VirtualAddress
        for ins in decoder.disasm(image[lo:lo + section.Misc_VirtualSize], 0x400000 + lo):
            if not ins.id or not (ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)):
                continue
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_IMM:
                    if ins.group(capstone.CS_GRP_CALL) and op.imm == 0x481170:
                        cache_callers.add(ins.address)
                    assert not any(a < op.imm < b for a, b in displaced), f"branch into displaced span at {ins.address:#x}"
    assert cache_callers == {0x481b6e, 0x481d13, 0x482bd3}, cache_callers
    print("PASS: 8 prices/sales retail guards, 3 cache callers, pack epilogue, stash bypass, no interior branch targets")


if __name__ == "__main__":
    main()
