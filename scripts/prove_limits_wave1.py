"""Limits wave 1 install proof in the synthetic retail-image host (tools/union_host; no game is launched).

The reference DLL (the previous release line) and this tree's DLL with the same OFF ini must give the same agreement
record and the same limits / model-pools log lines (bar the new read-only address-space line); with ModelPools=x4 alone
the same again; with [Limits] IGBCache=1024, ResourceNames=2048 and [Engine] ModelHandles=8192 this DLL must install all
three and add rule 31. Environment as scripts/README-gates.md: XML2_FIX_UNION (prepare_union.py folder, with run023),
XML2_FIX_LIMITS_REF (the reference build's folder: dinput.dll + dinput.map), XML2_FIX_TEST_EXE, XML2_FIX_UNION_HOST
and XML2_FIX_LIMITS_DLL (optional; default this tree's native-map build). Exports and restores the game's registry key around every host run and checks it is byte-identical.
"""
import ctypes, configparser, hashlib, os, re, shutil, subprocess, sys
from pathlib import Path
ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
R = Path(__file__).resolve().parent.parent
W = R
U = Path(os.environ['XML2_FIX_UNION'])
REF = Path(os.environ['XML2_FIX_LIMITS_REF'])
MINE = Path(os.environ.get('XML2_FIX_LIMITS_DLL') or R / 'native-map/bin/Release')  # this tree's /MAP build (dinput.dll + dinput.map)
HOST = Path(os.environ.get('XML2_FIX_UNION_HOST') or R / 'native-map/union/Release/union_host.exe')
RETAIL = os.environ['XML2_FIX_TEST_EXE']
KEY = r'HKCU\Software\Activision\X-Men Legends 2'
out = W / 'evidence/limits-wave1'
out.mkdir(parents=True, exist_ok=True)
(W / 'reg').mkdir(exist_ok=True)


LIMIT6 = ('ActorSlots', 'ResourceNames', 'ItemEnhancements', 'FightStyles', 'EffectCurves', 'MoveTriggers')


def run(tag, dlldir, extra, drop=()):
    folder = W / 'union' / tag
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    shutil.copytree(U / 'run/Data', folder / 'Data')
    shutil.copyfile(dlldir / 'dinput.dll', folder / 'dinput.dll')
    rows = (dlldir / 'dinput.map').read_text().splitlines()
    m = [l for l in rows if '?capabilities@' in l and '@gameplay_agreement@@3Vrecord@gameplay_capabilities@@A' in l]
    assert len(m) == 1, m
    rva = int(m[0].split()[-2], 16) - 0x10000000
    cfg = configparser.ConfigParser()
    cfg.optionxform = str
    cfg.read(U / 'run023/xml2-fix.ini')
    cfg.remove_section('TestMode')
    cfg['Game']['SaveFolder'] = 'X-Men Legends (limits-wave1-union)'
    for o in ('FastTransfer', 'Host', 'JoinCode', 'Broker', 'BrokerPin'):
        if cfg.has_option('Online', o):
            cfg.remove_option('Online', o)
    for key in drop:  # run023's non-stock [Limits] keys (rules 32-37 since b6a1c8c)
        if cfg.has_option('Limits', key):
            cfg.remove_option('Limits', key)
    if drop and cfg.has_option('Game', 'NpcPowerRule'):
        # rule 16's requested revision differs by release (11 in 1.8.0-beta.7, 14 in 1.9.0-beta.1) on purpose; without
        # the key it stays the released revision 3 OFF record in both (as prove_off_wire compares it)
        cfg.remove_option('Game', 'NpcPowerRule')
    if drop and cfg.has_option('Game', 'RevivalCost'):
        # rule 10 is revision 3 with the key from 1.8.0 (the first game's group price; beta 7 charged 200 * level^2 as
        # revision 2) on purpose; without it rule 10 stays the released revision 2 in both
        cfg.remove_option('Game', 'RevivalCost')
    for section, key, value in extra:
        if not cfg.has_section(section):
            cfg.add_section(section)
        cfg[section][key] = value
    with (folder / 'xml2-fix.ini').open('w') as h:
        cfg.write(h)
    before, after = W / f'reg/{tag}-before.reg', W / f'reg/{tag}-after.reg'
    subprocess.run(['reg', 'export', KEY, str(before), '/y'], check=True, capture_output=True)
    try:
        cmd = [HOST, folder / 'dinput.dll', RETAIL, U / 'engine', out / (tag + '.tsv'), hex(rva), out / (tag + '.bin')]
        done = subprocess.run(list(map(str, cmd)), cwd=folder, capture_output=True, text=True, timeout=180)
    finally:
        subprocess.run(['reg', 'import', str(before)], check=True, capture_output=True)
        subprocess.run(['reg', 'export', KEY, str(after), '/y'], check=True, capture_output=True)
        assert before.read_bytes() == after.read_bytes(), tag + ': registry changed'
    log = (folder / 'xml2-fix.log').read_text(errors='replace')
    raw = (out / (tag + '.bin')).read_bytes() if (out / (tag + '.bin')).exists() else b''
    return done.returncode, log, raw, folder


def lines(log, *words):
    return [l[l.find('limits') if 'limits' in l else 0:] for l in log.splitlines() if any(w in l for w in words)]


results = {}
rc_ref, log_ref, rec_ref, _ = run('off-beta7', REF, [])
rc_off, log_off, rec_off, _ = run('off-wave1', MINE, [])
rc_w1, log_w1, rec_w1, f_w1 = run('w1-wave1', MINE, [('Limits', 'IGBCache', '1024'), ('Limits', 'ResourceNames', '2048'), ('Engine', 'ModelPools', 'x4'), ('Engine', 'ModelHandles', '8192')])
rc_ref4, log_ref4, rec_ref4, _ = run('x4-beta7', REF, [('Engine', 'ModelPools', 'x4')], LIMIT6)
rc_x4, log_x4, rec_x4, _ = run('x4-wave1', MINE, [('Engine', 'ModelPools', 'x4')], LIMIT6)
# b6a1c8c registers every non-stock resource limit as its own rule (32-37), so run023's LC [Limits] (six non-stock
# limits) give a different record than beta 7 ON PURPOSE; the plain OFF claim is made without those six keys.
rc_refp, log_refp, rec_refp, _ = run('offplain-beta7', REF, [], LIMIT6)
rc_offp, log_offp, rec_offp, _ = run('offplain-wave1', MINE, [], LIMIT6)
ok = True
def check(cond, what):
    global ok
    ok &= bool(cond)
    print(('PASS ' if cond else 'FAIL ') + what)
check(rc_ref == rc_off == rc_w1 == rc_ref4 == rc_x4 == rc_refp == rc_offp == 0, f'host exit codes {rc_ref} {rc_off} {rc_w1} {rc_ref4} {rc_x4} {rc_refp} {rc_offp}')
check(len(rec_offp) == 108 and rec_offp == rec_refp, 'OFF (no non-stock limit keys): agreement record identical to beta 7 ' + hashlib.sha256(rec_offp).hexdigest()[:16])
count = lambda log: int((re.findall(r'protocol 4, (\d+) rules', log) or ['-1'])[-1])
check(len(rec_off) == 108 and rec_off != rec_ref and count(log_off) == count(log_ref) + 6,
      f'OFF with run023 [Limits]: record differs from beta 7 by the six resource-limit rules 32-37 ({count(log_ref)} -> {count(log_off)} rules)')
check(rec_x4 == rec_ref4 and len(rec_x4) == 108, 'x4 without ModelHandles (no non-stock limit keys): record identical to beta 7')
check(rec_w1 != rec_off and len(rec_w1) == 108, 'W1: record differs (rule 31 + rule 28 values)')
w = lambda log, *k: [l for l in log.splitlines() if all(x in l for x in k)]
check(w(log_w1, 'IGB cache raised from 200 to 1024 records'), 'W1: IGB cache raised')
check(w(log_w1, 'resource name table raised from 450 to 2048 names'), 'W1: names 2048')
check(w(log_w1, 'model pools: raised to x4', 'handles 8192, transforms 8192'), 'W1: model handles 8192')
check(w(log_w1, 'actor table raised from 40 to 127'), 'W1: actor table still raised')
check(not w(log_w1, 'IGB cache stays'), 'W1: no IGB refusal')
check(w(log_off, 'address space - LAA') and w(log_w1, 'address space - LAA'), 'LAA line present')
# OFF limits/model-pools lines equal to beta 7's (bar the new LAA line and the 200-IGB wording)
def norm(log):
    keep = [l.split(' ', 2)[-1] for l in log.splitlines() if ('limits:' in l or 'model pools' in l) and 'address space' not in l]
    keep = [k.replace(', 200 IGB records', '') for k in keep]
    # Limits wave 3 (w3/integrated), by design: W3-1 reports the style registry as the shared style manager
    # ("style manager styles N; events E ...") and DESIGN rev 2 section 7 raises the style manager, curves and items
    # after the actor cache and model pools. Same raises, same values: compare the style count and the set of lines.
    keep = [re.sub(r'^(?:fighting style registry raised from 19 to (\d+) entries|style manager styles (\d+); events 1064)\b.*$',
                   lambda m: 'STYLES ' + (m[1] or m[2]), k) for k in keep]
    return sorted(keep)
strip = lambda xs: sorted(re.sub(r'0x[0-9A-F]{8}', 'ADDR', x) for x in xs)
check(strip(norm(log_off)) == strip(norm(log_ref)), 'OFF: limits and model-pools log lines as beta 7')
cap_w1 = w(log_w1, 'capability') + w(log_w1, 'protocol 4')
cap_off = w(log_off, 'protocol 4')
print('OFF record line:', cap_off[:1])
print('W1 record line:', cap_w1[:2])
for l in w(log_w1, 'limits:') + w(log_w1, 'model pools'):
    print('  W1 |', l[:300])
for l in w(log_w1, 'startup transaction'):
    print('  W1 |', l[:300])
print('ALL PASS' if ok else 'FAILURES')
sys.exit(0 if ok else 1)
