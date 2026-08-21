from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from app.protocol import (
    BinaryFrame,
    DEVICE_ADDR_DEFAULT,
    CMD_STATUS_QUERY,
    MAX_PAYLOAD,
    StreamParser,
    build_legacy_command,
    crc16_ccitt,
    decode_record_entry,
    decode_status_snapshot,
    encode_binary_frame,
    encode_locker_control_payload,
    legacy_snapshot_from_json,
)


class ProtocolTests(unittest.TestCase):
    def test_binary_round_trip(self) -> None:
        payload = encode_locker_control_payload(0x00FF, 0x01, 0x02)
        frame = encode_binary_frame(DEVICE_ADDR_DEFAULT, CMD_STATUS_QUERY, 0x1234, payload)
        parser = StreamParser()
        events = parser.feed(frame)
        self.assertEqual(len(events), 1)
        parsed = events[0]
        self.assertIsInstance(parsed, BinaryFrame)
        self.assertEqual(parsed.addr, DEVICE_ADDR_DEFAULT)
        self.assertEqual(parsed.cmd, CMD_STATUS_QUERY)
        self.assertEqual(parsed.seq, 0x1234)
        self.assertEqual(parsed.payload, payload)

    def test_half_packet(self) -> None:
        payload = b"\x01\x02\x03"
        frame = encode_binary_frame(DEVICE_ADDR_DEFAULT, CMD_STATUS_QUERY, 1, payload)
        parser = StreamParser()
        first = parser.feed(frame[:5])
        second = parser.feed(frame[5:])
        self.assertEqual(first, [])
        self.assertEqual(len(second), 1)

    def test_back_to_back_frames(self) -> None:
        f1 = encode_binary_frame(DEVICE_ADDR_DEFAULT, CMD_STATUS_QUERY, 1, b"")
        f2 = encode_binary_frame(DEVICE_ADDR_DEFAULT, CMD_STATUS_QUERY, 2, b"\x10")
        parser = StreamParser()
        events = parser.feed(f1 + f2)
        self.assertEqual(len(events), 2)

    def test_crc_rejects_frame(self) -> None:
        frame = bytearray(encode_binary_frame(DEVICE_ADDR_DEFAULT, CMD_STATUS_QUERY, 1, b"\x10"))
        frame[-3] ^= 0x01
        parser = StreamParser()
        self.assertEqual(parser.feed(bytes(frame)), [])

    def test_legacy_json_line(self) -> None:
        line = build_legacy_command("status_query", seq=3)
        parser = StreamParser()
        events = parser.feed(line)
        self.assertEqual(len(events), 1)
        legacy = events[0]
        self.assertEqual(legacy.obj["cmd"], "status_query")
        self.assertEqual(legacy.obj["seq"], 3)

    def test_helpers(self) -> None:
        snap = decode_status_snapshot(
            b"\x00\x0F\x00\xF0\x00\x03\x01\x04\x00\x12\x00\x00\x01\x02\x03\x04\x01\x00\x01"
        )
        self.assertEqual(snap.occupied_mask, 0x000F)
        self.assertEqual(snap.relay_mask, 0x00F0)

    def test_record_entry_decode(self) -> None:
        record = decode_record_entry(b"\x06\x02\xFF\xFF\x00\x18\x05\x06\x07\x08\x09")
        self.assertEqual(record.op, 6)
        self.assertEqual(record.locker_id, 2)
        self.assertEqual(record.face_id, -1)
        self.assertEqual(record.result, 0)
        self.assertEqual(record.timestamp, "2024-05-06 07:08:09")

    def test_legacy_snapshot_prefers_protocol_mode(self) -> None:
        snapshot = legacy_snapshot_from_json({"protocol_mode": 1, "proto": 2, "occupied_mask": 3})
        self.assertEqual(snapshot.protocol_mode, 1)
        self.assertEqual(snapshot.occupied_mask, 3)


if __name__ == "__main__":
    unittest.main()
