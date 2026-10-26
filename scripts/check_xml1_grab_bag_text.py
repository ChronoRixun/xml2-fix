"""Read-only retail guard/ABI/census gate for the complete bag reward hook."""
from pathlib import Path
import hashlib
import struct
import sys

import capstone
import pefile


def main():
    raw = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b"
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    source = (Path(__file__).resolve().parents[1] / "src/xml1_grab_bag.cpp").read_text()
    assert '0x5d3720,patch_transaction::bytes_of("81ec1c010000")' in source
    assert image[0x1d3720:0x1d3726] == bytes.fromhex("81ec1c010000")
    # The preserved caller computes/negates the listed price, charges once,
    # then dispatches row type 1 to the replaced reward entry.
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    code = list(md.disasm(image[0x1d3ff0:0x1d40a8], 0x5d3ff0))
    calls = [(i.address, i.operands[0].imm) for i in code
             if i.id == capstone.x86.X86_INS_CALL and i.operands[0].type == capstone.x86.X86_OP_IMM]
    assert calls.count((0x5d3ffe, 0x5d22f0)) == 1
    assert calls.count((0x5d40a3, 0x5d3720)) == 1
    assert image[0x1d3ff9:0x1d3ffe] == bytes.fromhex("f7d8508bce")
    assert image[0x1d384d:0x1d3853] == bytes.fromhex("ff9038010000")
    assert struct.unpack_from("<I", image, 0x1d388f)[0] == 0x6a2150
    assert image[0x2a2150:0x2a216a].split(b"\0")[0] == b"loadDangerRoomCourse('%s')"
    # No external caller may bypass the new entry by entering its old body.
    md.skipdata = True
    reward_callers = []
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        lo = section.VirtualAddress
        for ins in md.disasm(image[lo:lo + section.Misc_VirtualSize], 0x400000 + lo):
            if not ins.id or not (ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)):
                continue
            for op in ins.operands:
                if op.type != capstone.x86.X86_OP_IMM:
                    continue
                if op.imm == 0x5d3720:
                    reward_callers.append(ins.address)
                if not 0x5d3720 <= ins.address < 0x5d39b7:
                    assert not 0x5d3720 < op.imm < 0x5d39b7, hex(ins.address)
    assert reward_callers == [0x5d40a3], reward_callers
    print("PASS: 1 retail guard, sole reward caller 5d40a3, preserved single-debit buy path, no exterior branches into old reward body, native script route")


if __name__ == "__main__":
    main()
