"""Per-zone save-compatibility release gate: real saves must keep at least as many zone records on a candidate build as
on the baseline (the current stable release), zone by zone, and the candidate's resave must keep them.

    python scripts/zone_records_gate.py --tool <bin>/lcsave_zone_records.exe --list SAVES.tsv
        --baseline STABLE_BUILD --candidate CANDIDATE_BUILD --out DIR [--require-zone ZONE ...]

SAVES.tsv lists "<id>\\t<path>" lines (copies of real saves: LC campaigns and retail slots; other files are skipped).
Each build folder needs Data/ (the save tables and Data/lcsave/zone-prefix.lcm), Maps/ and
Data/lcsave/typed-entity-ids.txt (or pass --baseline-registry / --candidate-registry). The tool only reads the saves
and the builds; it writes DIR/<name>.jsonl, DIR/zone-gate.tsv, DIR/zone-gate.md and DIR/zone-gate.json.

Fails (exit 1) when, for either native layout (retail, E10A2):
  - a zone applies fewer saved records on the candidate than on the baseline (summed over the saves);
  - a save's zone record that the baseline applies is not applied by the candidate (per save, per zone);
  - a zone record the candidate's load applies is gone after the candidate's save of that load and a reload;
  - a save that loads on the baseline does not load on the candidate;
  - a --require-zone zone has no saved record that the candidate applies on either layout.
Generate the candidate's ledger first (legends-classic tools/lcsave/README.md: `python -m lcsave.zonecompat
generate --previous STABLE [--previous PREVIOUS_BETA] CANDIDATE --ledger-out ...`). Never launches the game.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

LAYOUTS = ('retail', 'e10a2')


def run_tool(tool, saves, build, registry, out):
    flags = 0x00004000 | 0x08000000 if sys.platform == 'win32' else 0  # BELOW_NORMAL, no console window
    completed = subprocess.run([str(Path(tool).resolve()), '--list', str(saves), '--data', str(build), '--registry', str(registry),
                                '--out', str(out)], capture_output=True, text=True, creationflags=flags)
    if completed.returncode != 0 or not out.exists():
        raise SystemExit(f'{tool} failed on {build} ({completed.returncode}): {completed.stdout[-2000:]}{completed.stderr[-2000:]}')
    return [json.loads(line) for line in out.read_text(encoding='utf-8').splitlines() if line.strip()]


def index(rows):
    """{id: row} of the campaigns (LC campaigns and imported retail slots)."""
    return {r['id']: r for r in rows if 'saved' in r or r.get('problem')}


def compare(base_rows, cand_rows, required=()):
    base, cand = index(base_rows), index(cand_rows)
    failures, zones = [], {}
    for layout in LAYOUTS:
        saved, applied_b, applied_c, resave_c = Counter(), Counter(), Counter(), Counter()
        for fid in sorted(set(base) | set(cand)):
            b, c = base.get(fid, {}), cand.get(fid, {})
            lb, lc = b.get(layout) or {}, c.get(layout) or {}
            if lb.get('load') and not lc.get('load'):
                failures.append(f'{layout} {fid}: loads on the baseline, not on the candidate: {lc.get("error") or c.get("problem")}')
            for z in c.get('saved', b.get('saved', [])):
                saved[z] += 1
            ab, ac = Counter(lb.get('applied', [])), Counter(lc.get('applied', []))
            rc = Counter(lc.get('resave_applied', []))
            applied_b.update(ab); applied_c.update(ac); resave_c.update(rc)
            for z, n in ab.items():
                if ac[z] < n:
                    failures.append(f'{layout} {fid}: zone {z}: applied on the baseline, not on the candidate '
                                    f'({"; ".join(x for x in lc.get("notes", []) if z in x) or "no note"})')
            if lc.get('load') and 'resave_error' in lc:
                failures.append(f'{layout} {fid}: candidate resave: {lc["resave_error"]}')
            for z, n in ac.items():
                if lc.get('load') and 'resave_error' not in lc and rc[z] < n:
                    failures.append(f'{layout} {fid}: zone {z}: applied on the candidate load, gone after its resave')
        for z in sorted(set(saved) | set(applied_b) | set(applied_c)):
            zones.setdefault(z, {})[layout] = dict(saved=saved[z], baseline=applied_b[z], candidate=applied_c[z],
                                                   candidate_resave=resave_c[z])
            if applied_c[z] < applied_b[z]:
                failures.append(f'{layout} zone {z}: candidate applies {applied_c[z]} records, baseline {applied_b[z]}')
    for z in required:
        if not any((zones.get(z, {}).get(layout) or {}).get('candidate') for layout in LAYOUTS):
            failures.append(f'required zone {z}: no layout applies a saved record of it on the candidate '
                            f'({zones.get(z)}; the gate needs saves that exercise it)')
    return zones, failures


def table(zones):
    head = ['zone'] + [f'{layout} {k}' for layout in LAYOUTS for k in ('saved', 'baseline', 'candidate', 'resave')]
    rows = [head]
    for z, v in sorted(zones.items()):
        rows.append([z] + [str((v.get(layout) or {}).get(k, 0)) for layout in LAYOUTS
                           for k in ('saved', 'baseline', 'candidate', 'candidate_resave')])
    total = ['TOTAL'] + [str(sum((v.get(layout) or {}).get(k, 0) for v in zones.values())) for layout in LAYOUTS
                         for k in ('saved', 'baseline', 'candidate', 'candidate_resave')]
    rows.append(total)
    tsv = ''.join('\t'.join(r) + '\n' for r in rows)
    md = '| ' + ' | '.join(head) + ' |\n|' + '---|' * len(head) + '\n' + ''.join('| ' + ' | '.join(r) + ' |\n' for r in rows[1:])
    return tsv, md


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tool', required=True, help='lcsave_zone_records.exe (the CMake target of the same name)')
    ap.add_argument('--list', required=True, help='"<id>\\t<path>" lines of real-save copies')
    ap.add_argument('--baseline', required=True, help='the stable release build (Data/, Maps/)')
    ap.add_argument('--candidate', required=True, help='the candidate build, with its generated zone-prefix ledger')
    ap.add_argument('--baseline-registry'); ap.add_argument('--candidate-registry')
    ap.add_argument('--out', required=True, help='output folder (created)')
    ap.add_argument('--require-zone', action='append', default=[], help='a zone the saves must exercise: at least one of its records applies on the candidate')
    args = ap.parse_args(argv)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    reg = lambda build, given: Path(given) if given else Path(build) / 'Data/lcsave/typed-entity-ids.txt'
    base = run_tool(args.tool, args.list, args.baseline, reg(args.baseline, args.baseline_registry), out / 'baseline.jsonl')
    cand = run_tool(args.tool, args.list, args.candidate, reg(args.candidate, args.candidate_registry), out / 'candidate.jsonl')
    zones, failures = compare(base, cand, args.require_zone)
    tsv, md = table(zones)
    (out / 'zone-gate.tsv').write_text(tsv, encoding='utf-8', newline='\n')
    (out / 'zone-gate.md').write_text(md, encoding='utf-8', newline='\n')
    problems = sorted({f'{r["id"]}: {r["problem"]}' for r in base + cand if r.get('problem')})
    summary = dict(baseline=str(args.baseline), candidate=str(args.candidate), campaigns=len(index(cand)),
                   zones=zones, failures=failures, file_problems=problems, passed=not failures)
    (out / 'zone-gate.json').write_text(json.dumps(summary, indent=1) + '\n', encoding='utf-8', newline='\n')
    print(md)
    for p in problems:
        print('file problem (both builds alike unless listed as a failure):', p)
    for f in failures:
        print('FAIL', f)
    print('zone records gate:', 'PASS' if not failures else f'FAIL ({len(failures)})')
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
