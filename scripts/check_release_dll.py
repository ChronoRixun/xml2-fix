"""Fails unless a dinput.dll is the ONLINE build: GameNetworkingSockets, protobuf and the project CA pin linked into it.

The release job runs this on the DLL it is about to package (.github/workflows/build.yml). Without XML2_FIX_GNS_ROOT
CMake still produces a dinput.dll that loads, passes its tests and reports the same version - and has no online rooms or
drop-in, because every GNS call compiles out. Nothing else in the job would notice, so this does:

  * the file is a 32-bit PE DLL and (with --version) its ProductVersion/FileVersion is that version;
  * the GameNetworkingSockets library is in it (its ICE transport and protobuf package names), and the project CA pin
    from packaging/gns/ca-pin.txt is in it as text (the library's hard-coded root, and online_room.cpp's check of the
    broker's CA);
  * its imports are system DLLs only: no GameNetworkingSockets/protobuf/abseil/OpenSSL/libsodium DLL and no C++
    runtime DLL (vcruntime/msvcp/ucrt), which the game's PC would not have next to dinput.dll; and it imports bcrypt.dll
    (GNS's crypto);
  * the files that ship beside it (--ship, the folder of the release) name only dinput.dll as the DLL.

Standard library only:  python -I scripts/check_release_dll.py build/bin/Release/dinput.dll --version 1.8.0 \
                        --ca-pin packaging/gns/ca-pin.txt [--ship dist]
"""
import argparse
import re
import struct
import sys
from pathlib import Path
import importlib.util

# -I keeps scripts/ off sys.path. Load the same release policy as the workflow by absolute path.
_spec = importlib.util.spec_from_file_location('release_metadata', Path(__file__).with_name('release_metadata.py'))
_release = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_release)

# GameNetworkingSockets' own strings (not the fix's log lines, which a GNS-less build keeps): the P2P ICE transport class
# names and the protobuf package of its wire messages.
GNS_MARKERS = (b'CConnectionTransportP2PICE_Valve::Init', b'Valve.SteamNetworkingSockets')
# libraries that would have to travel with the DLL if the build had linked them dynamically
FORBIDDEN_IMPORTS = re.compile(r'^(gamenetworkingsockets|steamnetworkingsockets|libprotobuf|protobuf|abseil|absl_|libcrypto|libssl|'
                               r'libsodium|vcruntime|msvcp|msvcr|concrt|ucrtbase|api-ms-win-crt|libwinpthread|libgcc|libstdc)', re.I)
REQUIRED_IMPORTS = ('bcrypt.dll',)   # USE_CRYPTO=BCrypt: AES-GCM/SHA-256 come from Windows, so a GNS build always imports it
# a GNS-linked DLL is ~4.8 MB, the same DLL without it ~2.7 MB
MIN_SIZE = 3_500_000


def pe_imports(data: bytes) -> tuple[int, list[str]]:
    """(machine, imported DLL names incl. delay-load) of a PE image."""
    if data[:2] != b'MZ':
        raise ValueError('not a PE file (no MZ header)')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file (no PE signature)')
    machine, nsec, _, _, _, opt_size, characteristics = struct.unpack_from('<HHIIIHH', data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from('<H', data, opt)[0]
    directories = opt + (96 if magic == 0x10B else 112)
    sections = []
    for i in range(nsec):
        _, vsize, va, rsize, rptr = struct.unpack_from('<8sIIII', data, opt + opt_size + i * 40)
        sections.append((va, max(vsize, rsize), rptr))

    def offset(rva):
        for va, size, rptr in sections:
            if va <= rva < va + size:
                return rva - va + rptr
        raise ValueError(f'RVA {rva:#x} is in no section')

    def cstring(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii', 'replace')

    names = []
    rva, _ = struct.unpack_from('<II', data, directories + 1 * 8)       # import directory
    if rva:
        at = offset(rva)
        while True:
            fields = struct.unpack_from('<5I', data, at)
            if fields[3] == 0:
                break
            names.append(cstring(fields[3]))
            at += 20
    rva, _ = struct.unpack_from('<II', data, directories + 13 * 8)      # delay-load import directory
    if rva:
        at = offset(rva)
        while True:
            fields = struct.unpack_from('<8I', data, at)
            if fields[1] == 0:
                break
            names.append(cstring(fields[1]))
            at += 32
    if not characteristics & 0x2000:
        raise ValueError('not a DLL (IMAGE_FILE_DLL is not set)')
    return machine, names


def version_strings(data: bytes) -> dict[str, str]:
    """The string table of the version resource: {'ProductVersion': '1.8.0', ...} (first language block)."""
    out = {}
    for key in ('ProductVersion', 'FileVersion'):
        needle = key.encode('utf-16le') + b'\0\0'
        at = data.find(needle)
        if at < 0:
            continue
        at += len(needle)
        at = (at + 3) & ~3                                              # the value is DWORD-aligned after the key
        end = data.find(b'\0\0', at)
        while (end - at) % 2:
            end = data.find(b'\0\0', end + 1)
        out[key] = data[at:end].decode('utf-16le', 'replace')
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dll', type=Path)
    ap.add_argument('--version', help='release version without v; beta/rc suffixes compare to numeric DLL VERSIONINFO')
    ap.add_argument('--ca-pin', type=Path, help='packaging/gns/ca-pin.txt: its first line must be in the DLL as text')
    ap.add_argument('--ship', type=Path, help='folder of the release files: no DLL but dinput.dll may be in it')
    args = ap.parse_args()
    if args.version:
        try:
            args.version = _release.numeric_version(args.version)
        except ValueError as exc:
            ap.error(str(exc))

    problems = []
    data = args.dll.read_bytes()
    print(f'{args.dll}: {len(data):,} bytes')

    try:
        machine, imports = pe_imports(data)
    except (ValueError, struct.error) as exc:
        print(f'FAIL: {args.dll} is not a usable PE DLL: {exc}')
        return 1
    if machine != 0x14C:
        problems.append(f'machine type {machine:#x}: the game is 32-bit, build with -A Win32 (0x14c)')
    print('imports: ' + ', '.join(imports))

    if args.version:
        found = version_strings(data)
        print(f'version resource: {found}')
        for key in ('ProductVersion', 'FileVersion'):
            if found.get(key) != args.version:
                problems.append(f'{key} is {found.get(key)!r}, expected {args.version!r}')

    if len(data) < MIN_SIZE:
        problems.append(f'only {len(data):,} bytes: a GNS-linked dinput.dll is over {MIN_SIZE:,} (is XML2_FIX_GNS_ROOT set?)')
    for marker in GNS_MARKERS:
        if marker not in data:
            problems.append(f'GameNetworkingSockets is not linked in: the string {marker.decode()!r} is missing (configure with -DXML2_FIX_GNS_ROOT=...)')
    if args.ca_pin:
        pin = args.ca_pin.read_text(encoding='ascii').splitlines()[0].strip()
        if not pin.startswith('ssh-ed25519 '):
            problems.append(f'{args.ca_pin} does not start with an ssh-ed25519 key')
        elif pin.encode('ascii') not in data:
            problems.append(f'the project CA pin from {args.ca_pin} is not in the DLL (built without ca-pin.txt in XML2_FIX_GNS_ROOT?)')
    for name in imports:
        if FORBIDDEN_IMPORTS.search(name):
            problems.append(f'imports {name}: that DLL is not on the player\'s PC and would have to ship beside dinput.dll '
                            f'(the build must link GNS, protobuf, abseil and the C++ runtime statically, /MT)')
    for name in REQUIRED_IMPORTS:
        if name.lower() not in (n.lower() for n in imports):
            problems.append(f'does not import {name}: the GNS crypto layer is missing')

    if args.ship:
        shipped = sorted(p.name for p in args.ship.rglob('*.dll'))
        print('DLLs in the release folder: ' + (', '.join(shipped) or 'none'))
        extra = [n for n in shipped if n.lower() != 'dinput.dll']
        if extra:
            problems.append(f'the release folder carries DLLs the install does not account for: {extra}')
        if not any(n.lower() == 'dinput.dll' for n in shipped):
            problems.append('the release folder has no dinput.dll')

    if problems:
        print('\nFAIL: this dinput.dll is not the online build:')
        for p in problems:
            print('  - ' + p)
        return 1
    print('OK: GameNetworkingSockets, protobuf and the project CA pin are linked in; imports are system DLLs only')
    return 0


if __name__ == '__main__':
    sys.exit(main())
