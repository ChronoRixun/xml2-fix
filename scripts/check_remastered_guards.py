"""Read-only A/B evidence checks against the owned executable and Gfx DLL. No game starts."""
from pathlib import Path
import argparse
import hashlib
import json
import pefile


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--gfx', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    expected = [('retail', args.retail, '146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b', 0x400000,
                 {0x619390: '8b0d34ada60085c97405e801900000', 0x6223a0: '8b01ff10b001c3',
                  0x5facbc: 'a1988ca60083f8038b4938c644247802774aff248540b25f00',
                  0x621ca0:'568bf15733ff',0x61dcab:'d958408b0d34ada600',
                  0x624520:'56578bf18b4608',0x621ea0:'8b44240483ec38',
                  0x61ff70:'83ec088b54240c',0x6a5804:'70ff6100',
                  0x625df0:'db442404568bf1578b7e2cd84f3c',0x61ed69:'6a106a148bcde87c700000',
                  0x6260ac:'d94038dcc0e8c6c00400898614050000',0x626105:'8b7e2cd9473cd80dc02c6800',
                  0x62611c:'d9473cd80dfc196800',0x621d07:'db05fc9fa00083c404d80de83b6a00d95e38',
                  0x621d19:'db0500a0a000897e40d80de43b6a00d95e3c',0x61fe7b:'d84f3cd94740d84f38dee9',
                  0x6a3be4:'8988083b',0x6a3be8:'cdcccc3a'}),
                ('gfx', args.gfx, '5546a2801854ddeddb64b21c5f4a8326f28b257f80d981fda3d312a7c06c5cc4', 0x10000000,
                 {0x100058d0: '8b4158c3', 0x10008a10: '8b4130c3', 0x1000a280: '8b4134c3',
                  0x1002cc88: 'b93500000033c083c418f3ab', 0x10041d00: '83ec085355',
                  0x100433c0: '83ec0c5355', 0x100434f0: '83ec085355', 0x10042e10: '83ec0c5355',
                  0x100cfd40: '010000000200000001000000010000000200000002000000',
                  0x100cfd58: '000000000000000001000000020000000100000002000000',
                  0x100419f0:'83ec0ca00a971810',0x10042d10:'568bf1578b7c240c',
                  0x100352d0:'83ec08558b6c2414',0x100354f0:'51558b6c2410',
                  0x10041d1c:'8b8e3003000033c0668b013be8',0x10041d62:'578b7c24208b8cbe60030000'})]
    result = []
    for name, path, digest, base, guards in expected:
        data = path.read_bytes()
        assert hashlib.sha256(data).hexdigest() == digest, (name, 'identity')
        pe = pefile.PE(data=data)
        assert pe.OPTIONAL_HEADER.ImageBase == base
        for va, hex_bytes in guards.items():
            wanted = bytes.fromhex(hex_bytes)
            offset = pe.get_offset_from_rva(va - base)
            assert data[offset:offset + len(wanted)] == wanted, (name, hex(va))
            result.append(dict(module=name, va=hex(va), file_offset=hex(offset), expected=hex_bytes))
        if name == 'gfx':
            exports = {(x.name or b'').decode(): base + x.address for x in pe.DIRECTORY_ENTRY_EXPORT.symbols}
            for symbol, va in [('?getName@igImage@Gfx@Gap@@QBEPBDXZ', 0x100058d0),
                               ('?getImageSize@igImage@Gfx@Gap@@QBEHXZ', 0x10008a10),
                               ('?getData@igImage@Gfx@Gap@@QBEPBXXZ', 0x1000a280)]:
                assert exports[symbol] == va
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(dict(guards=result, runtime_af_admitted='opt-in authored path + current binding + perspective owner'), indent=2) + '\n', encoding='utf-8')
    print(f'PASS {len(result)} pinned A/B byte spans and three image getter exports; no runtime AF claim')


if __name__ == '__main__':
    main()
