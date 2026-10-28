"""Plan or run human-length Guard input against an ALREADY RUNNING test copy.

No process launch, OS keyboard/mouse injection, window activation, or wake key.
Default is plan-only; --execute writes only to the named XML2 Fix test pipe.
"""
import argparse
import json
import time


def commands(device, milliseconds, pad):
    if device == "pad":
        return ["status", f"padrelease {pad}", f"padhold {pad} X {milliseconds}", f"padrelease {pad}", "status"]
    return ["status", "release", f"hold E {milliseconds}", "release", "status"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pipe", default="xml2-fix-input")
    parser.add_argument("--device", choices=("keyboard", "pad"), required=True)
    parser.add_argument("--pad", type=int, choices=range(1, 5), default=1)
    parser.add_argument("--ms", type=int, choices=range(300, 801), default=500)
    parser.add_argument("--execute", action="store_true")
    args = parser.parse_args()
    if not args.pipe or len(args.pipe) > 64 or any(not (c.isascii() and (c.isalnum() or c in "-_")) for c in args.pipe):
        parser.error("pipe must be the configured PipeName (letters/digits/-/_, up to 64)")
    plan = commands(args.device, args.ms, args.pad)
    if not args.execute:
        print(json.dumps({"execute": False, "pipe": args.pipe, "commands": plan}, indent=2))
        return
    with open("\\\\.\\pipe\\" + args.pipe, "r+b", buffering=0) as pipe:
        try:
            for command in plan:
                pipe.write((command + "\n").encode("ascii"))
                reply = bytearray()
                while not reply.endswith(b"\n"):
                    char = pipe.read(1)
                    if not char:
                        raise RuntimeError("test pipe closed before its reply")
                    reply += char
                answer = reply.decode("utf-8", errors="replace").strip()
                print(json.dumps({"command": command, "reply": answer}), flush=True)
                if answer.startswith("error"):
                    raise RuntimeError(answer)
        finally:
            # The timed commands already release; also release on client errors.
            try:
                pipe.write(((f"padrelease {args.pad}" if args.device == "pad" else "release") + "\n").encode("ascii"))
            except OSError:
                pass
    time.sleep(.75)  # leave distinct presses for an external repeated-run harness


if __name__ == "__main__":
    main()
