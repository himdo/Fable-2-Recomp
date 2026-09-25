#!/usr/bin/env python3
"""fable2_control.py - CLI client for the Fable 2 remote control server.

Sends JSON-lines commands to the fable_2.exe control server (default
127.0.0.1:8791) so a human or an AI harness can drive the guest gamepad.
See src/remote_control_server.h and plans/ai-remote-input-control.md for the
full protocol. Stdlib only.

Examples:
  python tools/fable2_control.py ping
  python tools/fable2_control.py press A --hold 120
  python tools/fable2_control.py state --buttons A,RT --ly 1000
  python tools/fable2_control.py get-state
  python tools/fable2_control.py cvar mouse_look_scale
  python tools/fable2_control.py cvar set mouse_look_scale 512
  python tools/fable2_control.py script --file repro.json
  python tools/fable2_control.py raw '{"cmd":"info"}'

repro.json for `script` is either a bare list of steps or {"steps": [...]}:
  [
    {"delay_ms": 500, "op": "press", "input": "LB", "hold_ms": 2000},
    {"delay_ms": 2100, "op": "press", "input": "A", "hold_ms": 80}
  ]

Each command exits 0 on {"ok":true} and 1 on error (the response JSON is
printed either way, so shell-driven loops can parse it).
"""

import argparse
import json
import socket
import sys

INPUT_NAMES = (
    "A B X Y LB RB Up Down Left Right Start Back L3 R3 "
    "LT RT StkLx StkLy StkRx StkRy "
    "StickUp StickDown StickLeft StickRight"
).split()


def conn(host: str, port: int, timeout: float = 10.0) -> socket.socket:
    s = socket.create_connection((host, port), timeout=timeout)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def exchange(sock: socket.socket, request: dict) -> dict:
    """Send one command line, read exactly one response line."""
    sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("server closed the connection")
        buf += chunk
    return json.loads(buf.decode("utf-8").strip())


def run(host: str, port: int, token: str, request: dict) -> int:
    try:
        with conn(host, port) as s:
            if token:
                resp = exchange(s, {"cmd": "auth", "token": token})
                if not resp.get("ok"):
                    print(json.dumps(resp))
                    return 1
            resp = exchange(s, request)
    except OSError as e:
        print(json.dumps({"ok": False, "error": str(e)}))
        return 1
    print(json.dumps(resp))
    return 0 if resp.get("ok") else 1


def build_state(args) -> dict:
    req = {"cmd": "state"}
    if args.buttons:
        req["buttons"] = [b for b in args.buttons.split(",") if b]
    if args.lt is not None or args.rt is not None:
        req["triggers"] = {}
        if args.lt is not None:
            req["triggers"]["LT"] = args.lt
        if args.rt is not None:
            req["triggers"]["RT"] = args.rt
    if any(v is not None for v in (args.lx, args.ly, args.rx, args.ry)):
        req["stk"] = {}
        for key, val in (("lx", args.lx), ("ly", args.ly),
                         ("rx", args.rx), ("ry", args.ry)):
            if val is not None:
                req["stk"][key] = val
    return req


def main() -> int:
    p = argparse.ArgumentParser(
        description="Drive the Fable 2 recomp gamepad over the remote "
                    "control server (JSON-lines over localhost TCP).")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8791)
    p.add_argument("--token", default="", help="shared token (if configured)")
    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("ping", help="liveness probe")
    sub.add_parser("info", help="server info (port, pad status)")

    sp = sub.add_parser("press", help="press an input (hold by default)")
    sp.add_argument("input", choices=INPUT_NAMES)
    sp.add_argument("--hold", type=int, default=0,
                    help="auto-release after N ms (default: hold until release/clear)")
    sp.add_argument("--value", type=int, default=None,
                    help="trigger value 0-255 or stick value -32767..32767")

    sp = sub.add_parser("release", help="release an input")
    sp.add_argument("input", choices=INPUT_NAMES)

    sp = sub.add_parser("stick", help="set a stick axis for a duration")
    sp.add_argument("input", choices=["StkLx", "StkLy", "StkRx", "StkRy",
                                      "StickUp", "StickDown", "StickLeft",
                                      "StickRight"])
    sp.add_argument("value", type=int)
    sp.add_argument("--hold", type=int, default=16,
                    help="ms to hold (default 16 ~= one frame at 60 Hz)")

    sp = sub.add_parser("state",
                        help="replace the sticky baseline (omitted parts reset)")
    sp.add_argument("--buttons", default=None,
                    help="comma-separated, e.g. A,RT")
    sp.add_argument("--lt", type=int, default=None)
    sp.add_argument("--rt", type=int, default=None)
    sp.add_argument("--lx", type=int, default=None)
    sp.add_argument("--ly", type=int, default=None)
    sp.add_argument("--rx", type=int, default=None)
    sp.add_argument("--ry", type=int, default=None)

    sub.add_parser("clear", help="release everything remote")
    sub.add_parser("get-state", help="current pad state + pending releases")
    sub.add_parser("game-state",
                   help="current boot/menu state (PreMainMenu / PressAScreen / "
                        "MainMenuMovie / MainMenu / ?)")

    sp = sub.add_parser("cvar", help="get or set a cvar by name")
    sp.add_argument("name")
    sp.add_argument("value", nargs="?", default=None)

    sub.add_parser("enable", help="enable the remote pad")
    sub.add_parser("disable", help="disable the remote pad (zero state)")

    sp = sub.add_parser("script", help="run a timed input sequence (atomic)")
    sp.add_argument("--file", required=True,
                    help="JSON file: list of steps or {'steps': [...]}")

    sp = sub.add_parser("raw", help="send a raw JSON command object")
    sp.add_argument("json")

    args = p.parse_args()

    if args.command == "ping":
        req = {"cmd": "ping"}
    elif args.command == "info":
        req = {"cmd": "info"}
    elif args.command == "press":
        req = {"cmd": "press", "input": args.input}
        if args.hold > 0:
            req["hold_ms"] = args.hold
        if args.value is not None:
            req["value"] = args.value
    elif args.command == "release":
        req = {"cmd": "release", "input": args.input}
    elif args.command == "stick":
        req = {"cmd": "stick", "input": args.input, "value": args.value,
               "hold_ms": args.hold}
    elif args.command == "state":
        req = build_state(args)
    elif args.command == "clear":
        req = {"cmd": "clear"}
    elif args.command == "get-state":
        req = {"cmd": "get_state"}
    elif args.command == "game-state":
        req = {"cmd": "game_state"}
    elif args.command == "cvar":
        req = {"cmd": "cvar", "name": args.name}
        if args.value is not None:
            req["value"] = args.value
    elif args.command in ("enable", "disable"):
        req = {"cmd": args.command}
    elif args.command == "script":
        with open(args.file, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict) and "steps" in data:
            data = data["steps"]
        req = {"cmd": "script", "steps": data}
    elif args.command == "raw":
        req = json.loads(args.json)
    else:  # pragma: no cover
        p.error(f"unknown command {args.command}")

    return run(args.host, args.port, args.token, req)


if __name__ == "__main__":
    sys.exit(main())
