#!/usr/bin/env python3
"""Offline tests for the Home Security TCP frame parser."""

from __future__ import annotations

import json
import struct


MAGIC = b"HS"
VERSION = 1
HEADER_SIZE = 16
MAX_BODY = 640

LOGIN_REQ = 0x0001
PING = 0x0003
STATUS_QUERY = 0x0010


class Parser:
    def __init__(self) -> None:
        self.buf = bytearray()
        self.errors: list[tuple[int, str]] = []

    def feed(self, data: bytes) -> list[tuple[int, int, dict]]:
        self.buf.extend(data)
        out: list[tuple[int, int, dict]] = []

        while len(self.buf) >= HEADER_SIZE:
            if self.buf[0:2] != MAGIC:
                idx = self.buf.find(MAGIC)
                self.errors.append((0, "BAD_MAGIC"))
                if idx < 0:
                    keep = self.buf[-1:] == MAGIC[:1]
                    self.buf.clear()
                    if keep:
                        self.buf.extend(MAGIC[:1])
                else:
                    del self.buf[:idx]
                continue

            version = self.buf[2]
            header_len = self.buf[3]
            if version != VERSION or header_len != HEADER_SIZE:
                self.errors.append((0, "UNSUPPORTED_VERSION"))
                del self.buf[:1]
                continue

            msg_type = int.from_bytes(self.buf[4:6], "big")
            seq = int.from_bytes(self.buf[8:12], "big")
            body_len = int.from_bytes(self.buf[12:16], "big")

            if body_len > MAX_BODY:
                self.errors.append((seq, "FRAME_TOO_LARGE"))
                self.buf.clear()
                continue

            frame_len = HEADER_SIZE + body_len
            if len(self.buf) < frame_len:
                break

            raw = bytes(self.buf[HEADER_SIZE:frame_len])
            del self.buf[:frame_len]
            body = json.loads(raw.decode("utf-8") or "{}")
            out.append((msg_type, seq, body))

        return out


def frame(msg_type: int, seq: int, body: dict | None = None) -> bytes:
    payload = json.dumps(body or {}, separators=(",", ":")).encode("utf-8")
    return MAGIC + bytes([VERSION, HEADER_SIZE]) + struct.pack(">HBBII", msg_type, 0, 0, seq, len(payload)) + payload


def test_complete_frame() -> None:
    parser = Parser()
    frames = parser.feed(frame(LOGIN_REQ, 1, {"client_type": "pc"}))
    assert frames == [(LOGIN_REQ, 1, {"client_type": "pc"})]
    assert not parser.errors


def test_half_frame() -> None:
    parser = Parser()
    data = frame(PING, 2, {"tick": 1})
    assert parser.feed(data[:5]) == []
    assert parser.feed(data[5:]) == [(PING, 2, {"tick": 1})]


def test_sticky_frame() -> None:
    parser = Parser()
    frames = parser.feed(frame(PING, 3) + frame(STATUS_QUERY, 4))
    assert [x[:2] for x in frames] == [(PING, 3), (STATUS_QUERY, 4)]


def test_bad_magic_resync() -> None:
    parser = Parser()
    frames = parser.feed(b"noise" + frame(PING, 5))
    assert frames == [(PING, 5, {})]
    assert parser.errors == [(0, "BAD_MAGIC")]


def test_oversize() -> None:
    parser = Parser()
    bad = MAGIC + bytes([VERSION, HEADER_SIZE]) + struct.pack(">HBBII", PING, 0, 0, 6, MAX_BODY + 1)
    assert parser.feed(bad) == []
    assert parser.errors == [(6, "FRAME_TOO_LARGE")]
    assert parser.buf == bytearray()


def run() -> None:
    tests = [
        test_complete_frame,
        test_half_frame,
        test_sticky_frame,
        test_bad_magic_resync,
        test_oversize,
    ]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
    print(f"{len(tests)} protocol parser tests passed.")


if __name__ == "__main__":
    run()
