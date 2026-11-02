"""Synthetic regressions for the offline late-join log comparison tool."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('jip_components', Path(__file__).with_name('jip_components.py'))
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

RAW = 'jip hero: frame=30 slot=0 handle=00003802 ctl={ctl} t=40000000 dt=3d088889 d20={pos},40000000 d120=00000000\n'
CHECKSUM = ('jip checksum hero: epoch=1 frame=30 slot=0 contribution=0x00000001 '
            'controller=0 ai=0x00000000 transform20..34=3f800000,40000000,00000000,00000000,00000000,00000000\n'
            '[time] jip checksum produced: epoch=1 sample=1 frame=30 hash=0x00000003 heroes=1 complete=1 '
            'hero_sum=0x00000001 camera=0x00000001 rng_draws=1 seed=0x00000001\n')


class ComponentsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.host = Path(self.temp.name) / 'host.log'
        self.guest = Path(self.temp.name) / 'guest.log'

    def write_pair(self, host, guest):
        self.host.write_text(host, encoding='utf-8')
        self.guest.write_text(guest, encoding='utf-8')

    def test_checksum_parser_keeps_checksum_heroes_beside_raw_rows(self):
        self.write_pair(CHECKSUM + RAW.format(ctl=0, pos='3f800000'), '')
        samples, _ = tool.parse(self.host)
        self.assertEqual(samples[(1, 30)]['slots'][0][0], '3F800000,40000000,00000000,00000000,00000000,00000000')
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.main(['jip_components', str(self.host), str(self.host)]), 0)

    def test_local_controller_index_does_not_hide_equal_fields(self):
        self.write_pair(RAW.format(ctl=0, pos='3f800000'), RAW.format(ctl=1, pos='3f800000'))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.hero_mode(self.host, self.guest), 0)

    def test_raw_position_difference_is_named_despite_local_controllers(self):
        self.write_pair(RAW.format(ctl=0, pos='3f800000'), RAW.format(ctl=1, pos='3f800001'))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(tool.hero_mode(self.host, self.guest), 1)
        self.assertIn('first difference at frame 30 slot 0', output.getvalue())
        self.assertIn('+0x020: host 3f800000 guest 3f800001', output.getvalue())


    def test_spawn_mode_names_the_same_entities_spawned_at_different_frames(self):
        # p060fix/AJ (ZB-3Pp): identical spawn sets, the joiner's all at frame 0, the host's spread over frames 2..3
        line = 'jip spawn: phase=4 frame={f} n={n} caller=58ae0e78 def=roundtable_x inst=roundtable_x at=2181,2113,0 draws=15\n'
        host = ''.join(line.format(f=2 if i < 7 else 3, n=i) for i in range(40))
        guest = ''.join(line.format(f=0, n=i) for i in range(40))
        self.write_pair(host, guest)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(tool.spawn_mode(str(self.host), str(self.guest)), 1)
        self.assertIn('spawn frames differ: host {2: 7, 3: 33} / guest {0: 40}', out.getvalue())
        self.write_pair(host, host)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.spawn_mode(str(self.host), str(self.guest)), 0)

    def test_cadence_mode_names_the_actors_updated_at_another_cadence(self):
        # p060fix/AK (ZD-3P-E2p): the same kids, three of them every frame (now + 0.001) on one peer and three others on the other
        def dump(fast):
            head = ','.join(f'{"40023284" if i in fast else "40088888"}:{i}:{0x1800 + i:08x}:320:00' for i in range(0x114, 0x11a))
            return f'jip events: where=frame 1 heap=6 records=6 ring=r:6/w:0/n:1119 live=6 dead=0 stale=0 rebuilds=3 part=1/1 head={head}\n'
        host, guest = dump({0x116, 0x117, 0x119}), dump({0x115, 0x117, 0x119})
        self.write_pair(host, guest)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(tool.cadence_mode(str(self.host), str(self.guest)), 1)
        self.assertIn("update cadence differs at frame 1: 115: host ['40088888'] guest ['40023284'], 116: host ['40023284'] guest ['40088888']", out.getvalue())
        self.write_pair(host, host)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.cadence_mode(str(self.host), str(self.guest)), 0)
        self.write_pair('nothing\n', host)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.cadence_mode(str(self.host), str(self.guest)), 2)

    def test_per_process_dwords_and_local_controllers_together_still_agree(self):
        line = 'jip hero: frame=30 slot=0 handle=00003802 ctl={ctl} t=40000000 dt=3d088889 d20=3f800000 d21c={count},{ptr}\n'
        self.write_pair(line.format(ctl=0, count='00000001', ptr='02000000'), line.format(ctl=1, count='00000002', ptr='03000000'))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.hero_mode(self.host, self.guest), 0)

if __name__ == '__main__':
    unittest.main()
