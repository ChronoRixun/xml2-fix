"""First-shared-frame agreement across roles for a late-join cohort restart (p060fix/AE).

Reads two xml2-fix logs (the host's and a joiner's), pairs their `jip checksum produced` samples by epoch and native
frame, and reports every component the retail checksum sums (hero transforms per slot, hero subtotal, camera subtotal,
synchronized RNG draw count) plus the restart lines the fix logs (camera restore, first-frame RNG reset). Exit 0 when
every paired period agrees on every component, 1 otherwise, 2 when nothing could be paired.

usage: python jip_components.py HOST.log GUEST.log [--epoch N] [--periods 4]
"""
import re
import sys
from pathlib import Path

PRODUCED = re.compile(r'\[(?P<time>[^\]]+)\] jip checksum produced: epoch=(?P<epoch>\d+) sample=(?P<sample>\d+) frame=(?P<frame>\d+) '
                      r'hash=0x(?P<hash>[0-9A-Fa-f]{8}) heroes=(?P<heroes>\d+) complete=(?P<complete>\d) hero_sum=0x(?P<hero_sum>[0-9A-Fa-f]{8}) '
                      r'camera=0x(?P<camera>[0-9A-Fa-f]{8}) rng_draws=(?P<rng>\d+) seed=0x(?P<seed>[0-9A-Fa-f]{8})')
HERO = re.compile(r'jip checksum hero: epoch=(?P<epoch>\d+) frame=(?P<frame>\d+) slot=(?P<slot>\d+) .*?contribution=0x(?P<contribution>[0-9A-Fa-f]{8}) '
                  r'controller=(?P<controller>-?\d+) ai=0x(?P<ai>[0-9A-Fa-f]{8}) transform20\.\.34=(?P<transform>[0-9A-Fa-f,]+)')
RESTART = ('canonical restart', 'first shared frame', 'pristine image', 'cohort synchronized', 'restore mode', 'drop notice', 'checksum head')


def parse(path):
    samples = {}
    heroes = {}
    restart = []
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = HERO.search(line)
        if m:
            key = (int(m['epoch']), int(m['frame']))
            heroes.setdefault(key, {})[int(m['slot'])] = (m['transform'].upper(), m['contribution'].upper(), int(m['controller']), m['ai'].upper())
            continue
        m = PRODUCED.search(line)
        if m:
            key = (int(m['epoch']), int(m['frame']))
            samples[key] = dict(time=m['time'], hash=int(m['hash'], 16), heroes=int(m['heroes']), complete=m['complete'] == '1',
                                hero_sum=int(m['hero_sum'], 16), camera=int(m['camera'], 16), rng=int(m['rng']), seed=m['seed'].upper(),
                                slots=heroes.get(key, {}))
            continue
        if 'join in progress' in line and any(k in line for k in RESTART) or 'native drop notice' in line or 'jip checksum head' in line:
            restart.append(line.strip())
    return samples, restart


def main(argv):
    if len(argv) < 3:
        print(__doc__); return 2
    host, guest = parse(argv[1])[0], parse(argv[2])[0]
    host_lines, guest_lines = parse(argv[1])[1], parse(argv[2])[1]
    epoch = None
    periods = 4
    if '--epoch' in argv: epoch = int(argv[argv.index('--epoch') + 1])
    if '--periods' in argv: periods = int(argv[argv.index('--periods') + 1])
    keys = sorted(k for k in host if k in guest and (epoch is None or k[0] == epoch))[:periods]
    if not keys:
        print('no paired samples (host epochs/frames: %s; guest: %s)' % (sorted(host), sorted(guest)))
        return 2
    print('restart lines (host):'); [print('  ' + l) for l in host_lines]
    print('restart lines (guest):'); [print('  ' + l) for l in guest_lines]
    bad = 0
    for k in keys:
        h, g = host[k], guest[k]
        ok = True
        print(f'epoch {k[0]} frame {k[1]}: host hash {h["hash"]:08X} guest {g["hash"]:08X}')
        for name in ('hero_sum', 'camera', 'rng'):
            d = g[name] - h[name]
            flag = 'OK' if d == 0 else 'DIFFERS'
            if d: ok = False
            print(f'  {name:9s} host {h[name]:>10} guest {g[name]:>10} delta {d:+d} {flag}')
        for slot in sorted(set(h['slots']) | set(g['slots'])):
            hs, gs = h['slots'].get(slot), g['slots'].get(slot)
            same = hs is not None and gs is not None and hs[0] == gs[0]
            if not same: ok = False
            print(f'  slot {slot}: host {hs} guest {gs} {"OK" if same else "DIFFERS"}')
        for name, s in (('host', h), ('guest', g)):
            if (s['hero_sum'] + s['camera'] + s['rng']) & 0xffffffff != s['hash']:
                print(f'  {name} sample does not reconcile (hero_sum + camera + rng != hash): parse or producer change')
                ok = False
        print('  ' + ('AGREE' if ok else 'DISAGREE'))
        bad += not ok
    print(f'{len(keys)} period(s) compared, {bad} disagree')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
