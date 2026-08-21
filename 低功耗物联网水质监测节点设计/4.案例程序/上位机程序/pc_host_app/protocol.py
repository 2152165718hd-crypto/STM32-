from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class Packet:
    kind: str
    raw: str
    data: dict[str, Any]


@dataclass(slots=True)
class TelemetryRecord:
    received_at: str
    device_id: str
    seq: int
    uptime_s: int
    temp_c: float
    ph: float
    turb_ntu: float
    alarm: int
    err: int
    raw_json: str


@dataclass(slots=True)
class ConfigRecord:
    received_at: str
    device_id: str
    sample_period_s: int
    ph_min: float
    ph_max: float
    temp_min: float
    temp_max: float
    turb_max: float
    ph_k: float
    ph_b: float
    turb_a: float
    turb_b: float
    turb_c: float
    raw_json: str


def _as_int(value: Any, default: int = 0) -> int:
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def _as_float(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def build_json_command(cmd: str, **fields: Any) -> bytes:
    payload: dict[str, Any] = {"cmd": cmd}
    for key, value in fields.items():
        if value is not None:
            payload[key] = value
    text = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    return (text + "\r\n").encode("utf-8")


def parse_packet_line(line: str) -> Packet:
    text = line.strip()
    if not text:
        return Packet(kind="empty", raw=text, data={})

    if not text.startswith("{"):
        return Packet(kind="ignored", raw=text, data={"reason": "non_json"})

    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        return Packet(kind="invalid_json", raw=text, data={"reason": "bad_json"})

    if not isinstance(data, dict):
        return Packet(kind="ignored", raw=text, data={"reason": "non_object_json"})

    packet_type = str(data.get("type") or "json")
    if packet_type == "telemetry":
        return Packet(kind="telemetry", raw=text, data=data)
    if packet_type == "ack" and data.get("cmd") == "get_config" and _as_int(data.get("ok")) == 1:
        if "period_s" in data:
            return Packet(kind="config", raw=text, data=data)
    return Packet(kind=packet_type, raw=text, data=data)


def telemetry_from_packet(packet: Packet, received_at: str) -> TelemetryRecord:
    data = packet.data
    return TelemetryRecord(
        received_at=received_at,
        device_id=str(data.get("id", "")),
        seq=_as_int(data.get("seq")),
        uptime_s=_as_int(data.get("uptime_s")),
        temp_c=_as_float(data.get("temp_c")),
        ph=_as_float(data.get("ph")),
        turb_ntu=_as_float(data.get("turb_ntu")),
        alarm=_as_int(data.get("alarm")),
        err=_as_int(data.get("err")),
        raw_json=packet.raw,
    )


def config_from_packet(packet: Packet, received_at: str) -> ConfigRecord:
    data = packet.data
    return ConfigRecord(
        received_at=received_at,
        device_id=str(data.get("id", "")),
        sample_period_s=_as_int(data.get("period_s")),
        ph_min=_as_float(data.get("ph_min")),
        ph_max=_as_float(data.get("ph_max")),
        temp_min=_as_float(data.get("temp_min")),
        temp_max=_as_float(data.get("temp_max")),
        turb_max=_as_float(data.get("turb_max")),
        ph_k=_as_float(data.get("ph_k")),
        ph_b=_as_float(data.get("ph_b")),
        turb_a=_as_float(data.get("turb_a")),
        turb_b=_as_float(data.get("turb_b")),
        turb_c=_as_float(data.get("turb_c")),
        raw_json=packet.raw,
    )
