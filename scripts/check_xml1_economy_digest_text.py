"""Read-only retail guard, ABI and direct-entry census for shop observation."""
from pathlib import Path
import hashlib
import re
import sys
import capstone
import pefile


def main():
    raw = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b"
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    source = (Path(__file__).resolve().parents[1] / "src/xml1_economy_digest.cpp").read_text()
    guards = [(int(a,16),bytes.fromhex(b)) for a,b in re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"',source)]
    assert len(guards) == 8
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    for va, expected in guards:
        assert image[va-0x400000:va-0x400000+len(expected)] == expected, hex(va)
        assert sum(i.size for i in md.disasm(expected, va)) == len(expected), hex(va)
    # Open consumes two stack arguments; update consumes none; both native input
    # routes consume one out-player pointer. The draw counter is the native getter.
    for va, expected in ((0x5d3aa1,"c20800"),(0x5d417d,"c3"),(0x5d4a87,"c20400"),(0x5d4b9d,"c20400"),(0x560180,"a1b4d87a00c3")):
        assert image[va-0x400000:va-0x400000+len(bytes.fromhex(expected))] == bytes.fromhex(expected), hex(va)
    # +0x324 stores a CUIObject pointer: setter, virtual dereference, and
    # the shop's native dynamic_cast argument. Never feed it to the name table.
    for va, expected in ((0x5aafa1,"89be24030000"),(0x5aafda,"f6405408"),(0x5d3d90,"8b8624030000")):
        assert image[va-0x400000:va-0x400000+len(bytes.fromhex(expected))] == bytes.fromhex(expected), hex(va)
    # No immediate direct transfer may enter a displaced span's interior.
    md.skipdata = True
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        lo = section.VirtualAddress
        for i in md.disasm(image[lo:lo+section.Misc_VirtualSize], 0x400000+lo):
            if not i.id or not (i.group(capstone.CS_GRP_CALL) or i.group(capstone.CS_GRP_JUMP)):
                continue
            for op in i.operands:
                if op.type == capstone.x86.X86_OP_IMM:
                    assert not any(a < op.imm < a+len(b) for a,b in guards), f"interior entry {op.imm:#x} from {i.address:#x}"
    print("PASS: 8 digest guards, instruction boundaries, open/update/input ABIs, native lockstep counter, focused-widget pointer contract and no interior direct entries")


if __name__ == "__main__":
    main()
