"""Offline release policy checks: python scripts/test_release_metadata.py."""
import json
import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import release_metadata as R
import check_release_dll as D
from unittest.mock import patch


class ReleaseTests(unittest.TestCase):
    def test_tags_and_flags(self):
        for tag, base, pre in [('v1.8.0', '1.8.0', False),
                               ('v1.8.0-beta.2', '1.8.0', True),
                               ('v0.3.0-rc.1', '0.3.0', True)]:
            with self.subTest(tag=tag):
                meta = R.metadata(tag)
                self.assertEqual(meta['version'], tag[1:])
                self.assertEqual(meta['base'], base)
                self.assertEqual(meta['flags'], ['--prerelease', '--latest=false'] if pre else [])
                self.assertEqual(R.numeric_version(tag[1:]), base)
        for tag in ['malformed', '1.8.0', 'v01.8.0', 'v1.8.0-beta', 'v1.8.0-beta.02',
                    'v1.8.0-alpha.1', 'v1.8.0+build', 'v1.8.0-rc.1\n', 'v1.8.0-beta.2/evil']:
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                R.metadata(tag)

    def test_notes_and_version_check(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'v1.8.0.md').write_text('Stable notes\n', encoding='utf-8')
            self.assertEqual(R.release_notes('v1.8.0', root), 'Stable notes\n')
            self.assertEqual(R.release_notes('v1.8.0-beta.2', root), 'Beta - test build\n\nStable notes\n')
            self.assertEqual(R.release_notes('v1.8.0-rc.1', root), 'Beta - test build\n\nStable notes\n')
            self.assertIsNone(R.release_notes('v9.0.0', root))  # stable generate-notes fallback
            with self.assertRaises(FileNotFoundError):
                R.release_notes('v9.0.0-beta.1', root)
        R.check_version('v1.8.0-beta.2', '1.8.0')
        with self.assertRaises(ValueError):
            R.check_version('v1.8.0-beta.2', '1.7.0')

    def test_cli(self):
        p = subprocess.run([sys.executable, str(Path(R.__file__)), '--tag', 'v1.8.0-beta.2',
                            '--built-version', '1.8.0'], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(json.loads(p.stdout)['version'], '1.8.0-beta.2')
        p = subprocess.run([sys.executable, str(Path(R.__file__)), '--tag', 'malformed'],
                           capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)

    def test_manifest(self):
        import hashlib
        import os
        import shutil
        shell = shutil.which('pwsh') or shutil.which('powershell.exe')
        if not shell:
            self.skipTest('PowerShell required to verify workflow manifest bytes')
        workflow = Path(R.__file__).resolve().parents[1] / '.github/workflows/build.yml'
        lines = workflow.read_text(encoding='utf-8').splitlines()
        start = next(i for i, line in enumerate(lines) if line.strip() == 'Copy-Item build/bin/Release/dinput.dll dist/dinput.dll')
        end = next(i for i in range(start, len(lines)) if lines[i].strip() == 'Get-Content dist/ultimate-legends.json')
        script = '\n'.join(line[10:] for line in lines[start:end])
        self.assertNotIn('--manifest-output', script)  # keep the original PowerShell serializer for every tag
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            dll = root / 'build/bin/Release/dinput.dll'
            dll.parent.mkdir(parents=True)
            (root / 'dist').mkdir()
            data = b'synthetic release DLL'
            dll.write_bytes(data)
            expected = json.dumps([['dinput.dll', len(data), hashlib.sha1(data).hexdigest().upper(), 'game']],
                                  separators=(',', ':')).encode('ascii')
            for tag in ['v1.8.0', 'v1.8.0-beta.2', 'v0.3.0-rc.1', 'dev']:
                env = dict(os.environ, GITHUB_REF_TYPE='tag' if tag != 'dev' else 'branch', GITHUB_REF_NAME=tag)
                p = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-Command',
                                    "$ErrorActionPreference='Stop'; " + script], cwd=root, env=env,
                                   capture_output=True, text=True)
                self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
                self.assertEqual((root / 'dist/ultimate-legends.json').read_bytes(), expected)

    def test_dll_checker_accepts_suffix_but_requires_matching_base(self):
        # Isolate version checking from the independently tested PE parser and GNS binary checks.
        data = b''.join(D.GNS_MARKERS)
        for version, expected in [('1.8.0', 0), ('1.8.0-beta.2', 0), ('1.8.0-rc.1', 0), ('1.7.0-beta.2', 1)]:
            with self.subTest(version=version), \
                 patch.object(sys, 'argv', ['check_release_dll.py', 'synthetic.dll', '--version', version]), \
                 patch.object(Path, 'read_bytes', return_value=data), \
                 patch.object(D, 'pe_imports', return_value=(0x14c, ['bcrypt.dll'])), \
                 patch.object(D, 'version_strings', return_value={'ProductVersion': '1.8.0', 'FileVersion': '1.8.0'}), \
                 patch.object(D, 'MIN_SIZE', 0), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(D.main(), expected)


if __name__ == '__main__':
    unittest.main()
