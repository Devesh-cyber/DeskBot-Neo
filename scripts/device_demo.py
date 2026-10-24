#!/usr/bin/env python3
"""Talk to the Nova display from the laptop.   Run from the repo root.

  python3 scripts/device_demo.py status
  python3 scripts/device_demo.py cycle [--seconds 4]
  python3 scripts/device_demo.py set working --text "Running tests" --step 2 --steps 5
  python3 scripts/device_demo.py set error --text "USB drive not found"
  python3 scripts/device_demo.py heartbeat
  python3 scripts/device_demo.py watch        # heartbeat every 5 s until Ctrl+C
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))
from nova.config import ConfigError, load_config  # noqa: E402
from nova.device import STATES, DeviceClient, DeviceError, Heartbeat  # noqa: E402

DEMO = [  # (state, text, step, steps)
    ("idle", "", None, None),
    ("listening", "", None, None),
    ("thinking", "", None, None),
    ("speaking", "", None, None),
    ("working", "Running tests", 2, 5),
    ("saved", "Saved", None, None),
    ("error", "USB drive not found", None, None),
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    sub.add_parser("heartbeat")
    sub.add_parser("watch")
    c = sub.add_parser("cycle")
    c.add_argument("--seconds", type=float, default=4.0)
    s = sub.add_parser("set")
    s.add_argument("state", choices=STATES)
    s.add_argument("--text", default="")
    s.add_argument("--ttl-ms", type=int)
    s.add_argument("--step", type=int)
    s.add_argument("--steps", type=int)
    args = ap.parse_args()

    try:
        cfg = load_config()
    except ConfigError as exc:
        print(f"config error: {exc}")
        return 1
    client = DeviceClient.from_config(cfg)
    print(f"display: {cfg.device.address}:{cfg.device.port}")

    try:
        if args.cmd == "status":
            print(client.health())
        elif args.cmd == "heartbeat":
            print(client.heartbeat())
        elif args.cmd == "set":
            print(client.set_state(args.state, args.text, args.ttl_ms, args.step, args.steps))
        elif args.cmd == "cycle":
            ttl = int(args.seconds * 1000) + 3000
            for state, text, step, steps in DEMO:
                print(f"-> {state} {text!r}")
                client.set_state(state, text, ttl_ms=ttl if state != "idle" else None, step=step, steps=steps)
                time.sleep(args.seconds)
            client.set_state("idle")
            print("back to idle")
        elif args.cmd == "watch":
            hb = Heartbeat(client, 5.0, on_change=lambda ok: print("connected" if ok else "NOT reachable"))
            hb.start()
            print("sending a heartbeat every 5 s. Press Ctrl+C to stop; the face goes to 'disconnected' ~15 s later.")
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                hb.stop()
    except (DeviceError, ValueError) as exc:
        print(f"error: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
