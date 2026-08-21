#!/usr/bin/env python3
"""Local TCP simulator for PC_TcpUpper and Android_TcpUpper."""

from __future__ import annotations

import argparse
import json
import random
import socket
import struct
import threading
import time
from dataclasses import dataclass, field


MAGIC = b"HS"
VERSION = 1
HEADER_SIZE = 16

LOGIN_REQ = 0x0001
LOGIN_RSP = 0x0002
PING = 0x0003
PONG = 0x0004
STATUS_QUERY = 0x0010
STATUS_RSP = 0x0011
STATUS_PUSH = 0x0012
AUDIO_REPORT = 0x0020
VIBRATION_REPORT = 0x0021
ALARM_REPORT = 0x0030
CONFIG_SET = 0x0040
CONFIG_RSP = 0x0041
CONTROL_CMD = 0x0050
CONTROL_RSP = 0x0051
HISTORY_QUERY = 0x0060
HISTORY_RSP = 0x0061
ERROR_RSP = 0x00FF


def make_frame(msg_type: int, seq: int, body: dict | None = None) -> bytes:
    payload = json.dumps(body or {}, separators=(",", ":")).encode("utf-8")
    header = MAGIC + bytes([VERSION, HEADER_SIZE]) + struct.pack(">HBBII", msg_type, 0, 0, seq, len(payload))
    return header + payload


class FrameParser:
    def __init__(self) -> None:
        self.buf = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, int, dict]]:
        self.buf.extend(data)
        out: list[tuple[int, int, dict]] = []
        while len(self.buf) >= HEADER_SIZE:
            if self.buf[0:2] != MAGIC:
                idx = self.buf.find(MAGIC)
                if idx < 0:
                    self.buf.clear()
                    break
                del self.buf[:idx]
                continue
            if self.buf[2] != VERSION or self.buf[3] != HEADER_SIZE:
                del self.buf[:1]
                continue
            msg_type = int.from_bytes(self.buf[4:6], "big")
            seq = int.from_bytes(self.buf[8:12], "big")
            body_len = int.from_bytes(self.buf[12:16], "big")
            frame_len = HEADER_SIZE + body_len
            if len(self.buf) < frame_len:
                break
            raw = bytes(self.buf[HEADER_SIZE:frame_len])
            del self.buf[:frame_len]
            out.append((msg_type, seq, json.loads(raw.decode("utf-8") or "{}")))
        return out


@dataclass
class DeviceState:
    armed: int = 1
    silenced: int = 0
    state: str = "ARMED"
    state_code: int = 2
    reason: str = "NONE"
    last_alarm_tick: int = 0
    alarm_hold_ms: int = 15000
    fusion_window_ms: int = 300
    audio_medium_ratio_pct: int = 40
    audio_strong_ratio_pct: int = 55
    history: list[dict] = field(default_factory=list)

    def status(self) -> dict:
        tick = int(time.monotonic() * 1000)
        audio_energy = 2000 + random.randint(0, 700)
        vibration_energy = 300 + random.randint(0, 120)
        return {
            "device_id": "HOME_SECURITY_001",
            "tick": tick,
            "state": self.state,
            "state_code": self.state_code,
            "armed": self.armed,
            "silenced": self.silenced,
            "reason": self.reason,
            "last_alarm_tick": self.last_alarm_tick,
            "audio": {"freq_hz": 850, "energy": audio_energy, "ratio_pct": 35, "rms_mv": 62},
            "vibration": {"freq_hz": 120, "peak_mv": 410, "energy": vibration_energy, "zero_cross_permille": 120},
            "config": {
                "alarm_hold_ms": self.alarm_hold_ms,
                "fusion_window_ms": self.fusion_window_ms,
                "audio_medium_ratio_pct": self.audio_medium_ratio_pct,
                "audio_strong_ratio_pct": self.audio_strong_ratio_pct,
            },
            "wifi_ready": 1,
            "clients": 1,
        }

    def control(self, cmd: str) -> None:
        if cmd == "arm":
            self.armed = 1
            self.state = "ARMED"
            self.state_code = 2
        elif cmd == "disarm":
            self.armed = 0
            self.state = "DISARMED"
            self.state_code = 0
        elif cmd == "silence":
            self.silenced = 1
        elif cmd == "clear_alarm":
            self.reason = "NONE"
            self.last_alarm_tick = 0
            self.state = "ARMED" if self.armed else "DISARMED"
            self.state_code = 2 if self.armed else 0

    def make_alarm(self, reason: str = "SIM_ALARM") -> dict:
        tick = int(time.monotonic() * 1000)
        self.reason = reason
        self.last_alarm_tick = tick
        self.state = "ALARM"
        self.state_code = 4
        item = {"t": tick, "r": reason, "s": self.state_code, "af": 980, "ae": 5800, "ar": 76, "vf": 140, "vp": 960, "ve": 1800}
        self.history.append(item)
        self.history = self.history[-16:]
        return {
            "tick": tick,
            "reason": reason,
            "state": self.state,
            "state_code": self.state_code,
            "audio": {"freq_hz": 980, "energy": 5800, "ratio_pct": 76},
            "vibration": {"freq_hz": 140, "peak_mv": 960, "energy": 1800},
        }


def client_thread(conn: socket.socket, addr: tuple[str, int], state: DeviceState) -> None:
    print(f"client {addr} connected")
    parser = FrameParser()
    seq = 1000
    last_push = time.monotonic()
    last_alarm = time.monotonic()
    conn.settimeout(0.2)
    try:
        while True:
            now = time.monotonic()
            if now - last_push >= 1.0:
                seq += 1
                conn.sendall(make_frame(STATUS_PUSH, seq, state.status()))
                last_push = now

            if now - last_alarm >= 15.0:
                seq += 1
                conn.sendall(make_frame(ALARM_REPORT, seq, state.make_alarm()))
                last_alarm = now

            try:
                data = conn.recv(2048)
            except socket.timeout:
                continue
            if not data:
                break
            for msg_type, rx_seq, body in parser.feed(data):
                print(f"RX type=0x{msg_type:04X} seq={rx_seq} body={body}")
                if msg_type == LOGIN_REQ:
                    conn.sendall(make_frame(LOGIN_RSP, rx_seq, {
                        "ok": True,
                        "device_id": "HOME_SECURITY_001",
                        "protocol": 1,
                        "heartbeat_ms": 10000,
                        "server_ip": "192.168.4.1",
                        "server_port": 5000,
                    }))
                elif msg_type == PING:
                    conn.sendall(make_frame(PONG, rx_seq, {"ok": True, "tick": int(time.monotonic() * 1000)}))
                elif msg_type == STATUS_QUERY:
                    conn.sendall(make_frame(STATUS_RSP, rx_seq, state.status()))
                elif msg_type == CONFIG_SET:
                    for key in ("alarm_hold_ms", "fusion_window_ms", "audio_medium_ratio_pct", "audio_strong_ratio_pct"):
                        if key in body:
                            setattr(state, key, int(body[key]))
                    conn.sendall(make_frame(CONFIG_RSP, rx_seq, state.status()))
                elif msg_type == CONTROL_CMD:
                    state.control(str(body.get("cmd", "")))
                    conn.sendall(make_frame(CONTROL_RSP, rx_seq, state.status()))
                elif msg_type == HISTORY_QUERY:
                    conn.sendall(make_frame(HISTORY_RSP, rx_seq, {"count": len(state.history), "items": state.history}))
                else:
                    conn.sendall(make_frame(ERROR_RSP, rx_seq, {"code": "UNKNOWN_TYPE", "detail": msg_type}))
    finally:
        conn.close()
        print(f"client {addr} disconnected")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    args = parser.parse_args()

    state = DeviceState()
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(5)
    print(f"simulator listening on {args.host}:{args.port}")

    try:
        while True:
            conn, addr = server.accept()
            threading.Thread(target=client_thread, args=(conn, addr, state), daemon=True).start()
    except KeyboardInterrupt:
        print("bye")
    finally:
        server.close()


if __name__ == "__main__":
    main()
