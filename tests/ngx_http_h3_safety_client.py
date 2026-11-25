#!/usr/bin/env python3
"""Minimal aioquic-based HTTP/3 regression client for the safety test module."""

import argparse
import asyncio
import ssl
import sys

from aioquic.asyncio.client import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3_ALPN, H3Connection
from aioquic.h3.events import DataReceived, HeadersReceived
from aioquic.quic.configuration import QuicConfiguration


class SafetyClient(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._http = H3Connection(self._quic)
        self._waiters = {}
        self._responses = {}

    def quic_event_received(self, event):
        for http_event in self._http.handle_event(event):
            if not isinstance(http_event, (HeadersReceived, DataReceived)):
                continue

            response = self._responses.setdefault(
                http_event.stream_id, {"headers": [], "body": bytearray()}
            )

            if isinstance(http_event, HeadersReceived):
                response["headers"].extend(http_event.headers)
            else:
                response["body"].extend(http_event.data)

            if http_event.stream_ended:
                waiter = self._waiters.get(http_event.stream_id)
                if waiter is not None and not waiter.done():
                    waiter.set_result(response)

    async def get(self, path):
        stream_id = self._quic.get_next_available_stream_id()
        waiter = asyncio.get_running_loop().create_future()
        self._waiters[stream_id] = waiter
        self._responses[stream_id] = {"headers": [], "body": bytearray()}

        self._http.send_headers(
            stream_id,
            [
                (b":method", b"GET"),
                (b":scheme", b"https"),
                (b":authority", b"localhost"),
                (b":path", path.encode("ascii")),
                (b"user-agent", b"nginx-safety-test"),
            ],
            end_stream=True,
        )
        self.transmit()

        try:
            return await asyncio.wait_for(waiter, timeout=15)
        finally:
            self._waiters.pop(stream_id, None)
            self._responses.pop(stream_id, None)


async def run(host, port):
    configuration = QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN)
    configuration.verify_mode = ssl.CERT_NONE
    failures = 0

    endpoints = [
        ("normal", 200),
        ("direct-503", 503),
        ("pool-overflow", 503),
        ("array-overflow", 503),
        ("list-overflow", 503),
        ("thread-overflow", 503),
        ("header-overflow", 503),
    ]

    print("protocol,endpoint,expected,actual")
    async with connect(
        host, port, configuration=configuration, create_protocol=SafetyClient
    ) as client:
        for endpoint, expected in endpoints:
            try:
                response = await client.get("/" + endpoint)
                statuses = [
                    value.decode("ascii")
                    for name, value in response["headers"]
                    if name == b":status"
                ]
                actual = statuses[0] if statuses else "missing"
            except Exception as exc:
                actual = "error:" + type(exc).__name__

            print(f"http3,{endpoint},{expected},{actual}", flush=True)
            if actual != str(expected):
                failures += 1

    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18443)
    args = parser.parse_args()

    failures = asyncio.run(run(args.host, args.port))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
