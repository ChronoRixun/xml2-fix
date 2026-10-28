"""Read-only retail guard/ABI census for the Forge, tabs and style contribution."""
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
    source = (Path(__file__).resolve().parents[1] / "src/xml1_economy_shop.cpp").read_text()
    guards = [(int(a, 16), bytes.fromhex(b)) for a, b in re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"', source)]
    assert len(guards) == 14
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    for va, expected in guards:
        assert image[va - 0x400000:va - 0x400000 + len(expected)] == expected, hex(va)
        assert sum(ins.size for ins in decoder.disasm(expected, va)) == len(expected), (hex(va), "partial instruction")
    # Native call shapes and non-stash/stash branch separation are unchanged.
    assert image[0x6a330:0x6a333] == bytes.fromhex("c20400")
    assert image[0x1d2a3c:0x1d2a55] == bytes.fromhex("f686e8180000017410536a05e8f37afdff83c408e9d8000000")
    for caller, target in ((0x5d4104, 0x5d2d30), (0x5d4119, 0x5d2290), (0x5d412e, 0x5d2810)):
        assert caller + 5 + struct.unpack_from("<i", image, caller - 0x400000 + 1)[0] == target
    # The rare bridge is at the actual style entry, before its unique call.
    assert image[0x80e25:0x80e28] == bytes.fromhex("ff5034")
    # Every new displaced span is instruction-aligned and has no interior
    # direct entry. Calls to its first byte remain supported by the entry hook.
    decoder.skipdata = True
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        lo = section.VirtualAddress
        for ins in decoder.disasm(image[lo:lo + section.Misc_VirtualSize], 0x400000 + lo):
            if not ins.id or not (ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)):
                continue
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_IMM:
                    assert not any(a < op.imm < a + len(b) for a, b in guards), f"interior target {op.imm:#x} from {ins.address:#x}"
    print("PASS: 14 shop retail guards, instruction boundaries, refill ret 4, stash bypass, 3 Training dispatches, unique-first style, no interior branch targets")


if __name__ == "__main__":
    main()
