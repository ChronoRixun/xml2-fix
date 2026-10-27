"""Record a sustained carry on an already running test copy; default is plan only.

Uses only the configured test pipe. Never launches or activates an application.
Registered restoreEnergy is measured through fresh state samples, then repeated
during the hold to separate input/attachment behavior from legitimate exhaustion.
"""
import argparse
import json
from pathlib import Path
import re
import time


def energy(state, actor):
    matches = [a for a in state.get("actors", []) if a.get("entity_id") == actor]
    if len(matches) != 1:
        raise RuntimeError("actor token is missing or ambiguous in state")
    value = matches[0].get("energy")
    return value if isinstance(value, (int, float)) else -1


def require_flight_rank(log, actor, sampled_ms, expected=3):
    seen = [(int(rank), int(stamp)) for rank, stamp, token in re.findall(
        r"carry trace flight-rank-(\d+) ms=(\d+) actor=([0-9A-Fa-f]{8})\b", log)
        if int(token, 16) == actor]
    if not seen or abs(sampled_ms - seen[-1][1]) > 1000:
        raise RuntimeError("no fresh same-generation effective Flight rank; enable CarryTrace and start airborne")
    if seen[-1][0] != expected:
        raise RuntimeError(f"effective Flight rank {seen[-1][0]}, expected {expected}; unspent points do not establish rank")
    return seen[-1][0]


def record_hold(send, actor, device, pad, milliseconds, cost, clock=time.monotonic, sleep=time.sleep, rank_check=None):
    # Both injection backends expire a durationless down after 10 seconds.
    # Leave one second for pipe/command latency; never renew a press implicitly.
    if not 4000 <= milliseconds <= 9000:
        raise ValueError("carry hold must be 4000..9000 ms, below the input hard release")
    release = f"padrelease {pad}" if device == "pad" else "release"
    down = f"paddown {pad} X" if device == "pad" else "down E"
    refill = f'script restoreEnergy("{actor}",999)'
    rows = []
    try:
        send(release)
        before = send("state")
        if rank_check:
            rank_check(before)
        stamp = before.get("sampled_ms", 0)
        send(refill)
        deadline = clock() + 3
        while True:
            sleep(.1)
            state = send("state")
            if state.get("sampled_ms", 0) > stamp and energy(state, actor) >= cost:
                break
            if clock() >= deadline:
                raise RuntimeError("registered refill did not produce fresh measured energy sufficient for flight")
        if rank_check:
            rank_check(state)
        send(down)
        start = changed = last_refill = clock()
        stamp = state["sampled_ms"]
        while clock() - start < milliseconds / 1000:
            sleep(.1)
            state = send("state")
            current = state.get("sampled_ms", 0)
            if current > stamp:
                stamp, changed = current, clock()
            if clock() - changed > 1:
                raise RuntimeError("state samples stalled during held input")
            rows.append({"elapsed": round(clock() - start, 3), "state": state})
            if energy(state, actor) < cost:
                raise RuntimeError("measured energy fell below flight cost; this is not a valid sustained-carry trial")
            if clock() - last_refill >= .5:
                send(refill)
                last_refill = clock()
    finally:
        send(release)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pipe", default="xml2-fix-input")
    parser.add_argument("--device", choices=("keyboard", "pad"), required=True)
    parser.add_argument("--pad", type=int, choices=range(1, 5), default=1)
    parser.add_argument("--actor", type=int, required=True, help="generation-bearing entity_id from fresh state")
    parser.add_argument("--ms", type=int, choices=range(4000, 9001), default=8000)
    parser.add_argument("--cost", type=float, default=20., help="rank 1..5 costs 40/30/20/10/5")
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--trace-log", help="the running test copy's xml2-fix.log with CarryTrace=1")
    parser.add_argument("--rank", type=int, choices=(3, 4, 5), default=3, help="effective Flight rank required for this positive trial")
    args = parser.parse_args()
    if not args.pipe or len(args.pipe) > 64 or any(not (c.isascii() and (c.isalnum() or c in "-_")) for c in args.pipe):
        parser.error("invalid configured PipeName")
    if not 0 < args.actor <= 0xffffffff or not 0 < args.cost <= 120:
        parser.error("actor token or flight cost out of range")
    if not args.execute:
        print(json.dumps({"execute": False, "pipe": args.pipe, "actor": args.actor, "hold_ms": args.ms,
                          "refill": f'restoreEnergy("{args.actor}",999)', "require_fresh_energy": args.cost,
                          "require_fresh_flight_rank": args.rank, "trace_log": args.trace_log,
                          "repeat_refill_ms": 500, "device": args.device}, indent=2))
        return
    if not args.trace_log or not Path(args.trace_log).is_file():
        parser.error("--execute requires --trace-log from the running copy with CarryTrace=1")
    with open("\\\\.\\pipe\\" + args.pipe, "r+b", buffering=0) as pipe:
        def send(command):
            pipe.write((command + "\n").encode("ascii"))
            reply = bytearray()
            while not reply.endswith(b"\n"):
                char = pipe.read(1)
                if not char:
                    raise RuntimeError("pipe closed before reply")
                reply += char
            answer = reply.decode("utf-8", errors="replace").strip()
            print(json.dumps({"command": command, "reply": answer}), flush=True)
            if answer.startswith("error"):
                raise RuntimeError(answer)
            return json.loads(answer) if command == "state" else answer
        def rank_check(state):
            actors = [a for a in state.get("actors", []) if a.get("entity_id") == args.actor]
            if len(actors) != 1 or actors[0].get("flying") is not True:
                raise RuntimeError("rank calibration requires the selected actor to be airborne")
            require_flight_rank(Path(args.trace_log).read_text(encoding="utf-8", errors="replace"),
                                args.actor, state.get("sampled_ms", 0), args.rank)
        rows = record_hold(send, args.actor, args.device, args.pad, args.ms, args.cost, rank_check=rank_check)
        print(json.dumps({"samples": rows}), flush=True)


if __name__ == "__main__":
    main()
