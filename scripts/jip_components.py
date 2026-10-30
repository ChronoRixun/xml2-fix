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


SNAPSHOT = re.compile(r'jip snapshot: where=(?P<where>[^ ]+(?: [^ ]+)*?) (?P<fields>(?:[a-z\[\]0-9_]+[:=]\S+ ?)+)$')
RNG = re.compile(r'jip rng: phase=(?P<phase>\d+) frame=(?P<frame>\d+) count=(?P<count>\d+) caller=(?P<caller>[0-9a-f]{8})')


def parse_snapshots(path):
    out = {}
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        if 'jip snapshot: where=' not in line:
            continue
        body = line.split('jip snapshot: ', 1)[1]
        where, _, rest = body.partition(' frame=') if ' frame=' in body else body.partition(' hero[')
        where = where[len('where='):]
        key = where if 'hero[' not in body or ' frame=' in body else where + ' ' + rest.split('=')[0]
        fields = {}
        last = ''
        for tok in (('frame=' + rest) if ' frame=' in body else rest).split():
            if '=' in tok:
                k, v = tok.split('=', 1); fields[k] = v; last = k
            elif ':' in tok and last:
                k, v = tok.split(':', 1); fields[last + '.' + k] = v
        out.setdefault(key, []).append(fields)
    return out


def snapshot_mode(host, guest):
    h, g = parse_snapshots(host), parse_snapshots(guest)
    bad = 0
    for key in [k for k in h if k in g]:
        for i, (a, b) in enumerate(zip(h[key], g[key])):
            diff = {k: (a.get(k), b.get(k)) for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)}
            # the clock's raw now/pause and the paused flag differ harmlessly (play-time accounting only)
            harmless = {k: v for k, v in diff.items() if k.startswith('clock') or k == 'session'}
            real = {k: v for k, v in diff.items() if k not in harmless}
            print(f'{key} [{i}]: ' + ('AGREE' if not real else 'DIFFERS ' + ', '.join(f'{k} host {v[0]} guest {v[1]}' for k, v in real.items())) + (f' (harmless: {", ".join(harmless)})' if harmless else ''))
            bad += bool(real)
    print(f'{bad} differing snapshot(s)')
    return 1 if bad else 0


def parse_rng(path):
    out = {}
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = RNG.search(line)
        if m:
            out[(int(m['phase']), int(m['count']))] = (int(m['frame']), m['caller'])
    return out


def rng_mode(host, guest):
    h, g = parse_rng(host), parse_rng(guest)
    phases = sorted({k[0] for k in h} | {k[0] for k in g})
    first = None
    for ph in phases:
        hk = sorted(k for k in h if k[0] == ph); gk = sorted(k for k in g if k[0] == ph)
        print(f'phase {ph}: host {len(hk)} draw(s), guest {len(gk)} draw(s)')
        for key in sorted(set(hk) | set(gk)):
            a, b = h.get(key), g.get(key)
            if a is None or b is None or a[1] != b[1]:
                first = first or (key, a, b)
                print(f'  first difference at phase {key[0]} count {key[1]}: host {a} guest {b}')
                break
    if first is None:
        print('every synchronized draw agrees (phase, count, caller)')
    return 1 if first else 0


SPAWN = re.compile(r'jip spawn: phase=(?P<phase>\d+) frame=(?P<frame>\d+) n=(?P<n>\d+) caller=(?P<caller>[0-9a-f]{8}) def=(?P<definition>\S*) inst=(?P<instance>\S*) at=(?P<at>\S+) draws=(?P<draws>\d+)')


def parse_spawns(path):
    out = {}
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = SPAWN.search(line)
        if m:
            out.setdefault(int(m['phase']), []).append((m['caller'], m['definition'], m['instance'], m['at'], int(m['frame']), int(m['draws'])))
    return out


def spawn_mode(host, guest):
    """Follow-up 5: every entity the zone spawned (0x4612e0), paired per phase in order; the extras are named."""
    h, g = parse_spawns(host), parse_spawns(guest)
    bad = 0
    for ph in sorted(set(h) | set(g)):
        a, b = h.get(ph, []), g.get(ph, [])
        print(f'phase {ph}: host {len(a)} spawn(s), guest {len(b)} spawn(s)')
        for i, (x, y) in enumerate(zip(a, b)):
            if x[:4] != y[:4]:
                print(f'  first difference at index {i}: host {x} guest {y}')
                bad += 1
                break
        for who, extra in (('host', a[len(b):]), ('guest', b[len(a):])):
            for e in extra:
                print(f'  only on the {who}: caller {e[0]} def {e[1]} inst {e[2]} at {e[3]} (frame {e[4]}, draws {e[5]})')
                bad += 1
    if not bad:
        print('every spawn agrees (phase, order, caller, names, position)')
    return 1 if bad else 0


if __name__ == '__main__' and '--mode' in sys.argv:
    mode = sys.argv[sys.argv.index('--mode') + 1]
    sys.exit({'snapshot': snapshot_mode, 'rng': rng_mode, 'spawn': spawn_mode}[mode](sys.argv[1], sys.argv[2]))

if __name__ == '__main__':
    sys.exit(main(sys.argv))
