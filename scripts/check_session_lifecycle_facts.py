"""0.6 preview fix J: the retail facts the session-lifecycle fixes rest on, checked against the retail XMen2.exe (and the
shipped msvcr71.dll when given). No game launch.

usage: python check_session_lifecycle_facts.py <retail XMen2.exe> [<msvcr71.dll>]

1. Every campaign message-handler handle the DLL scrubs (src/online_menus.cpp campaign_handles) is the result field of a
   retail 0x60edb0 registration of exactly that opcode (push <opcode>; call 0x60edb0; mov [esi+<offset>],eax), and every
   campaign registration site in 0x609660 / 0x609c80 / 0x609d80 is in the table.
2. 0x612c40 reloads its reason argument after its terminal clears (0x612e2b mov ebx,[ebp+0xc]) and tests session +0x44c
   before "lostconnectdialog" (0x612ec0, push 0x6a3968 at 0x612ee1).
3. The session game-info clear 0x615740 zeroes 0x44 bytes (the version at +0x3e included); the session shutdown calls it
   (0x612933) and only the struct constructor writes the version (0x61571c).
4. The discovery send's transport call 0x60da7f is call [edx+0xc]; the transport base vtable 0x6a4788 slot 3 is the
   _purecall import thunk; XMen2.exe imports _purecall from MSVCR71.dll (and msvcr71.dll exports _set_purecall_handler).
"""
import re, struct, sys
from pathlib import Path

exe = Path(sys.argv[1]).read_bytes()
pe = struct.unpack_from('<I', exe, 0x3c)[0]
base = struct.unpack_from('<I', exe, pe + 24 + 28)[0]
nsec = struct.unpack_from('<H', exe, pe + 6)[0]; opt = struct.unpack_from('<H', exe, pe + 20)[0]
secs = []
o = pe + 24 + opt
for _ in range(nsec):
    vsz, va, rsz, rp = struct.unpack_from('<IIII', exe, o + 8); secs.append((va + base, max(vsz, rsz), rp)); o += 40


def off(a):
    for va, sz, rp in secs:
        if va <= a < va + sz: return a - va + rp
    raise ValueError(hex(a))


def b(a, n): return exe[off(a):off(a) + n]
def u32(a): return struct.unpack('<I', b(a, 4))[0]
def call_target(site): return (site + 5 + struct.unpack('<i', b(site + 1, 4))[0]) & 0xffffffff


fails = []
def check(ok, what):
    print(('PASS ' if ok else 'FAIL ') + what)
    if not ok: fails.append(what)


src = (Path(__file__).resolve().parents[1] / 'src' / 'online_menus.cpp').read_text(encoding='utf-8')
table = src[src.index('campaign_handles[] = {'):]
table = table[:table.index('};')]
pairs = [(int(a, 16), int(c, 16)) for a, c in re.findall(r'\{0x([0-9a-f]+),0x([0-9a-f]+)\}', table)]
check(len(pairs) == 19, f'campaign_handles has 19 entries ({len(pairs)})')
# every campaign registration site in the three registration routines
sites = {}
for lo, hi in ((0x609660, 0x6096b7), (0x609c80, 0x609d7b), (0x609d80, 0x609f50)):
    code = b(lo, hi - lo)
    for i in range(len(code) - 11):
        if code[i] == 0xe8 and call_target(lo + i) == 0x60edb0 and code[i - 2] == 0x6a and code[i + 5:i + 7] == b'\x89\x86':
            sites[struct.unpack_from('<I', code, i + 7)[0]] = code[i - 1]
check(len(sites) == 19, f'19 campaign registration sites found ({len(sites)})')
for offset, opcode in pairs:
    check(sites.get(offset) == opcode, f'campaign +{offset:#x} holds the opcode {opcode:#04x} handle (retail: {sites.get(offset, -1):#x})')
check(set(sites) == {a for a, _ in pairs}, 'no campaign registration outside the table')

check(b(0x612e2b, 3) == bytes.fromhex('8b5d0c'), '0x612e2b reloads the reason argument [ebp+0xc] after the terminal clears')
check(b(0x612ec0, 6) == bytes.fromhex('8a864c040000'), '0x612ec0 tests session +0x44c before the takeover offer')
check(b(0x612ee1, 5) == bytes.fromhex('6868396a00') and b(0x6a3968, 18) == b'lostconnectdialog\0', '0x612ee1 queues "lostconnectdialog"')
check(b(0x612f89, 5)[0] == 0xe8 and call_target(0x612f89) == 0x612c40, 'the 0x2b handler returns to 0x612f8e from 0x612c40 (removal_notice_return)')
check(b(0x615740, 14) == bytes.fromhex('8bd15733c0b9110000008bfaf3ab'), '0x615740 zeroes the 0x44-byte game info (version +0x3e included)')
check(b(0x612933, 1) == b'\xe8' and call_target(0x612933) == 0x615740 and b(0x61292d - 0xc, 6) == bytes.fromhex('8d8ea4030000'),
      'the session shutdown clears session +0x3a4 (0x612921 lea / 0x612933 call 0x615740)')
check(b(0x61571c, 4) in (bytes.fromhex('c6403e31'), bytes.fromhex('8b15b049')), 'only the game-info constructor writes the version (0x61571c, retail or game_version)')
check(b(0x60da7f, 3) == bytes.fromhex('ff520c'), 'the discovery send calls transport vt+0xc (0x60da7f)')
thunk = u32(0x6a4788 + 12)
iat = u32(thunk + 2) if b(thunk, 2) == b'\xff\x25' else 0
names = []
imp = struct.unpack_from('<I', exe, pe + 24 + 104)[0]
d = off(imp + base)
while True:
    oft, _, _, name, ft = struct.unpack_from('<IIIII', exe, d)
    if not name: break
    dll = exe[off(name + base):off(name + base) + 32].split(b'\0')[0]
    i = 0
    while True:
        e = struct.unpack_from('<I', exe, off((oft or ft) + base) + 4 * i)[0]
        if not e: break
        if not e & 0x80000000:
            n = exe[off(e + base) + 2:off(e + base) + 40].split(b'\0')[0]
            names.append((dll.lower(), n, ft + base + 4 * i))
        i += 1
    d += 20
pure = [x for x in names if x[1] == b'_purecall']
check(bool(pure) and pure[0][0] == b'msvcr71.dll', 'XMen2.exe imports _purecall from MSVCR71.dll')
check(bool(pure) and iat == pure[0][2], f'the transport base vtable 0x6a4788 slot 3 is the _purecall thunk ({thunk:#x} -> [{iat:#x}])')
if len(sys.argv) > 2:
    crt = Path(sys.argv[2]).read_bytes()
    check(b'_set_purecall_handler\0' in crt, 'msvcr71.dll exports _set_purecall_handler')
# 060s follow-up N: the loaded query is shared with Team Management, and the
# Join tick owns a second popup independently of 0x612c40's reason argument.
check(call_target(0x5e68d9) == 0x60c350, 'shared Team/start loaded query calls 0x60c350')
check(call_target(0x60c369) == 0x614210, 'retail loaded query walks participant indices without a pending filter')
check(b(0x60c36e, 7) == bytes.fromhex('8b8c8794000000'), 'retail loaded query tests pause +0x94 +4*index')
check(call_target(0x5c8865) == 0x6111f0, 'Join tick checks its local participant at 0x5c8865')
check(b(0x5c8893, 5) == bytes.fromhex('6805110000'), 'Join missing-participant path selects separate dialog 0x1105')
check(b(0x5c88a0, 3) == bytes.fromhex('ff550c'), 'Join missing-participant path invokes the dialog vtable independently')
check(call_target(0x6133d8) == 0x612c40, 'native drop enters removal with return 0x6133dd (watchdog before tunnel-close callback)')
print('FAILED' if fails else 'ALL PASS', len(fails))
raise SystemExit(1 if fails else 0)
