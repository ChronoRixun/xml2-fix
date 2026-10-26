"""Offline tag/version/notes policy shared by the release workflow and its tests."""
import argparse
import json
import re
from pathlib import Path

NUMBER = r'(?:0|[1-9][0-9]*)'
TAG = re.compile(rf'v(?P<base>{NUMBER}\.{NUMBER}\.{NUMBER})(?:-(?:beta|rc)\.{NUMBER})?')
NOTES = Path(__file__).resolve().parents[1] / 'packaging' / 'release-notes'


def metadata(tag: str) -> dict:
    match = TAG.fullmatch(tag)
    if not match:
        raise ValueError(f'invalid release tag {tag!r}: expected vX.Y.Z[-beta.N|-rc.N]')
    pre = '-' in tag
    return dict(tag=tag, version=tag[1:], base=match['base'], prerelease=pre,
                flags=['--prerelease', '--latest=false'] if pre else [])


def numeric_version(version: str) -> str:
    """The DLL VERSIONINFO is numeric; the release tag carries the beta/rc identity."""
    return metadata('v' + version)['base']


def check_version(tag: str, built: str) -> None:
    base = metadata(tag)['base']
    if base != built:
        raise ValueError(f'tag {tag}, but dinput.dll is version {built}: update project(... VERSION) in CMakeLists.txt')


def release_notes(tag: str, folder: Path = NOTES) -> str | None:
    meta = metadata(tag)
    path = folder / f'v{meta["base"]}.md'
    if not path.is_file() and not meta['prerelease']:
        return None  # preserve the stable workflow's --generate-notes fallback
    text = path.read_text(encoding='utf-8')
    return ('Beta - test build\n\n' if meta['prerelease'] else '') + text


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tag', required=True)
    ap.add_argument('--built-version')
    ap.add_argument('--notes-output', type=Path)
    args = ap.parse_args()
    try:
        meta = metadata(args.tag)
        if args.built_version is not None:
            check_version(args.tag, args.built_version)
        if args.notes_output:
            notes = release_notes(args.tag)
            meta['notes_file'] = str(args.notes_output) if notes is not None else None
            if notes is not None:
                args.notes_output.write_text(notes, encoding='utf-8', newline='\n')
    except (ValueError, OSError) as exc:
        ap.error(str(exc))
    print(json.dumps(meta))


if __name__ == '__main__':
    main()
