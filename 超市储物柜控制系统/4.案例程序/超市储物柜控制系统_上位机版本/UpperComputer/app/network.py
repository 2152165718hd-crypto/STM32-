from __future__ import annotations

import datetime as _dt
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Any

from PySide6.QtCore import QObject, QTimer, Signal, Slot
from PySide6.QtNetwork import QAbstractSocket, QTcpSocket

from .protocol import (
    CMD_CLEAR_ALARM,
    CMD_EMERGENCY_UNLOCK,
    CMD_HEARTBEAT,
    CMD_LOCKER_CONTROL,
    CMD_RECORD_EXPORT,
    CMD_SET_TIME,
    CMD_STATUS_QUERY,
    CMD_RESPONSE_MASK,
    DEVICE_ADDR_DEFAULT,
    DeviceSnapshot,
    LegacyMessage,
    ProtocolMode,
    RecordEntry,
    ResultCode,
    StreamParser,
    command_name,
    decode_result_and_snapshot,
    decode_record_entry,
    decode_status_snapshot,
    encode_binary_frame,
    encode_locker_control_payload,
    encode_set_time_payload,
    format_result_code,
    legacy_record_from_json,
    legacy_snapshot_from_json,
    build_legacy_command,
)


@dataclass(slots=True)
class ConnectionConfig:
    host: str = "192.168.4.1"
    port: int = 9000
    addr: int = DEVICE_ADDR_DEFAULT
    protocol_mode: ProtocolMode = ProtocolMode.BINARY
    heartbeat_interval_ms: int = 5000
    response_timeout_ms: int = 1500
    retry_count: int = 3
    reconnect_interval_ms: int = 3000
    auto_reconnect: bool = True
    auto_status_on_connect: bool = True


@dataclass(slots=True)
class PendingRequest:
    seq: int
    cmd: int
    description: str
    response_kind: str
    payload: bytes
    json_cmd: str
    json_fields: dict[str, Any]
    timeout_ms: int
    max_retries: int
    attempt: int = 1
    sent_at: float = 0.0
    wire: bytes = b""


class DeviceWorker(QObject):
    connectionStateChanged = Signal(str)
    connectedChanged = Signal(bool)
    protocolModeChanged = Signal(str)
    logMessage = Signal(str, str, str)
    rawFrame = Signal(bytes, str)
    snapshotReceived = Signal(object)
    recordReceived = Signal(object)
    requestCompleted = Signal(int, int, str)
    requestFailed = Signal(int, int, str)
    exportProgress = Signal(int, int)
    metricsChanged = Signal(object)

    def __init__(self) -> None:
        super().__init__()
        self._config = ConnectionConfig()
        self._socket: QTcpSocket | None = None
        self._parser = StreamParser()
        self._tick_timer: QTimer | None = None
        self._reconnect_timer: QTimer | None = None
        self._queue: deque[PendingRequest] = deque()
        self._active: PendingRequest | None = None
        self._seq = 0
        self._connected = False
        self._manual_disconnect = False
        self._failure_streak = 0
        self._state_text = "idle"
        self._last_io_mono = time.monotonic()
        self._last_snapshot: DeviceSnapshot | None = None
        self._export_expected = 0
        self._export_received = 0
        self._export_active_seq = 0

    @Slot(object)
    def configure(self, config: ConnectionConfig) -> None:
        self._config = config
        self.protocolModeChanged.emit(config.protocol_mode.value)

    @Slot()
    def start(self) -> None:
        if self._socket is not None:
            return

        self._socket = QTcpSocket(self)
        self._socket.readyRead.connect(self._on_ready_read)
        self._socket.connected.connect(self._on_connected)
        self._socket.disconnected.connect(self._on_disconnected)
        self._socket.errorOccurred.connect(self._on_socket_error)

        self._tick_timer = QTimer(self)
        self._tick_timer.setInterval(100)
        self._tick_timer.timeout.connect(self._tick)
        self._tick_timer.start()

        self._reconnect_timer = QTimer(self)
        self._reconnect_timer.setSingleShot(True)
        self._reconnect_timer.timeout.connect(self._attempt_reconnect)

        self._emit_state("idle")
        self._log("info", "SYS", "worker ready")

    @Slot()
    def stop(self) -> None:
        self._manual_disconnect = True
        self._queue.clear()
        self._active = None
        if self._reconnect_timer is not None:
            self._reconnect_timer.stop()
        if self._tick_timer is not None:
            self._tick_timer.stop()
        if self._socket is not None:
            self._socket.abort()
            self._socket.deleteLater()
            self._socket = None
        self._connected = False
        self._emit_state("stopped")

    @Slot()
    def connect_now(self) -> None:
        if self._socket is None:
            return

        self._manual_disconnect = False
        self._queue.clear()
        self._active = None
        self._parser.reset()
        self._export_expected = 0
        self._export_received = 0
        self._export_active_seq = 0

        if self._socket.state() != QAbstractSocket.UnconnectedState:
            self._socket.abort()

        self._emit_state("connecting")
        self._log(
            "info",
            "SYS",
            f"connect {self._config.host}:{self._config.port} addr=0x{self._config.addr:02X} mode={self._config.protocol_mode.value}",
        )
        self._socket.connectToHost(self._config.host, self._config.port)

    @Slot()
    def disconnect_now(self) -> None:
        self._manual_disconnect = True
        self._queue.clear()
        self._active = None
        self._parser.reset()
        self._export_expected = 0
        self._export_received = 0
        self._export_active_seq = 0
        if self._reconnect_timer is not None:
            self._reconnect_timer.stop()
        self._disconnect_socket()
        self._emit_state("disconnected")

    @Slot()
    def request_status(self) -> None:
        self._enqueue_request(
            cmd=CMD_STATUS_QUERY,
            description="状态轮询",
            response_kind="snapshot",
            payload=b"",
            json_cmd="status_query",
            json_fields={},
        )

    @Slot()
    def request_heartbeat(self) -> None:
        self._enqueue_request(
            cmd=CMD_HEARTBEAT,
            description="心跳",
            response_kind="snapshot",
            payload=b"",
            json_cmd="heartbeat",
            json_fields={},
        )

    @Slot(int, int, int)
    def send_locker_control(self, locker_mask: int, action: int, flags: int = 0) -> None:
        if locker_mask <= 0:
            self._log("warn", "SYS", "locker mask is empty")
            return

        self._enqueue_request(
            cmd=CMD_LOCKER_CONTROL,
            description=f"柜门控制 mask=0x{locker_mask:04X} action={action}",
            response_kind="result_snapshot",
            payload=encode_locker_control_payload(locker_mask, action, flags),
            json_cmd="locker_control",
            json_fields={"locker_mask": locker_mask, "action": action, "flags": flags},
        )

    @Slot()
    def clear_alarm(self) -> None:
        self._enqueue_request(
            cmd=CMD_CLEAR_ALARM,
            description="清除报警",
            response_kind="result_snapshot",
            payload=b"",
            json_cmd="clear_alarm",
            json_fields={},
        )

    @Slot()
    def emergency_unlock(self) -> None:
        self._enqueue_request(
            cmd=CMD_EMERGENCY_UNLOCK,
            description="紧急强制解锁",
            response_kind="result_snapshot",
            payload=b"",
            json_cmd="emergency_unlock",
            json_fields={},
        )

    @Slot()
    def export_records(self) -> None:
        self._enqueue_request(
            cmd=CMD_RECORD_EXPORT,
            description="导出记录",
            response_kind="export",
            payload=b"",
            json_cmd="record_export",
            json_fields={},
        )

    @Slot(object)
    def set_time(self, when: object) -> None:
        if isinstance(when, _dt.datetime):
            dt = when
        else:
            dt = _dt.datetime.now()
        self._enqueue_request(
            cmd=CMD_SET_TIME,
            description=dt.strftime("设时 %Y-%m-%d %H:%M:%S"),
            response_kind="result_snapshot",
            payload=encode_set_time_payload(
                dt.year % 100,
                dt.month,
                dt.day,
                dt.hour,
                dt.minute,
                dt.second,
            ),
            json_cmd="set_time",
            json_fields={
                "year": dt.year % 100,
                "month": dt.month,
                "day": dt.day,
                "hour": dt.hour,
                "minute": dt.minute,
                "second": dt.second,
            },
        )

    def current_snapshot(self) -> DeviceSnapshot | None:
        return self._last_snapshot

    def _next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFFFF
        if self._seq == 0:
            self._seq = 1
        return self._seq

    def _enqueue_request(
        self,
        *,
        cmd: int,
        description: str,
        response_kind: str,
        payload: bytes,
        json_cmd: str,
        json_fields: dict[str, Any],
    ) -> int:
        seq = self._next_seq()
        request = PendingRequest(
            seq=seq,
            cmd=cmd,
            description=description,
            response_kind=response_kind,
            payload=payload,
            json_cmd=json_cmd,
            json_fields=dict(json_fields),
            timeout_ms=self._config.response_timeout_ms,
            max_retries=max(0, self._config.retry_count),
        )
        request.wire = self._build_wire(request)
        self._queue.append(request)
        self._pump_queue()
        return seq

    def _build_wire(self, request: PendingRequest) -> bytes:
        if self._config.protocol_mode == ProtocolMode.LEGACY_JSON:
            return build_legacy_command(request.json_cmd, seq=request.seq, **request.json_fields)
        return encode_binary_frame(self._config.addr, request.cmd, request.seq, request.payload)

    def _pump_queue(self) -> None:
        if self._socket is None or self._socket.state() != QAbstractSocket.ConnectedState:
            return
        if self._active is not None:
            return

        while self._queue:
            request = self._queue.popleft()
            request.attempt = max(1, request.attempt)
            request.sent_at = time.monotonic()
            request.wire = self._build_wire(request)
            self._socket.write(request.wire)
            self._socket.flush()
            self._last_io_mono = request.sent_at
            self.rawFrame.emit(request.wire, "TX")
            self._log(
                "tx",
                "TX",
                f"seq=0x{request.seq:04X} cmd={command_name(request.cmd)} {request.description}",
            )
            if request.response_kind == "export":
                self._export_active_seq = request.seq
                self._export_expected = 0
                self._export_received = 0
            self._active = request
            return

    def _emit_snapshot(self, snapshot: DeviceSnapshot, source: str) -> None:
        self._last_snapshot = snapshot
        self._last_io_mono = time.monotonic()
        self.snapshotReceived.emit(snapshot)
        self._log("rx", "RX", f"{source}: {snapshot.to_summary()}")
        self.metricsChanged.emit(
            {
                "occupied": snapshot.occupied_count,
                "relay": snapshot.relay_count,
                "door": snapshot.door_count,
                "alarm": snapshot.alarm,
                "records": snapshot.record_count,
            }
        )

    def _fallback_snapshot(self) -> DeviceSnapshot:
        return DeviceSnapshot(
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            1 if self._config.protocol_mode == ProtocolMode.BINARY else 2,
        )

    def _complete_active(self, message: str, result_ok: bool = True) -> None:
        if self._active is None:
            return
        request = self._active
        self._active = None
        self._failure_streak = 0
        level = "info" if result_ok else "warn"
        self.requestCompleted.emit(request.seq, request.cmd, message)
        self._log(level, "SYS", f"seq=0x{request.seq:04X} {message}")
        self._pump_queue()

    def _fail_active(self, message: str) -> None:
        if self._active is None:
            return
        request = self._active
        self._active = None
        self._failure_streak += 1
        self.requestFailed.emit(request.seq, request.cmd, message)
        self._log("error", "SYS", f"seq=0x{request.seq:04X} {message}")
        self._pump_queue()
        if self._failure_streak >= 3:
            self._schedule_reconnect(message)

    def _schedule_reconnect(self, reason: str) -> None:
        if self._manual_disconnect or not self._config.auto_reconnect:
            return
        self._log("warn", "SYS", f"reconnect scheduled: {reason}")
        if self._reconnect_timer is not None and not self._reconnect_timer.isActive():
            self._reconnect_timer.start(self._config.reconnect_interval_ms)
        self._disconnect_socket()

    def _disconnect_socket(self) -> None:
        if self._socket is None:
            return
        if self._socket.state() == QAbstractSocket.ConnectedState:
            self._socket.disconnectFromHost()
        if self._socket.state() != QAbstractSocket.UnconnectedState:
            self._socket.abort()
        self._parser.reset()
        self._export_expected = 0
        self._export_received = 0
        self._export_active_seq = 0
        self._connected = False
        self.connectedChanged.emit(False)

    @Slot()
    def _attempt_reconnect(self) -> None:
        if self._manual_disconnect or not self._config.auto_reconnect:
            return
        if self._socket is None:
            return
        if self._socket.state() in (
            QAbstractSocket.ConnectingState,
            QAbstractSocket.ConnectedState,
        ):
            return
        self._emit_state("reconnecting")
        self._log("warn", "SYS", "attempt reconnect")
        self._socket.connectToHost(self._config.host, self._config.port)

    @Slot()
    def _tick(self) -> None:
        now = time.monotonic()

        if self._socket is None:
            return

        if self._active is not None:
            elapsed = (now - self._active.sent_at) * 1000.0
            if elapsed >= self._active.timeout_ms:
                if self._active.attempt < self._active.max_retries + 1:
                    self._active.attempt += 1
                    self._active.sent_at = now
                    self._socket.write(self._active.wire)
                    self._socket.flush()
                    self._last_io_mono = now
                    self.rawFrame.emit(self._active.wire, "TX")
                    self._log(
                        "retry",
                        "TX",
                        f"seq=0x{self._active.seq:04X} retry {self._active.attempt - 1}/{self._active.max_retries}",
                    )
                else:
                    self._fail_active("response timeout")

        if (
            self._connected
            and self._active is None
            and not self._queue
            and (now - self._last_io_mono) * 1000.0 >= self._config.heartbeat_interval_ms
        ):
            self.request_heartbeat()

    @Slot()
    def _on_connected(self) -> None:
        self._connected = True
        self._failure_streak = 0
        self._last_io_mono = time.monotonic()
        if self._reconnect_timer is not None:
            self._reconnect_timer.stop()
        self.connectedChanged.emit(True)
        self._emit_state("connected")
        self._log("info", "SYS", "socket connected")
        if self._config.auto_status_on_connect:
            self.request_status()

    @Slot()
    def _on_disconnected(self) -> None:
        self._connected = False
        self.connectedChanged.emit(False)
        self._emit_state("disconnected")
        self._log("warn", "SYS", "socket disconnected")
        if self._active is not None and not self._manual_disconnect:
            self._queue.appendleft(self._active)
            self._active = None
        if not self._manual_disconnect and self._config.auto_reconnect:
            if self._reconnect_timer is not None and not self._reconnect_timer.isActive():
                self._reconnect_timer.start(self._config.reconnect_interval_ms)

    @Slot(QAbstractSocket.SocketError)
    def _on_socket_error(self, error: QAbstractSocket.SocketError) -> None:
        if self._socket is None:
            return
        if error == QAbstractSocket.RemoteHostClosedError:
            return
        message = self._socket.errorString()
        self._log("error", "SYS", f"socket error: {message}")
        self._failure_streak += 1
        if not self._manual_disconnect and self._config.auto_reconnect:
            if self._reconnect_timer is not None and not self._reconnect_timer.isActive():
                self._reconnect_timer.start(self._config.reconnect_interval_ms)

    @Slot()
    def _on_ready_read(self) -> None:
        if self._socket is None:
            return

        data = bytes(self._socket.readAll())
        if not data:
            return

        self._last_io_mono = time.monotonic()
        self.rawFrame.emit(data, "RX")

        for event in self._parser.feed(data):
            if isinstance(event, LegacyMessage):
                self._handle_legacy_message(event)
            else:
                self._handle_binary_frame(event)

    def _handle_binary_frame(self, frame: Any) -> None:
        if frame.addr not in (self._config.addr, 0xFF):
            return

        self._log("rx", "RX", f"seq=0x{frame.seq:04X} cmd={command_name(frame.cmd)} bytes={len(frame.payload)}")
        if frame.cmd in (CMD_STATUS_QUERY | CMD_RESPONSE_MASK, CMD_HEARTBEAT | CMD_RESPONSE_MASK):
            try:
                result, snapshot, _ = decode_result_and_snapshot(frame.payload)
            except ValueError as exc:
                self._log("warn", "RX", f"bad snapshot: {exc}")
                return
            self._emit_snapshot(snapshot, command_name(frame.cmd))
            if result != ResultCode.OK:
                self._log("warn", "RX", f"{command_name(frame.cmd)} result={format_result_code(result)}")
            if self._active is not None and self._active.seq == frame.seq:
                self._complete_active(
                    command_name(frame.cmd),
                    result_ok=result == ResultCode.OK,
                )
            return

        if frame.cmd == (CMD_LOCKER_CONTROL | CMD_RESPONSE_MASK):
            if len(frame.payload) < 1 + 2 + 1:
                return
            result = frame.payload[0]
            applied_mask = int.from_bytes(frame.payload[1:3], "big")
            try:
                snapshot = decode_status_snapshot(frame.payload[3:])
            except ValueError:
                snapshot = self._last_snapshot or self._fallback_snapshot()
            self._emit_snapshot(snapshot, "locker_control")
            self._log(
                "rx",
                "RX",
                f"locker_control result={format_result_code(result)} applied=0x{applied_mask:04X}",
            )
            if self._active is not None and self._active.seq == frame.seq:
                if result == ResultCode.OK:
                    self._complete_active("locker_control ok")
                else:
                    self._complete_active(f"locker_control {format_result_code(result)}", result_ok=False)
            return

        if frame.cmd == (CMD_CLEAR_ALARM | CMD_RESPONSE_MASK):
            if len(frame.payload) < 1:
                return
            result = ResultCode.BAD_REQUEST
            try:
                result, snapshot, _ = decode_result_and_snapshot(frame.payload)
            except ValueError:
                snapshot = self._last_snapshot or self._fallback_snapshot()
            self._emit_snapshot(snapshot, "clear_alarm")
            if self._active is not None and self._active.seq == frame.seq:
                self._complete_active(
                    "clear_alarm ok" if result == ResultCode.OK else f"clear_alarm {format_result_code(result)}",
                    result_ok=result == ResultCode.OK,
                )
            return

        if frame.cmd == (CMD_EMERGENCY_UNLOCK | CMD_RESPONSE_MASK):
            if len(frame.payload) < 1 + 2:
                return
            result = frame.payload[0]
            applied_mask = int.from_bytes(frame.payload[1:3], "big")
            try:
                snapshot = decode_status_snapshot(frame.payload[3:])
            except ValueError:
                snapshot = self._last_snapshot or self._fallback_snapshot()
            self._emit_snapshot(snapshot, "emergency_unlock")
            self._log("rx", "RX", f"emergency result={format_result_code(result)} applied=0x{applied_mask:04X}")
            if self._active is not None and self._active.seq == frame.seq:
                self._complete_active(
                    "emergency_unlock ok" if result == ResultCode.OK else f"emergency_unlock {format_result_code(result)}",
                    result_ok=result == ResultCode.OK,
                )
            return

        if frame.cmd == (CMD_SET_TIME | CMD_RESPONSE_MASK):
            result = ResultCode.BAD_REQUEST
            try:
                result, snapshot, _ = decode_result_and_snapshot(frame.payload)
            except ValueError:
                snapshot = self._last_snapshot or self._fallback_snapshot()
            self._emit_snapshot(snapshot, "set_time")
            if self._active is not None and self._active.seq == frame.seq:
                self._complete_active(
                    "set_time ok" if result == ResultCode.OK else f"set_time {format_result_code(result)}",
                    result_ok=result == ResultCode.OK,
                )
            return

        if frame.cmd == (CMD_RECORD_EXPORT | CMD_RESPONSE_MASK):
            if len(frame.payload) < 2:
                return
            stage = frame.payload[0]
            result = frame.payload[1]
            if stage == 0:
                count = int.from_bytes(frame.payload[2:4], "big") if len(frame.payload) >= 4 else 0
                self._export_expected = count
                self._export_received = 0
                self.exportProgress.emit(0, count)
                self._log("rx", "RX", f"export begin count={count} result={format_result_code(result)}")
            elif stage == 1:
                try:
                    record = decode_record_entry(frame.payload[2:])
                except ValueError as exc:
                    self._log("warn", "RX", f"bad record: {exc}")
                    return
                self._export_received += 1
                self.exportProgress.emit(self._export_received, self._export_expected)
                self.recordReceived.emit(record)
                self._log("rx", "RX", f"record {record.label()} locker={record.locker_id:02d} time={record.timestamp}")
            elif stage == 2:
                count = int.from_bytes(frame.payload[2:4], "big") if len(frame.payload) >= 4 else self._export_expected
                self._export_expected = count
                self.exportProgress.emit(self._export_received, count)
                self._log("rx", "RX", f"export done count={count} result={format_result_code(result)}")
                if self._active is not None and self._active.seq == frame.seq:
                    self._complete_active("record_export done", result_ok=result == ResultCode.OK)
            return

    def _handle_legacy_message(self, message: LegacyMessage) -> None:
        if message.obj is None:
            self._log("warn", "RX", f"text: {message.raw}")
            return

        obj = message.obj
        msg_type = str(obj.get("type", "")).lower()
        if msg_type == "hello":
            device = obj.get("device", "device")
            proto = obj.get("proto", "")
            self._log("info", "RX", f"hello from {device} proto={proto}")
            return

        if msg_type == "status":
            snapshot = legacy_snapshot_from_json(obj)
            self._emit_snapshot(snapshot, "legacy status")
            if self._active is not None and self._active.response_kind in {"snapshot", "result_snapshot"}:
                if self._active.cmd in (CMD_STATUS_QUERY, CMD_HEARTBEAT):
                    self._complete_active("status ok")
                elif self._active.cmd in (CMD_LOCKER_CONTROL, CMD_CLEAR_ALARM, CMD_EMERGENCY_UNLOCK, CMD_SET_TIME):
                    # legacy firmware sends ack + status, so status completes the request
                    self._complete_active(f"{command_name(self._active.cmd)} ok")
            return

        if msg_type == "record":
            record = legacy_record_from_json(obj)
            self.recordReceived.emit(record)
            self._export_received += 1
            self.exportProgress.emit(self._export_received, self._export_expected)
            self._log("rx", "RX", f"record {record.label()} locker={record.locker_id:02d} time={record.timestamp}")
            return

        if msg_type == "export_done":
            count = int(obj.get("count", obj.get("record_count", self._export_expected)))
            self._export_expected = count
            self.exportProgress.emit(self._export_received, count)
            self._log("rx", "RX", f"export done count={count}")
            if self._active is not None and self._active.cmd == CMD_RECORD_EXPORT:
                self._complete_active("record_export done")
            return

        if msg_type == "ack":
            seq = int(obj.get("seq", 0)) & 0xFFFF
            cmd = str(obj.get("cmd", ""))
            self._log("rx", "RX", f"ack seq=0x{seq:04X} cmd={cmd}")
            return

        if msg_type == "error":
            seq = int(obj.get("seq", 0)) & 0xFFFF
            code = str(obj.get("code", "error"))
            message_text = str(obj.get("message", ""))
            self._log("error", "RX", f"error seq=0x{seq:04X} {code}: {message_text}")
            if self._active is not None and self._active.seq == seq:
                self._complete_active(f"{code}: {message_text}", result_ok=False)
            return

        self._log("warn", "RX", f"json type={msg_type} raw={message.raw}")

    def _emit_state(self, text: str) -> None:
        self._state_text = text
        self.connectionStateChanged.emit(text)

    def _log(self, level: str, direction: str, message: str) -> None:
        self.logMessage.emit(level, direction, message)
