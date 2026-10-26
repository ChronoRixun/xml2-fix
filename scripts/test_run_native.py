"""Offline check of the native runner's matrix mode: python scripts/test_run_native.py. `run_native.py --list` runs
in a scratch copy of the repository layout (synthetic test executables, an existing xml2-fix.ini next to them) and
must print the matrix without creating, changing or deleting anything."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent


def tree(root):
    return {str(p.relative_to(root)): (p.read_bytes() if p.is_file() else None) for p in sorted(root.rglob('*'))}


class ListModeTests(unittest.TestCase):
    def test_list_is_side_effect_free(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td) / 'repo'
            (root / 'scripts').mkdir(parents=True)
            shutil.copyfile(HERE / 'run_native.py', root / 'scripts' / 'run_native.py')
            (root / 'test').mkdir()
            for name in ('xml2_test.cpp', 'classic_hud_install_test.cpp'):
                shutil.copyfile(REPO / 'test' / name, root / 'test' / name)
            bins = root / 'native' / 'bin' / 'Release'
            bins.mkdir(parents=True)
            for name in ('xml2_npc_query_scheduler_test.exe', 'xml2_npc_semi_auto_test.exe', 'xml2_log_test.exe',
                         'xml2_speaker_label_install_test.exe', 'xml2_pool_counter_observers_test.exe'):
                (bins / name).write_bytes(b'MZ')
            (bins / 'xml2-fix.ini').write_bytes(b'[Game]\r\nKeep=1\r\n')
            vectors = Path(td) / 'vectors'
            (vectors / 'case1').mkdir(parents=True)
            (vectors / 'case1' / 'cases.tsv').write_bytes(b'')
            env = dict(os.environ, XML2_FIX_TEST_EXE='X:/game/XMen2.exe', XML2_FIX_TEST_GAME_DIR='X:/game',
                       XML2_FIX_TEST_XBE='X:/xbox/default.xbe', XML2_FIX_LEGACY_VECTORS_DIR=str(vectors),
                       PYTHONDONTWRITEBYTECODE='1')
            before = tree(Path(td))
            done = subprocess.run([sys.executable, '-B', str(root / 'scripts' / 'run_native.py'), '--list'],
                                  cwd=td, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertEqual(tree(Path(td)), before, 'run_native.py --list changed the file system')
            lines = done.stdout.splitlines()
            self.assertTrue(any(l.endswith('xml2_npc_query_scheduler_test.exe X:/game/XMen2.exe X:/xbox/default.xbe') for l in lines))
            self.assertTrue(any(l.endswith('xml2_npc_semi_auto_test.exe X:/game/XMen2.exe X:/xbox/default.xbe') for l in lines))
            self.assertTrue(any(l.endswith('xml2_pool_counter_observers_test.exe X:/game/XMen2.exe') for l in lines))
            self.assertTrue(any(l.endswith('xml2_speaker_label_install_test.exe X:/game/XMen2.exe guard6') for l in lines))
            self.assertTrue(any('lcsave_durable_parity.exe' in l and l.endswith('case1') for l in lines))
            self.assertFalse(any('deleted a stale' in l for l in lines))


if __name__ == '__main__':
    unittest.main()
