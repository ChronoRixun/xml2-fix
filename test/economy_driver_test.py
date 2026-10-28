"""No pipe/game/desktop required: canonical comparator and routed-input driver."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("economy_driver", Path(__file__).parents[1] / "tools/economy_driver.py")
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


def line(seq=1, **changes):
    fields = dict(seq=str(seq), player="1", gate="0", tab="0", list="buy", row="0:unique", owner="-1", result="buy", money="100", defs="a"*16, stock="b"*16, rng="42")
    fields.update(changes)
    return "econ " + " ".join(f"{k}={v}" for k, v in fields.items())


class FakeWorld:
    def __init__(self):
        self.tab = self.selected = 0
        self.lines = []
        self.commands = [[], []]
        self.rows = [dict(index=0, type=0, definition="unique", owner=-1), dict(index=1, type=1, definition="bag", owner=-1)]

    def request(self, peer, command):
        self.commands[peer].append(command)
        if command.startswith("economy events"):
            after = int(command.split()[2])
            return dict(first=1 if self.lines else 0, last=len(self.lines), lost=False, lines=self.lines[after:])
        if command == "economy shop":
            return dict(active=True, sample=dict(vendor="forge", tab=self.tab, list="buy", selected=self.selected, error="", rows=self.rows))
        if command == "tap RIGHT 80":
            self.tab = 1-self.tab
        elif command == "tap DOWN 80":
            self.selected = min(1, self.selected+1)
        elif command == "tap UP 80":
            self.selected = max(0, self.selected-1)
        elif command.startswith("script "):
            self.lines.append(line(len(self.lines)+1, result="open", player="-1", gate="-1"))
        elif command == "tap RETURN 80":
            result = "bag-equipment" if self.selected == 1 else "sell" if self.tab else "buy"
            self.lines.append(line(len(self.lines)+1, result=result, tab=str(self.tab), row=f"{self.selected}:"+self.rows[self.selected]["definition"]))
        else:
            raise AssertionError(command)
        return "ok"


class DriverTests(unittest.TestCase):
    def test_first_differing_field(self):
        self.assertEqual(driver.first_difference(line(), line(player="2", stock="c"*16)), "player")
        self.assertEqual(driver.first_difference(line(), line(stock="c"*16)), "stock")
        self.assertIsNone(driver.first_difference(line(), line()))

    def test_malformed_equal_lines_are_not_success(self):
        with self.assertRaises(driver.AuditFailure):
            driver.first_difference("econ seq=1", "econ seq=1")

    def test_observer_errors_fail_even_if_equal(self):
        with self.assertRaises(driver.AuditFailure):
            driver.first_difference(line(result="error-row-identity"), line(result="error-row-identity"))

    def test_lost_history_refused(self):
        with self.assertRaises(driver.AuditFailure):
            driver.check_history(dict(first=45, last=300, lost=True, lines=[]), 0)

    def test_sequence_gap_refused(self):
        with self.assertRaises(driver.AuditFailure):
            driver.check_history(dict(first=1, last=3, lost=False, lines=[line(1), line(3)]), 0)

    def test_navigation_and_bag_use_only_driving_peer_input(self):
        world = FakeWorld()
        ask = [lambda c, p=p: world.request(p, c) for p in (0, 1)]
        shop = driver.ShopDriver(ask, driving=1, timeout=.05, pause=lambda _: None)
        shop.run([dict(op="open", vendor="forge"), dict(op="buy", definition="unique"), dict(op="bag", expect="bag-equipment")])
        mutations = [c for c in world.commands[0] if not c.startswith("economy ")]
        self.assertEqual(mutations, [])
        self.assertEqual([c for c in world.commands[1] if c.startswith("script ")], ["script shopMenu('equip')"])
        self.assertEqual(world.commands[1].count("tap RETURN 80"), 2)
        self.assertEqual(shop.audit.cursor, 3)

    def test_missing_peer_event_times_out(self):
        empty = dict(first=0, last=0, lost=False, lines=[])
        full = dict(first=1, last=1, lost=False, lines=[line()])
        audit = driver.PeerAudit([lambda c: full, lambda c: empty], timeout=.001, pause=lambda _: None)
        with self.assertRaisesRegex(driver.AuditFailure, "missing.*seq=1"):
            audit.one()

    def test_ambiguous_row_refused(self):
        rows = [dict(index=i, type=0, definition="unique", owner=-1) for i in (0,1)]
        with self.assertRaises(driver.AuditFailure):
            driver.choose_row(rows, dict(definition="unique"))

    def test_equal_wrong_transaction_is_not_success(self):
        for field, value in dict(row="1:wrong", owner="3", tab="1", list="wrong").items():
            with self.subTest(field=field):
                world = FakeWorld()
                ask = [lambda c, p=p: world.request(p, c) for p in (0, 1)]
                original = world.request
                def wrong(peer, command):
                    result = original(peer, command)
                    if command == "tap RETURN 80":
                        world.lines[-1] = line(len(world.lines), **{field:value})
                    return result
                world.request = wrong
                shop = driver.ShopDriver(ask, timeout=.01, pause=lambda _: None)
                with self.assertRaisesRegex(driver.AuditFailure, "intended.*"+field):
                    shop.run([dict(op="buy", definition="unique")])

    def test_requested_player_is_checked(self):
        world = FakeWorld()
        ask = [lambda c, p=p: world.request(p, c) for p in (0, 1)]
        shop = driver.ShopDriver(ask, timeout=.01, pause=lambda _: None, expect_player=0)
        with self.assertRaisesRegex(driver.AuditFailure, "intended.*player"):
            shop.run([dict(op="buy", definition="unique")])

    def test_open_vendor_is_checked(self):
        world = FakeWorld()
        ask = [lambda c, p=p: world.request(p, c) for p in (0, 1)]
        shop = driver.ShopDriver(ask, timeout=.01, pause=lambda _: None)
        with self.assertRaisesRegex(driver.AuditFailure, "open vendor expected=healer got=forge"):
            shop.run([dict(op="open", vendor="healer")])

    def test_equipped_owner_selects_exact_row(self):
        rows = [dict(index=i, type=0, definition="unique", owner=i-1) for i in (0,1)]
        self.assertEqual(driver.choose_row(rows, dict(definition="unique", owner=0)), 1)


if __name__ == "__main__":
    unittest.main()
