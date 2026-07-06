#!/usr/bin/env python3
"""WebSocket telemetry smoke check.

Connects to the live-dashboard WebSocket bridge and asserts that a
telemetry snapshot arrives within a deadline. Exit code 0 = healthy,
1 = no telemetry (bridge down, MQTT starved, or endpoint unreachable).

Added per the 2026-05-14 ws-bridge outage handoff: the dashboard can be
"up" (HTTP 200) while the WS stream is silently dead — this probes the
actual data path.

Usage:
    python scripts/ws_smoke.py                        # default: public SIL host
    python scripts/ws_smoke.py --url ws://localhost:8095/ws/telemetry
    python scripts/ws_smoke.py --timeout 10

Requires: websockets (pip install websockets)
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys

DEFAULT_URL = "wss://sil.taktflow-systems.com/ws/telemetry"


async def probe(url: str, timeout: float) -> int:
    try:
        import websockets
    except ImportError:
        print("FAIL: python package 'websockets' not installed", file=sys.stderr)
        return 1

    try:
        async with websockets.connect(url, open_timeout=timeout) as ws:
            raw = await asyncio.wait_for(ws.recv(), timeout=timeout)
    except Exception as exc:  # connection refused, TLS, timeout, ...
        print(f"FAIL: no telemetry within {timeout:.0f}s from {url}: {exc}",
              file=sys.stderr)
        return 1

    try:
        snapshot = json.loads(raw)
    except (json.JSONDecodeError, UnicodeDecodeError):
        print(f"FAIL: first frame from {url} is not JSON", file=sys.stderr)
        return 1

    keys = ", ".join(sorted(snapshot)[:8]) if isinstance(snapshot, dict) else type(snapshot).__name__
    print(f"OK: telemetry snapshot received from {url} (keys: {keys})")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--url", default=DEFAULT_URL,
                        help=f"WebSocket endpoint (default: {DEFAULT_URL})")
    parser.add_argument("--timeout", type=float, default=10.0,
                        help="Seconds to wait for the first snapshot (default: 10)")
    args = parser.parse_args()
    return asyncio.run(probe(args.url, args.timeout))


if __name__ == "__main__":
    sys.exit(main())
