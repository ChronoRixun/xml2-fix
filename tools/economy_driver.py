"""Script shops through opt-in test pipes; compare every native event on two peers.

No process launches, state writes, RNG injection, desktop input or replication.
Only the selected driving peer receives script/key/pad commands. Both peers must
already be running in co-op. See docs/xml1-economy-digest.md for the JSON plan.
"""
import argparse
import json
import os
from pathlib import Path
import re
import time

FIELDS = ("seq", "player", "gate", "tab", "list", "row", "owner", "result", "money", "defs", "stock", "rng")


class AuditFailure(RuntimeError):
    pass


def parse_line(line):
    parts = line.split()
    if not parts or parts[0] != "econ" or len(parts) != len(FIELDS)+1:
        raise AuditFailure("format: malformed economy line: " + line)
    values = {}
    for key, field in zip(FIELDS, parts[1:]):
        name, sep, value = field.partition("=")
        if name != key or not sep or not value:
            raise AuditFailure("format: expected " + key + " in " + line)
        values[key] = value
    for key in ("seq", "player", "gate", "tab", "owner", "money", "rng"):
        if not re.fullmatch(r"-?[0-9]+", values[key]):
            raise AuditFailure("format: invalid " + key)
    if int(values["seq"]) < 1 or int(values["rng"]) < 0:
        raise AuditFailure("format: invalid sequence or RNG counter")
    for key in ("defs", "stock"):
        if not re.fullmatch(r"[0-9a-f]{16}", values[key]):
            raise AuditFailure("format: invalid " + key)
    if values["result"].startswith("error-") or int(values["owner"]) < -1:
        raise AuditFailure("observer error: " + line)
    return values


def first_difference(left, right):
    a, b = parse_line(left), parse_line(right)
    return next((key for key in FIELDS if a[key] != b[key]), None)


def check_history(reply, after):
    if reply.get("lost"):
        raise AuditFailure(f"history lost after seq={after}: first={reply.get('first')}")
    lines = reply.get("lines")
    if not isinstance(lines, list):
        raise AuditFailure("format: no history lines")
    expected = after+1
    for line in lines:
        if int(parse_line(line)["seq"]) != expected:
            raise AuditFailure(f"sequence gap: expected seq={expected}")
        expected += 1
    if reply.get("last") != expected-1:
        raise AuditFailure(f"history tail differs: last={reply.get('last')} expected={expected-1}")
    return lines


class Pipe:
    """One byte-mode request/reply pipe, bounded connect and response waits."""
    def __init__(self, name, timeout=8, trace=None):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,80}", name):
            raise AuditFailure("invalid pipe name")
        self.path = "\\\\.\\pipe\\" + name
        self.name, self.timeout, self.trace = name, timeout, trace
        self.fd = None

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __call__(self, command):
        import ctypes
        import ctypes.wintypes as w
        import msvcrt
        if "\n" in command or "\r" in command:
            raise AuditFailure("one pipe command per line")
        deadline = time.monotonic()+self.timeout
        while self.fd is None:
            try:
                self.fd = os.open(self.path, os.O_RDWR | os.O_BINARY)
            except OSError as error:
                if time.monotonic() >= deadline:
                    raise AuditFailure(f"pipe {self.name}: connect timeout: {error}") from error
                time.sleep(.05)
        peek = ctypes.windll.kernel32.PeekNamedPipe
        peek.argtypes = (w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.c_void_p, ctypes.POINTER(w.DWORD), ctypes.c_void_p)
        available = w.DWORD()
        data = (command+"\n").encode("ascii")
        try:
            if os.write(self.fd, data) != len(data):
                raise AuditFailure("partial pipe write")
            reply = b""
            while b"\n" not in reply:
                if not peek(msvcrt.get_osfhandle(self.fd), None, 0, None, ctypes.byref(available), None):
                    raise AuditFailure(f"pipe {self.name}: disconnected")
                if available.value:
                    reply += os.read(self.fd, min(available.value, 65536))
                elif time.monotonic() >= deadline:
                    raise AuditFailure(f"pipe {self.name}: response timeout for {command}")
                else:
                    time.sleep(.01)
                if len(reply) > 1024*1024:
                    raise AuditFailure("oversize pipe reply")
            text = reply.split(b"\n", 1)[0].decode("utf-8").rstrip("\r")
            if self.trace:
                self.trace(dict(pipe=self.name, command=command, reply=text))
            if text.startswith("error"):
                raise AuditFailure(f"pipe {self.name}: {text}")
            return json.loads(text) if text.startswith("{") else text
        except Exception:
            self.close()
            raise


class PeerAudit:
    def __init__(self, ask, timeout=8, pause=time.sleep, record=None):
        self.ask, self.timeout, self.pause, self.record = ask, timeout, pause, record
        self.cursor = 0

    def read(self):
        replies = [request(f"economy events {self.cursor}") for request in self.ask]
        return [check_history(reply, self.cursor) for reply in replies]

    def compare(self, batches):
        for a, b in zip(*batches):
            field = first_difference(a, b)
            if field:
                raise AuditFailure(f"first differing field={field} seq={self.cursor+1}\nhost: {a}\nclient: {b}")
            if self.record:
                self.record(dict(seq=self.cursor+1, host=a, client=b))
            self.cursor += 1

    def baseline(self):
        batches = self.read()
        if len(batches[0]) != len(batches[1]):
            raise AuditFailure(f"missing baseline event seq={min(map(len, batches))+1}")
        self.compare(batches)

    def quiet(self):
        if any(self.read()):
            raise AuditFailure(f"unexpected event before action seq={self.cursor+1}")

    def one(self):
        deadline = time.monotonic()+self.timeout
        while True:
            batches = self.read()
            if any(len(batch) > 1 for batch in batches):
                raise AuditFailure(f"unexpected extra event seq={self.cursor+2}; input repeated or another actor acted")
            if all(batches):
                self.compare(batches)
                return parse_line(batches[0][0])
            if time.monotonic() >= deadline:
                peer = "host" if not batches[0] else "client"
                raise AuditFailure(f"missing event seq={self.cursor+1} peer={peer}; shop rows return to design")
            self.pause(.05)


def choose_row(rows, action):
    matches = list(rows)
    if "row" in action:
        matches = [r for r in matches if r["index"] == action["row"]]
    if "definition" in action:
        matches = [r for r in matches if r["definition"] == action["definition"]]
    if "owner" in action:
        matches = [r for r in matches if r["owner"] == action["owner"]]
    if action.get("op") == "bag":
        matches = [r for r in matches if r["type"] == 1]
    if len(matches) != 1:
        raise AuditFailure(f"row selector matched {len(matches)} rows; specify definition/owner/row exactly: {action}")
    return matches[0]["index"]


class ShopDriver:
    def __init__(self, ask, driving=0, timeout=8, pause=time.sleep, record=None, pad=None, next_tab="RIGHT", accept="RETURN", back="ESCAPE", expect_player=None):
        self.ask, self.driving, self.timeout, self.pause = ask, driving, timeout, pause
        self.audit = PeerAudit(ask, timeout, pause, record)
        self.pad, self.next_tab, self.accept, self.back = pad, next_tab, accept, back
        self.expect_player = expect_player

    def press(self, key):
        buttons = {"RETURN": "A", "ESCAPE": "B"}
        command = f"pad {self.pad} {buttons.get(key, key)} 80" if self.pad else f"tap {key} 80"
        self.ask[self.driving](command)

    def view(self):
        view = self.ask[self.driving]("economy shop")
        if not view.get("active") or view["sample"].get("error"):
            raise AuditFailure("shop is inactive or observation failed: " + str(view))
        return view["sample"]

    def wait_view(self, predicate):
        deadline = time.monotonic()+self.timeout
        while time.monotonic() < deadline:
            view = self.view()
            if predicate(view):
                return view
            self.pause(.05)
        raise AuditFailure("navigation timeout; check menu bindings, no transaction sent")

    def tab(self, target):
        target = {"buy": 0, "sell": 1}.get(target, target)
        if target not in (0, 1):
            raise AuditFailure("tab must be buy/0 or sell/1")
        if self.view()["tab"] != target:
            self.press(self.next_tab)
            self.wait_view(lambda v: v["tab"] == target)

    def row(self, action):
        view = self.view()
        target = choose_row(view["rows"], action)
        # Native UI sorting can map adjacent visible rows to nonadjacent global
        # row IDs. Scan, reversing at a clamped boundary; never assume index +/-1.
        direction = "DOWN"
        for _ in range(2*len(view["rows"])+2):
            view = self.view()
            if view["selected"] == target:
                return next(r for r in view["rows"] if r["index"] == target)
            old = view["selected"]
            self.press(direction)
            # Allow an observation period even when the list clamps at its end.
            self.pause(.15)
            new = self.view()
            if new["selected"] == old:
                direction = "UP"
        raise AuditFailure(f"row {target} was not reached; no transaction sent")

    def run(self, plan):
        self.audit.baseline()
        for action in plan:
            self.audit.quiet()
            op = action.get("op")
            if op == "open":
                vendor = action.get("vendor")
                scripts = {"forge": "shopMenu('equip')", "healer": "shopMenu('item')", "stash": "stashMenu()"}
                if vendor not in scripts:
                    raise AuditFailure("open vendor must be forge, healer or stash")
                self.ask[self.driving]("script " + scripts[vendor])
                event = self.audit.one()
                if event["result"] != "open":
                    raise AuditFailure("open did not produce a native open event")
                observed = self.view().get("vendor")
                if observed != vendor:
                    raise AuditFailure(f"open vendor expected={vendor} got={observed}")
                continue
            if op == "close":
                self.press(self.back)
                self.pause(1.1)
                continue
            if op == "tab":
                self.tab(action["tab"])
                continue
            if op == "row":
                self.row(action)
                continue
            if op not in ("buy", "sell", "deposit", "withdraw", "bag"):
                raise AuditFailure(f"unknown plan op: {op}")
            self.tab(1 if op in ("sell", "deposit") else 0)
            selected = self.row(action)
            intended = self.view()
            expected_fields = dict(tab=str(intended["tab"]), list=intended["list"],
                                   row=f"{selected['index']}:{selected['definition']}", owner=str(selected["owner"]))
            if self.expect_player is not None:
                expected_fields["player"] = str(self.expect_player)
            self.audit.quiet()
            self.press(self.accept)
            event = self.audit.one()
            for field, value in expected_fields.items():
                if event[field] != value:
                    raise AuditFailure(f"seq={event['seq']} intended action field={field} expected={value} got={event[field]}")
            expected = action.get("expect", op)
            if expected == "bag":
                matched = event["result"] in ("bag-equipment", "bag-course", "bag-empty")
            else:
                matched = event["result"] == expected
            if not matched:
                raise AuditFailure(f"seq={event['seq']} expected result={expected}, got {event['result']}")
        self.audit.quiet()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-pipe", required=True)
    parser.add_argument("--client-pipe", required=True)
    parser.add_argument("--drive", choices=("host", "client"), required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=8)
    parser.add_argument("--pad", type=int, choices=range(1, 5))
    parser.add_argument("--expect-player", type=int, help="assert the native routed action-player index on every transaction")
    parser.add_argument("--next-tab", default="RIGHT")
    parser.add_argument("--accept", default="RETURN")
    parser.add_argument("--back", default="ESCAPE")
    args = parser.parse_args()
    if args.host_pipe == args.client_pipe:
        parser.error("two distinct peer pipes are required")
    plan = json.loads(args.plan.read_text(encoding="utf-8"))
    if not isinstance(plan, list):
        parser.error("plan must be a JSON array of operations")
    args.out.mkdir(parents=True, exist_ok=True)
    with (args.out / "requests.jsonl").open("w", encoding="utf-8", newline="\n") as trace, (args.out / "equal-events.jsonl").open("w", encoding="utf-8", newline="\n") as equal:
        def log_request(record):
            trace.write(json.dumps(record)+"\n")
            trace.flush()
        def log_equal(record):
            equal.write(json.dumps(record)+"\n")
            equal.flush()
        pipes = [Pipe(name, args.timeout, log_request) for name in (args.host_pipe, args.client_pipe)]
        try:
            shop = ShopDriver(pipes, driving=int(args.drive == "client"), timeout=args.timeout, record=log_equal, pad=args.pad, next_tab=args.next_tab, accept=args.accept, back=args.back, expect_player=args.expect_player)
            shop.run(plan)
            result = dict(status="PASS", compared_through=shop.audit.cursor, driving=args.drive, plan=str(args.plan))
            (args.out / "result.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
            print(json.dumps(result))
            return 0
        except (AuditFailure, OSError, ValueError) as error:
            # Capture both retained histories before exiting. Never send another
            # mutating command after a mismatch or attempt a synchronization fix.
            snapshots = []
            for pipe in pipes:
                try:
                    snapshots.append(pipe("economy events 0"))
                except Exception as capture_error:
                    snapshots.append(dict(error=str(capture_error)))
            result = dict(status="FAIL", reason=str(error), peers=snapshots)
            (args.out / "failure.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
            print("FAIL: " + str(error))
            return 1
        finally:
            for pipe in pipes:
                pipe.close()


if __name__ == "__main__":
    raise SystemExit(main())
