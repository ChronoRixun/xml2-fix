"""Offline protocol regressions; never open a pipe or inject input."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock
import io

spec = importlib.util.spec_from_file_location("probe", Path(__file__).parents[1] / "scripts/carry_live_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class Fake:
    def __init__(self, refill=True, stalled=False):
        self.now = 0.
        self.energy = 0.
        self.refill = refill
        self.stalled = stalled
        self.commands = []

    def sleep(self, seconds):
        self.now += seconds

    def send(self, command):
        self.commands.append(command)
        if command == "state":
            stamp = 1 if self.stalled and self.now > .2 else 1 + int(self.now * 1000)
            return {"sampled_ms": stamp, "actors": [{"entity_id": 17, "energy": self.energy}]}
        if command.startswith('script restoreEnergy("17",999)') and self.refill:
            self.energy = 80.
        return "ok queued"


class ProbeTests(unittest.TestCase):
    def run_probe(self, fake):
        return probe.record_hold(fake.send, 17, "pad", 1, 4000, 20., lambda: fake.now, fake.sleep)

    def test_measured_refill_and_held_samples(self):
        fake = Fake()
        rows = self.run_probe(fake)
        self.assertGreater(len(rows), 30)
        self.assertIn('script restoreEnergy("17",999)', fake.commands)
        self.assertGreater(fake.commands.count('script restoreEnergy("17",999)'), 5)
        self.assertIn("paddown 1 X", fake.commands)
        self.assertEqual(fake.commands[-1], "padrelease 1")
        self.assertTrue(all(row["state"]["actors"][0]["energy"] >= 20 for row in rows))

    def test_queued_ack_is_not_energy(self):
        fake = Fake(refill=False)
        with self.assertRaisesRegex(RuntimeError, "measured energy"):
            self.run_probe(fake)
        self.assertNotIn("paddown 1 X", fake.commands)
        self.assertEqual(fake.commands[-1], "padrelease 1")

    def test_stale_samples_abort_and_release(self):
        fake = Fake(stalled=True)
        with self.assertRaisesRegex(RuntimeError, "stalled"):
            self.run_probe(fake)
        self.assertEqual(fake.commands[-1], "padrelease 1")

    def test_hold_cannot_outlast_backend_hard_release(self):
        fake = Fake()
        with self.assertRaisesRegex(ValueError, "hard release"):
            probe.record_hold(fake.send, 17, "pad", 1, 12000, 20., lambda: fake.now, fake.sleep)
        self.assertEqual(fake.commands, [])

    def test_execute_requires_rank_evidence_before_opening_pipe(self):
        argv = ["carry_live_probe.py", "--device", "pad", "--actor", "17", "--execute"]
        with mock.patch("sys.argv", argv), mock.patch("sys.stderr", new=io.StringIO()), mock.patch("builtins.open", side_effect=AssertionError("pipe opened before rank evidence was required")):
            with self.assertRaises(SystemExit) as error:
                probe.main()
        self.assertEqual(error.exception.code, 2)

    def test_rank_comes_from_fresh_same_generation_trace(self):
        verify = getattr(probe, "require_flight_rank", lambda *args: None)
        log = "[11:40:00] carry trace flight-rank-3 ms=1000 actor=00000011 victim=00000000\n"
        self.assertEqual(verify(log, 17, 1500), 3)

    def test_wrong_rank_is_not_a_valid_rank_three_trial(self):
        verify = getattr(probe, "require_flight_rank", lambda *args: None)
        with self.assertRaises(RuntimeError):
            verify("carry trace flight-rank-1 ms=1000 actor=00000011 ", 17, 1500)

    def test_stale_or_different_generation_rank_cannot_validate_trial(self):
        verify = getattr(probe, "require_flight_rank", lambda *args: None)
        for log in ("carry trace flight-rank-3 ms=1 actor=00000011 ", "carry trace flight-rank-3 ms=1000 actor=00010011 "):
            with self.assertRaises(RuntimeError):
                verify(log, 17, 1500)


if __name__ == "__main__":
    unittest.main()
