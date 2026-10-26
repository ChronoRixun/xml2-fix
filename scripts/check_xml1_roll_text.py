"""Read-only roller guards, generator census and preserved native paths.

The owned retail PE is supplied locally. No extracted listing is written.
"""
import hashlib
import struct
import sys
from pathlib import Path

import capstone
import pefile
from capstone import x86


def main():
    raw = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b"
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000
    sites = {
        0x46e5e2: "85ed7e148b542474", 0x46e760: "8a44241384c0",
        0x46ea43: "8b4c246064890d00000000", 0x421f3f: "04",
        0x469c80: "83ec145355", 0x448dc0: "83ec10568bf1",
        0x4c908a: "895c2414895c2418", 0x47d4b0: "83ec34538a5c2444",
        0x47ecb0: "83ec105355", 0x47b590: "83ec205356",
        0x481b80: "83ec2c5556", 0x46e735: "6a016a056a02",
        0x46e774: "8b176aff8bcfb301",
    }
    for va, expected in sites.items():
        assert image[va-base:va-base+len(expected)//2] == bytes.fromhex(expected), hex(va)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    decoder.skipdata = True
    calls = {}
    slot_calls = {0x138: [], 0x13c: []}
    # W2 copies/widens the picker body. The economy observes that guarded copy,
    # preserving the retail-to-W2 redirect and its definition-index semantics.
    targets = {0x47d4b0, 0x47ecb0, 0x47b590, 0x481b80, 0x448dc0}
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        start = base + section.VirtualAddress
        for ins in decoder.disasm(image[start-base:start-base+section.Misc_VirtualSize], start):
            if not ins.id:
                continue
            if ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP):
                for operand in ins.operands:
                    if operand.type == x86.X86_OP_IMM:
                        target = operand.imm
                        if ins.group(capstone.CS_GRP_CALL) and target in targets:
                            calls.setdefault(target, []).append(ins.address)
                        for va, expected in sites.items():
                            if va == 0x421f3f:
                                continue  # immediate, not an entry instruction
                            assert not va < target < va + len(expected)//2, (hex(ins.address), hex(target))
                    elif ins.group(capstone.CS_GRP_CALL) and operand.type == x86.X86_OP_MEM:
                        if operand.mem.disp in slot_calls:
                            slot_calls[operand.mem.disp].append(ins.address)
    assert calls == {0x47b590: [0x47d69b, 0x47eccc]}, calls
    for slot, expected in {
        0x138: {0x448ea9, 0x46a3a6, 0x46e6af, 0x47ed6a, 0x4c90d4, 0x5d384d},
        0x13c: {0x469cb4},
    }.items():
        assert expected <= set(slot_calls[slot]), (slot, slot_calls[slot])
    # Placement remains native and consumes the synchronized stream separately.
    assert image[0x46be66-base:0x46be68-base] == b"\x6a\x01"
    assert 0x46be75 + struct.unpack_from("<i", image, 0x46be71-base)[0] == 0x560190
    assert image[0x46be70-base] == 0xe8
    # Category-six rewards branch around the generated-gear block independently.
    assert image[0x4c9084-base:0x4c9086-base] == b"\x0f\x84"
    assert 0x4c908a + struct.unpack_from("<i", image, 0x4c9086-base)[0] == 0x4c911c
    print("PASS: 13 retail guards; no branch into displaced instructions")
    print("T-CENSUS direct picker callers:", ", ".join(hex(v) for v in calls[0x47b590]))
    print("T-CENSUS manager-qualified vt138 callers: 448ea9 46a3a6 46e6af 47ed6a 4c90d4 5d384d; vt13c: 469cb4")
    print("PASS: DR category-six branch, placement lockstep draw, and regeneration ret4 boundary verified")


if __name__ == "__main__":
    main()
