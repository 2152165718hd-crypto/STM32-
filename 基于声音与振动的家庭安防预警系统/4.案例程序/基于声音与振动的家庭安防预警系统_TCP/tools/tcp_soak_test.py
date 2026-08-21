#!/usr/bin/env python3
"""Short protocol soak test for the TCP upper/device data-refresh path."""

from __future__ import annotations

import json
import socket
import struct
import threading
import time


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


def build_frame(msg_type: int, seq: int, body: dict | None = None) -> bytes:
    payload = json.dumps(body or {}, separators=(",", ":")).encode("utf-8")
    header = MAGIC + bytes([VERSION, HEADER_SIZE]) + struct.pack(">HBBII", msg_type, 0, 0, seq, len(payload))
    return header + payload


class Parser:
    def __init__(self) -> None:
        self.buf = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, int, dict]]:
        self.buf.extend(data)
        frames: list[tuple[int, int, dict]] = []
        while len(self.buf) >= HEADER_SIZE:
            if self.buf[:2] != MAGIC:
                idx = self.buf.find(MAGIC, 1)
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
            frames.append((msg_type, seq, json.loads(raw.decode("utf-8") or "{}")))
        return frames


def status_body(counter: int) -> dict:
    return {
        "device_id": "HOME_SECURITY_001",
        "tick": counter,
        "state": "ARMED",
        "state_code": 2,
        "armed": 1,
        "silenced": 0,
        "reason": "NONE",
        "last_alarm_tick": 0,
        "audio": {"freq_hz": 1560, "energy": 3000, "ratio_pct": 40, "high_ratio_pct": 20, "rms_mv": 60, "score": 39, "detect_score": 45},
        "vibration": {"freq_hz": 250, "peak_mv": 300, "energy": 400, "zero_cross_permille": 100, "score": 6, "detect_score": 35},
        "config": {"alarm_hold_ms": 15000, "fusion_window_ms": 300, "audio_medium_ratio_pct": 40, "audio_strong_ratio_pct": 55},
        "wifi_ready": 1,
        "clients": 1,
        "tcp": {"link_mask": 1, "active_link": 0, "logged_in": 1, "tx_fail_count": 0, "last_tx_status": 0, "blocked_ms": 0},
    }


def device_server(port_holder: list[int], stop_event: threading.Event) -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    port_holder.append(server.getsockname()[1])
    server.settimeout(0.2)

    try:
        conn, _ = server.accept()
    except socket.timeout:
        server.close()
        return

    parser = Parser()
    conn.settimeout(0.05)
    logged_in = False
    seq = 1000
    status_count = 0
    last_push = time.monotonic()
    first_push_after_login = 0.35
    login_tick = time.monotonic()

    try:
        while not stop_event.is_set():
            now = time.monotonic()
            if logged_in and (now - login_tick >= first_push_after_login) and (now - last_push >= 1.0):
                status_count += 1
                seq += 1
                conn.sendall(build_frame(STATUS_PUSH, seq, status_body(status_count)))
                last_push = now

            try:
                data = conn.recv(2048)
            except socket.timeout:
                continue
            if not data:
                break

            for msg_type, rx_seq, _body in parser.feed(data):
                if msg_type == LOGIN_REQ:
                    logged_in = True
                    login_tick = time.monotonic()
                    last_push = login_tick - 0.70
                    conn.sendall(build_frame(LOGIN_RSP, rx_seq, {"ok": True, "device_id": "HOME_SECURITY_001", "protocol": 1}))
                elif msg_type == PING:
                    conn.sendall(build_frame(PONG, rx_seq, {"ok": True, "tick": int(time.monotonic() * 1000)}))
                elif msg_type == STATUS_QUERY:
                    status_count += 1
                    conn.sendall(build_frame(STATUS_RSP, rx_seq, status_body(status_count)))
    finally:
        conn.close()
        server.close()


def main() -> None:
    stop_event = threading.Event()
    port_holder: list[int] = []
    worker = threading.Thread(target=device_server, args=(port_holder, stop_event), daemon=True)
    worker.start()

    deadline = time.monotonic() + 3.0
    while not port_holder and time.monotonic() < deadline:
        time.sleep(0.01)
    if not port_holder:
        raise SystemExit("server did not start")

    parser = Parser()
    client = socket.create_connection(("127.0.0.1", port_holder[0]), timeout=3.0)
    client.settimeout(0.2)
    client.sendall(build_frame(LOGIN_REQ, 1, {"client_type": "pc", "client_id": "PC_TcpUpper", "protocol": 1}))

    logged_in = False
    data_frames = 0
    max_data_gap = 0.0
    last_data_at: float | None = None
    last_ping = time.monotonic()
    last_probe = 0.0
    end = time.monotonic() + 10.0

    try:
        while time.monotonic() < end:
            now = time.monotonic()
            if logged_in and now - last_ping >= 5.0:
                client.sendall(build_frame(PING, 100, {}))
                last_ping = now

            if logged_in and last_data_at is not None and now - last_data_at >= 3.0 and now - last_probe >= 3.0:
                client.sendall(build_frame(STATUS_QUERY, 101, {}))
                last_probe = now

            try:
                data = client.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break

            for msg_type, _seq, _body in parser.feed(data):
                if msg_type == LOGIN_RSP:
                    logged_in = True
                elif msg_type in (STATUS_PUSH, STATUS_RSP):
                    data_frames += 1
                    if last_data_at is not None:
                        max_data_gap = max(max_data_gap, time.monotonic() - last_data_at)
                    last_data_at = time.monotonic()

    finally:
        stop_event.set()
        client.close()

    if not logged_in:
        raise SystemExit("login response not received")
    if data_frames < 7:
        raise SystemExit(f"too few data frames: {data_frames}")
    if max_data_gap > 2.5:
        raise SystemExit(f"data gap too large: {max_data_gap:.2f}s")

    print(f"PASS tcp soak: data_frames={data_frames}, max_gap={max_data_gap:.2f}s")


if __name__ == "__main__":
    main()
