#!/usr/bin/env python3
"""Python websockets echo server — asyncio WS stack for comparison."""
from __future__ import annotations

import asyncio
import sys

import websockets


async def echo(ws):
    async for message in ws:
        await ws.send(message)


async def main() -> None:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 19182
    async with websockets.serve(echo, "127.0.0.1", port, ping_interval=None):
        print(f"python websockets echo on {port} (/)", file=sys.stderr, flush=True)
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
