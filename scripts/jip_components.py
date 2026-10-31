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
RNG = re.compile(r'jip rng: phase=(?P<phase>\d+) frame=(?P<frame>\d+) count=(?P<count>\d+) caller=(?P<caller>[0-9a-f]{8})(?: actor=(?P<actor>[0-9a-f]{8})/(?P<ctl>[0-9a-f]{8}) think=(?P<think>[0-9a-f]{8}) dist=(?P<dist>\d+))?')
EVENTS = re.compile(r'jip events: where=(?P<where>.+?) heap=(?P<heap>\d+) records=(?P<records>\d+) ring=(?P<ring>\S+) live=(?P<live>\d+) dead=(?P<dead>\d+) stale=(?P<stale>\d+) rebuilds=(?P<rebuilds>\d+)(?: part=(?P<part>\d+)/(?P<parts>\d+))? head=(?P<head>\S*)')
CAMDUMP = re.compile(r'jip camdump: frame=(?P<frame>\d+) off=(?P<off>[0-9a-f]{3})(?P<words>(?: [0-9a-f]{8})+)')


def actor_index(actor):
    """A handle's dense index (the generation bits differ per process: a warm host's are higher for the same slot)."""
    return int(actor, 16) & 0x3ff if actor else None




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
    """Follow-up 7: field-level entries (caller, the actor's dense index, controller, think time, distance) as rngcmp.py
    compared them in round 4; the caller alone missed the earlier same-caller / different-actor draws."""
    out = {}
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = RNG.search(line)
        if m:
            out[(int(m['phase']), int(m['count']))] = (int(m['frame']), m['caller'], actor_index(m['actor']), m['ctl'], m['think'], m['dist'], m['actor'])
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
            if a is None or b is None or a[:6] != b[:6]:
                first = first or (key, a, b)
                print(f'  first difference at phase {key[0]} count {key[1]} (frame, caller, actor index, controller, think, dist, handle): host {a} guest {b}')
                break
    if first is None:
        print('every synchronized draw agrees (phase, count, caller, actor index, controller, think, dist)')
    return 1 if first else 0


def events_mode(host, guest):
    """Follow-up 7/9: the timed-event queue lines per milestone (heap/records/ring/live/dead/stale and every heap pair as
    time:index:handle:kind[:payload], several `part=k/n` lines per milestone since AE10); the handle's generation is
    masked before comparing; the kind is the event's offset inside its owner, the static kind object's address or (AE15,
    a heap kind) its vtable - the same in every process. Entries present on one peer only are listed by (time, slot,
    kind); the payload is never compared (follow-up 14, review S7: per-process pointers / stale residue)."""
    def parse(path):
        out, seen_parts = {}, {}
        for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
            m = EVENTS.search(line)
            if not m:
                continue
            where, part = m['where'], int(m['part'] or 1)
            if where in out and part <= seen_parts.get(where, 0):
                continue  # a later epoch's repeat of the same milestone: keep the first
            if where in out and part != seen_parts[where] + 1:
                continue
            head = []
            for item in m['head'].split(',') if m['head'] else []:
                fields = item.split(':')
                t, index, handle, kind = fields[0], int(fields[1]), int(fields[2], 16), fields[3]
                payload = fields[4] if len(fields) > 4 else ''
                head.append((t, handle & 0x3ff if handle else 0, kind, payload, index))
            if where in out:
                out[where][7].extend(head)
            else:
                out[where] = [int(m['heap']), int(m['records']), m['ring'], int(m['live']), int(m['dead']), int(m['stale']), int(m['rebuilds']), head]
            seen_parts[where] = part
        return out
    h, g = parse(host), parse(guest)
    bad = 0
    for where in [w for w in h if w in g]:
        a, b = h[where], g[where]
        # Follow-up 14 (review S7): the 16 payload bytes carry per-process pointers for several kinds (round 10 frame 900:
        # one pointer byte apart on an otherwise identical entry) and are stale residue for the "moved" kind, so an entry
        # is compared by (time, slot, kind) only; the payload is printed for reference on the one-sided entries.
        pa, pb = {e[:3]: e[3] for e in a[7]}, {e[:3]: e[3] for e in b[7]}
        ka, kb = [e[:3] for e in a[7]], [e[:3] for e in b[7]]
        same = a[0] == b[0] and a[3] == b[3] and ka == kb
        verdict = 'AGREE  ' if same else 'DIFFERS'
        print(f"{verdict} {where}: host heap {a[0]} records {a[1]} ring {a[2]} live {a[3]} dead {a[4]} stale {a[5]} rebuilds {a[6]} | guest heap {b[0]} records {b[1]} ring {b[2]} live {b[3]} dead {b[4]} stale {b[5]} rebuilds {b[6]}")
        if not same:
            bad += 1
            for i, (x, y) in enumerate(zip(ka, kb)):
                if x != y:
                    print(f'  first differing heap pair at {i}: host {x} guest {y}')
                    break
            from collections import Counter
            ca, cb = Counter(ka), Counter(kb)
            for who, extra, payloads in (('host', ca - cb, pa), ('guest', cb - ca, pb)):
                for key, n in sorted(extra.items()):
                    print(f'  only on the {who} ({n}x): time {key[0]} slot {key[1]:#x} kind {key[2]} payload {payloads.get(key) or "-"} (payload not compared)')
    for who, only in (('host', [w for w in h if w not in g]), ('guest', [w for w in g if w not in h])):
        for w in only:
            print(f'  only on the {who}: {w}')
    return 1 if bad else 0


def camdump_mode(host, guest):
    """Follow-up 7: the camera object's dwords at the frame-0 epoch and frames 1 and 2; the first differing dword
    per frame is named by its offset."""
    def parse(path):
        out = {}
        for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
            m = CAMDUMP.search(line)
            if m:
                words = m['words'].split()
                frame = int(m['frame'])
                if frame not in out or int(m['off'], 16) not in out[frame]:
                    out.setdefault(frame, {})[int(m['off'], 16)] = words
        return out
    h, g = parse(host), parse(guest)
    bad = 0
    for frame in sorted(set(h) & set(g)):
        diffs = []
        for off in sorted(set(h[frame]) & set(g[frame])):
            for k, (x, y) in enumerate(zip(h[frame][off], g[frame][off])):
                if x != y:
                    diffs.append((off + 4 * k, x, y))
        first = f"; first at +{diffs[0][0]:#05x}: host {diffs[0][1]} guest {diffs[0][2]}" if diffs else ''
        print(f"frame {frame}: {len(diffs)} differing dword(s){first}")
        for off, x, y in diffs[:12]:
            print(f'  +{off:#05x}: host {x} guest {y}')
        bad += bool(diffs)
    return 1 if bad else 0


SPAWN = re.compile(r'jip spawn: phase=(?P<phase>\d+) frame=(?P<frame>\d+) n=(?P<n>\d+) caller=(?P<caller>[0-9a-f]{8}) def=(?P<definition>\S*) inst=(?P<instance>\S*) at=(?P<at>\S+) draws=(?P<draws>\d+)')


def parse_spawns(path):
    out = {}
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = SPAWN.search(line)
        if m:
            out.setdefault(int(m['phase']), []).append((m['caller'], m['definition'], m['instance'], m['at'], int(m['frame']), int(m['draws'])))
    return out


def normalise_caller(caller):
    """A caller inside the fix DLL (per-process load base; the dormant respawn's own call) is 'dll', not a game address."""
    return caller if 0x400000 <= int(caller, 16) < 0x700000 else 'dll'


CAM = re.compile(r'jip cam: frame=(?P<frame>\d+) count=(?P<count>\d+) (?:t=\S+ )?(?P<rest>yaw=\S+ hist=\S+ mode=\S+ flags=\S+ eye=\S+ target=\S+) table=(?P<table>\S+)(?: fpu=(?P<fpu>\S+))?')


def cam_mode(host, guest):
    """Follow-up 6: the per-frame camera lines 1..60, paired by frame; the first frame whose camera differs is named."""
    def parse(path):
        out = {}
        for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
            m = CAM.search(line)
            if m and int(m['frame']) not in out:
                out[int(m['frame'])] = (int(m['count']), m['rest'], m['table'], m['fpu'])
        return out
    h, g = parse(host), parse(guest)
    frames = sorted(set(h) & set(g))
    print(f'camera lines: host {len(h)} frame(s), guest {len(g)} frame(s), {len(frames)} paired')
    if frames and h[frames[0]][2] != g[frames[0]][2]:
        print(f'  controller hero table order differs (local seats): host {h[frames[0]][2]} guest {g[frames[0]][2]}')
    if frames and h[frames[0]][3] != g[frames[0]][3]:
        print(f'  FPU control state differs (x87/mxcsr): host {h[frames[0]][3]} guest {g[frames[0]][3]}')
    for f in frames:
        if h[f][1] != g[f][1] or h[f][0] != g[f][0]:
            print(f'  first difference at frame {f}: host count {h[f][0]} {h[f][1]} / guest count {g[f][0]} {g[f][1]}')
            return 1
    print('every paired camera line agrees')
    return 0


def spawn_mode(host, guest):
    """Follow-up 5: every entity the zone spawned (0x4612e0), paired per phase in order; the extras are named.
    Follow-up 6: a caller inside the fix DLL is normalised to 'dll' (its load base differs per process)."""
    h, g = parse_spawns(host), parse_spawns(guest)
    h = {k: [(normalise_caller(e[0]),) + e[1:] for e in v] for k, v in h.items()}
    g = {k: [(normalise_caller(e[0]),) + e[1:] for e in v] for k, v in g.items()}
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
    sys.exit({'snapshot': snapshot_mode, 'rng': rng_mode, 'spawn': spawn_mode, 'cam': cam_mode, 'events': events_mode, 'camdump': camdump_mode}[mode](sys.argv[1], sys.argv[2]))

if __name__ == '__main__':
    sys.exit(main(sys.argv))
