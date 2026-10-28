"""Run the production fixture with optional base-only sidecars, no game process.

python -B scripts/test_anim_enums.py <test-exe> <retail-exe>
All temporary files are made beside the owned test executable, then removed by
absolute path. Existing sidecars are refused rather than overwritten.
"""
from pathlib import Path
import os, re, struct, subprocess, sys

exe=Path(sys.argv[1]).resolve()
retail=Path(sys.argv[2]).resolve()
assert exe.is_relative_to(Path(__file__).resolve().parents[1]) and exe.name=='xml2_anim_enums_test.exe'
root=Path(__file__).resolve().parents[1]
dll=exe.with_name('dinput.dll').read_bytes()
magic=b'XML2FIX.ANIMENUMS.1\0'
at=dll.index(magic)+len(magic)
assert struct.unpack_from('<H',dll,at)[0]==23
expected=re.findall(r'"(ea_[a-z0-9_]+)"',(root/'src/anim_enum_names.hpp').read_text().split('builtin_names{{',1)[1].split('}};',1)[0])
assert dll[at+2:].split(b'\0',23)[:23]==[n.encode('ascii') for n in expected]
assert 'AnimEnums'.encode('utf-16le') in dll
print('PASS built DLL marker, count, fixed-ID names and UTF16 key')
sidecar=exe.parent/'Data/anim_enums.txt'
assert not sidecar.exists(), f'refusing existing sidecar: {sidecar}'
sidecar.parent.mkdir(exist_ok=True)
cases=[('',None),('bad-sidecar',b'ea_idle1\n'),('bad-sidecar',b'ea_x\nea_x\n'),
       ('bad-sidecar',b'e\n'),('bad-sidecar',b'ea\n'),('bad-sidecar',b'\xff\n'),
       ('bad-sidecar',b'ea_bad-name\n'),('bad-sidecar',b'ea_x\0hidden\n'),
       ('sidecar',('ea_mod_first\nea_'+60*'a'+'\n').encode()),
       ('sidecar-full',''.join(f'ea_mod_{i}\n' for i in range(279)).encode()),
       ('retain',b'')]
try:
    for mode,data in cases:
        if data is not None:sidecar.write_bytes(data)
        p=subprocess.run([str(exe),str(retail)]+([mode] if mode else []),capture_output=True,text=True,timeout=60,creationflags=0x08004000,cwd=exe.parent)
        print(mode or 'base-absent',p.returncode,p.stdout.strip(),flush=True)
        assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
finally:
    if sidecar.exists():os.remove(str(sidecar))
print('PASS native production fixtures: base absent, invalid sidecars, base sidecar, case and 63/64 truncation')
