"""Read owned PC medium-font metrics; write only the requested JSON report. No game or network."""
from pathlib import Path
import argparse
import hashlib
import json
import struct


def xmlb(data):
    assert struct.unpack_from('<II', data) == (0x11b1, 1)
    def text(at):
        return data[at:data.index(b'\0', at)].decode('latin-1')
    def node(at):
        name, sibling, child, count = struct.unpack_from('<IIII', data, at)
        attributes = {text(k): text(v) for k, v in
                      (struct.unpack_from('<II', data, at + 16 + i * 8) for i in range(count))}
        children = []
        while child != 0xffffffff:
            value, child = node(child)
            children.append(value)
        return (text(name), attributes, children), sibling
    return node(8)[0]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--fonts', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    messages = {'MSAA: unknown', 'MSAA: unavailable', 'MSAA applied: Off', 'MSAA: Off (capture)'}
    messages.update(f'MSAA: Off (requested {n}x)' for n in (2, 4, 6, 8))
    messages.update(f'MSAA applied: {n}x' for n in range(2, 17))
    messages.update(f'MSAA: {n}x (Remastered)' for n in range(2, 17))
    files = {p.name.lower(): p for p in args.fonts.iterdir() if p.is_file()}
    config = files['fonts_pc.xmlb']
    rows = []
    for _, entry, _ in xmlb(config.read_bytes())[2]:
        if entry.get('index') != 'medium':
            continue
        path = files[entry['data'].lower() + '.xmlb']
        table = xmlb(path.read_bytes())
        glyphs = {int(g['num']): g for _, g, _ in table[2]}
        for message in sorted(messages):
            pen = 0.0
            ink_right = 0.0
            for c in message:
                glyph = glyphs[ord(c)]
                advance = float(glyph['horizadvance'])
                assert advance > 0, (path, c)
                ink_right = max(ink_right, pen + float(glyph.get('horizoffset', 0)) + float(glyph.get('width', 0)))
                pen += advance
            # Native small BXIG text uses the 0.8 scale at X 0x617973 (0x684bb0).
            # This checks logical 640x480 fit, not live language/renderer/DPI coverage.
            width = max(pen, ink_right) * 0.8
            assert len(message) <= 25 and width <= 196, (path, message, width)
            rows.append(dict(file=path.name, language=entry.get('language', '*'), text=message,
                             logical_width=round(width, 3), available=196,
                             sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    assert rows
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(dict(rows=rows, live_verified=False), indent=2) + '\n', encoding='utf-8')
    print(f'PASS {len(rows)} actual glyph-metric fits; max {max(r["logical_width"] for r in rows):.3f}/196; live coverage pending')


if __name__ == '__main__':
    main()
