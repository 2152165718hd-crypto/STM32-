from __future__ import annotations

import binascii
import json
import struct
from dataclasses import dataclass
from enum import Enum
from typing import Any


SOF = b"\xAA\x55"
EOF_MARK = b"\x55\xAA"
PROTOCOL_VERSION = 1
DEVICE_ADDR_DEFAULT = 0x01
MAX_PAYLOAD = 256

CMD_HEARTBEAT = 0x01
CMD_STATUS_QUERY = 0x10
CMD_LOCKER_CONTROL = 0x20
CMD_CLEAR_ALARM = 0x30
CMD_EMERGENCY_UNLOCK = 0x31
CMD_RECORD_EXPORT = 0x40
CMD_SET_TIME = 0x50
CMD_RESPONSE_MASK = 0x80

LOCKER_ACTION_OPEN = 0x01
LOCKER_ACTION_CLOSE = 0x02
LOCKER_ACTION_TOGGLE = 0x03
LOCKER_ACTION_FORCE_OPEN = 0x04

STATUS_PAYLOAD_STRUCT = struct.Struct(">HHHBBHIBBBB")
RECORD_PAYLOAD_STRUCT = struct.Struct(">BBhBBBBBBB")


class ProtocolMode(str, Enum):
    BINARY = "binary"
    LEGACY_JSON = "legacy_json"


class ResultCode(int, Enum):
    OK = 0
    BAD_REQUEST = 1
    INVALID_LOCKER = 2
    INVALID_ACTION = 3
    BUSY = 4
    UNSUPPORTED = 5
    TIMEOUT = 6
    CRC_ERROR = 7


def crc16_ccitt(data: bytes, initial: int = 0xFFFF) -> int:
    return binascii.crc_hqx(data, initial) & 0xFFFF


def command_name(cmd: int) -> str:
    base = cmd & 0x7F
    if base == CMD_HEARTBEAT:
        return "heartbeat"
    if base == CMD_STATUS_QUERY:
        return "status_query"
    if base == CMD_LOCKER_CONTROL:
        return "locker_control"
    if base == CMD_CLEAR_ALARM:
        return "clear_alarm"
    if base == CMD_EMERGENCY_UNLOCK:
        return "emergency_unlock"
    if base == CMD_RECORD_EXPORT:
        return "record_export"
    if base == CMD_SET_TIME:
        return "set_time"
    return f"cmd_{base:02X}"


def response_command(cmd: int) -> int:
    return cmd | CMD_RESPONSE_MASK


def frame_to_hex(data: bytes, limit: int = 128) -> str:
    sample = data[:limit]
    return " ".join(f"{byte:02X}" for byte in sample)


def binary_frame_length(payload_len: int) -> int:
    return len(SOF) + 1 + 1 + 1 + 2 + 2 + payload_len + 2 + len(EOF_MARK)


@dataclass(frozen=True)
class BinaryFrame:
    version: int
    addr: int
    cmd: int
    seq: int
    payload: bytes
    crc: int = 0

    @property
    def is_response(self) -> bool:
        return bool(self.cmd & CMD_RESPONSE_MASK)


@dataclass(frozen=True)
class DeviceSnapshot:
    occupied_mask: int
    relay_mask: int
    door_mask: int
    alarm: int
    selected_locker: int
    record_count: int
    uptime_ms: int
    wifi_connected: int
    tcp_connected: int
    protocol_mode: int
    reserved: int = 0

    @property
    def occupied_count(self) -> int:
        return int(self.occupied_mask.bit_count())

    @property
    def relay_count(self) -> int:
        return int(self.relay_mask.bit_count())

    @property
    def door_count(self) -> int:
        return int(self.door_mask.bit_count())

    def to_summary(self) -> str:
        return (
            f"occupied={self.occupied_count} relay={self.relay_count} "
            f"door={self.door_count} alarm={self.alarm} record={self.record_count}"
        )


@dataclass(frozen=True)
class RecordEntry:
    op: int
    locker_id: int
    face_id: int
    result: int
    timestamp: str

    def label(self) -> str:
        return {
            1: "存物",
            2: "取物",
            3: "管理员开箱",
            4: "清空柜体",
            5: "超时报警",
            6: "远程控制",
            7: "紧急解锁",
            8: "设时",
        }.get(self.op, f"OP-{self.op}")


@dataclass(frozen=True)
class LegacyMessage:
    raw: str
    obj: dict[str, Any] | None


def decode_status_snapshot(payload: bytes) -> DeviceSnapshot:
    if len(payload) < STATUS_PAYLOAD_STRUCT.size:
        raise ValueError("status payload too short")
    unpacked = STATUS_PAYLOAD_STRUCT.unpack_from(payload)
    return DeviceSnapshot(*unpacked)


def legacy_snapshot_from_json(obj: dict[str, Any]) -> DeviceSnapshot:
    occupied_mask = 0
    relay_mask = 0
    door_mask = 0

    lockers = obj.get("lockers")
    if isinstance(lockers, list):
        for idx, value in enumerate(lockers[:16]):
            if int(value):
                occupied_mask |= 1 << idx

    if "occupied_mask" in obj:
        occupied_mask = int(obj.get("occupied_mask", 0)) & 0xFFFF

    if "relay_mask" in obj:
        relay_mask = int(obj.get("relay_mask", 0)) & 0xFFFF
    elif int(obj.get("door_open", 0)):
        locker = int(obj.get("locker", obj.get("selected_locker", 0)))
        if 1 <= locker <= 16:
            relay_mask = 1 << (locker - 1)

    if "door_mask" in obj:
        door_mask = int(obj.get("door_mask", 0)) & 0xFFFF
    else:
        door_mask = relay_mask

    return DeviceSnapshot(
        occupied_mask=occupied_mask,
        relay_mask=relay_mask,
        door_mask=door_mask,
        alarm=int(obj.get("alarm", 0)) & 0xFF,
        selected_locker=int(obj.get("selected_locker", obj.get("locker", 0))) & 0xFF,
        record_count=int(obj.get("record_count", obj.get("records", obj.get("count", 0)))) & 0xFFFF,
        uptime_ms=int(obj.get("uptime_ms", obj.get("uptime", 0))) & 0xFFFFFFFF,
        wifi_connected=int(obj.get("wifi_connected", obj.get("ap", obj.get("wifi", 0)))) & 0xFF,
        tcp_connected=int(obj.get("tcp_connected", obj.get("client", 0))) & 0xFF,
        protocol_mode=int(
            obj.get(
                "protocol_mode",
                obj.get("protocol", obj.get("proto", 2)),
            )
        ) & 0xFF,
        reserved=0,
    )


def legacy_record_from_json(obj: dict[str, Any]) -> RecordEntry:
    timestamp = str(obj.get("time", obj.get("timestamp", "")))
    if not timestamp:
        timestamp = "1970-01-01 00:00:00"
    return RecordEntry(
        op=int(obj.get("op", obj.get("type_id", 0))) & 0xFF,
        locker_id=int(obj.get("locker", obj.get("locker_id", 0))) & 0xFF,
        face_id=int(obj.get("face", obj.get("face_id", -1))),
        result=int(obj.get("result", obj.get("ok", 0))) & 0xFF,
        timestamp=timestamp,
    )


def encode_binary_frame(addr: int, cmd: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")

    header = struct.pack(
        ">BBBHH",
        PROTOCOL_VERSION & 0xFF,
        addr & 0xFF,
        cmd & 0xFF,
        seq & 0xFFFF,
        len(payload) & 0xFFFF,
    )
    crc = crc16_ccitt(header + payload)
    return SOF + header + payload + struct.pack(">H", crc) + EOF_MARK


def build_legacy_command(cmd: str, seq: int | None = None, **fields: Any) -> bytes:
    payload: dict[str, Any] = {"type": "cmd", "cmd": cmd}
    if seq is not None:
        payload["seq"] = int(seq) & 0xFFFF
    payload.update(fields)
    return (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


def parse_legacy_message(raw: str) -> LegacyMessage:
    text = raw.strip()
    if not text:
        return LegacyMessage(raw=raw, obj=None)
    try:
        obj = json.loads(text)
    except json.JSONDecodeError:
        return LegacyMessage(raw=raw, obj=None)
    if not isinstance(obj, dict):
        return LegacyMessage(raw=raw, obj=None)
    return LegacyMessage(raw=raw, obj=obj)


class StreamParser:
    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self._state = "text"
        self._text_buf = bytearray()
        self._header_buf = bytearray()
        self._payload_buf = bytearray()
        self._crc_buf = bytearray()
        self._tail_buf = bytearray()
        self._ver = 0
        self._addr = 0
        self._cmd = 0
        self._seq = 0
        self._payload_len = 0

    def feed(self, data: bytes) -> list[Any]:
        events: list[Any] = []
        for byte in data:
            self._consume(byte, events)
        return events

    def _consume(self, byte: int, events: list[Any]) -> None:
        if self._state == "text":
            if byte == SOF[0]:
                if self._text_buf:
                    self._text_buf.clear()
                self._state = "sof2"
                self._header_buf.clear()
                self._payload_buf.clear()
                self._crc_buf.clear()
                self._tail_buf.clear()
                return

            if byte == 0x0D:
                return

            if byte == 0x0A:
                if self._text_buf:
                    raw = self._text_buf.decode("utf-8", errors="ignore").strip()
                    self._text_buf.clear()
                    if raw:
                        events.append(parse_legacy_message(raw))
                return

            if byte == 0x09 or 0x20 <= byte <= 0x7E:
                self._text_buf.append(byte)
                if len(self._text_buf) > 2048:
                    self._text_buf.clear()
            return

        if self._state == "sof2":
            if byte == SOF[1]:
                self._state = "header"
                return
            self._state = "text"
            return

        if self._state == "header":
            self._header_buf.append(byte)
            if len(self._header_buf) < 7:
                return
            try:
                self._ver, self._addr, self._cmd, self._seq, self._payload_len = struct.unpack(
                    ">BBBHH", bytes(self._header_buf)
                )
            except struct.error:
                self.reset()
                return
            if self._ver != PROTOCOL_VERSION or self._payload_len > MAX_PAYLOAD:
                self.reset()
                return
            self._payload_buf.clear()
            self._crc_buf.clear()
            self._tail_buf.clear()
            self._state = "payload" if self._payload_len > 0 else "crc"
            return

        if self._state == "payload":
            self._payload_buf.append(byte)
            if len(self._payload_buf) >= self._payload_len:
                self._state = "crc"
            return

        if self._state == "crc":
            self._crc_buf.append(byte)
            if len(self._crc_buf) < 2:
                return
            expected = crc16_ccitt(
                struct.pack(
                    ">BBBHH",
                    self._ver & 0xFF,
                    self._addr & 0xFF,
                    self._cmd & 0xFF,
                    self._seq & 0xFFFF,
                    self._payload_len & 0xFFFF,
                )
                + bytes(self._payload_buf)
            )
            received = int.from_bytes(bytes(self._crc_buf), "big")
            if received != expected:
                self.reset()
                return
            self._state = "tail"
            return

        if self._state == "tail":
            self._tail_buf.append(byte)
            if len(self._tail_buf) < 2:
                return
            if bytes(self._tail_buf) != EOF_MARK:
                self.reset()
                return
            events.append(
                BinaryFrame(
                    version=self._ver,
                    addr=self._addr,
                    cmd=self._cmd,
                    seq=self._seq,
                    payload=bytes(self._payload_buf),
                    crc=int.from_bytes(bytes(self._crc_buf), "big"),
                )
            )
            self.reset()
            return

        self.reset()


def decode_record_entry(payload: bytes) -> RecordEntry:
    if len(payload) < RECORD_PAYLOAD_STRUCT.size:
        raise ValueError("record payload too short")
    op, locker_id, face_id, result, year, month, day, hour, minute, second = RECORD_PAYLOAD_STRUCT.unpack_from(payload)
    timestamp = f"20{year:02d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}"
    return RecordEntry(op=op, locker_id=locker_id, face_id=face_id, result=result, timestamp=timestamp)


def encode_set_time_payload(year: int, month: int, day: int, hour: int, minute: int, second: int) -> bytes:
    return struct.pack(">6B", year & 0xFF, month & 0xFF, day & 0xFF, hour & 0xFF, minute & 0xFF, second & 0xFF)


def encode_locker_control_payload(locker_mask: int, action: int, flags: int = 0) -> bytes:
    return struct.pack(">HBB", locker_mask & 0xFFFF, action & 0xFF, flags & 0xFF)


def decode_result_and_snapshot(payload: bytes) -> tuple[int, DeviceSnapshot, int]:
    if len(payload) < 1 + STATUS_PAYLOAD_STRUCT.size:
        raise ValueError("result payload too short")
    result = payload[0]
    snapshot = decode_status_snapshot(payload[1:])
    return result, snapshot, 1


def format_result_code(result: int) -> str:
    try:
        return ResultCode(result).name
    except ValueError:
        return f"ERR-{result}"
