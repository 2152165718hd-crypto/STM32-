from __future__ import annotations

import queue
import threading
from dataclasses import dataclass
from typing import Any

import serial
from serial.tools import list_ports


@dataclass(slots=True)
class SerialPortInfo:
    device: str
    description: str

    @property
    def display(self) -> str:
        if self.description:
            return f"{self.device} - {self.description}"
        return self.device


@dataclass(slots=True)
class SerialEvent:
    kind: str
    text: str


def enumerate_ports() -> list[SerialPortInfo]:
    ports: list[SerialPortInfo] = []
    for port in list_ports.comports():
        ports.append(SerialPortInfo(device=port.device, description=port.description or ""))
    return ports


def decode_line(raw: bytes) -> str:
    for encoding in ("utf-8", "gbk", "latin1"):
        try:
            text = raw.decode(encoding)
        except UnicodeDecodeError:
            continue
        text = text.strip()
        if text:
            return text
    return raw.decode("utf-8", errors="replace").strip()


class SerialWorker:
    def __init__(self, event_queue: "queue.Queue[SerialEvent]") -> None:
        self._event_queue = event_queue
        self._serial: serial.Serial | None = None
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._write_lock = threading.Lock()
        self._port = ""
        self._baudrate = 9600

    @property
    def connected(self) -> bool:
        return self._serial is not None and self._serial.is_open

    @property
    def port(self) -> str:
        return self._port

    @property
    def baudrate(self) -> int:
        return self._baudrate

    def _emit(self, kind: str, text: str) -> None:
        self._event_queue.put(SerialEvent(kind=kind, text=text))

    def connect(self, port: str, baudrate: int) -> None:
        if self.connected:
            raise RuntimeError("串口已连接")
        if self._thread and self._thread.is_alive():
            raise RuntimeError("串口线程尚未退出")

        self._port = port
        self._baudrate = baudrate
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, name="SerialWorker", daemon=True)
        self._thread.start()

    def disconnect(self) -> None:
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._thread = None
        if self._serial is not None:
            try:
                self._serial.close()
            finally:
                self._serial = None

    def send_bytes(self, data: bytes) -> None:
        if not self.connected or self._serial is None:
            raise RuntimeError("串口未连接")
        with self._write_lock:
            self._serial.write(data)
            self._serial.flush()

    def send_text(self, text: str) -> None:
        self.send_bytes(text.encode("utf-8"))

    def _run(self) -> None:
        try:
            ser = serial.Serial(
                port=self._port,
                baudrate=self._baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.5,
                write_timeout=1.0,
            )
            self._serial = ser
            self._emit("status", f"串口已连接: {self._port} @ {self._baudrate}")

            while not self._stop_event.is_set():
                try:
                    raw = ser.readline()
                except serial.SerialException as exc:
                    self._emit("error", f"串口读取失败: {exc}")
                    break

                if raw:
                    self._emit("line", decode_line(raw))
        except Exception as exc:
            self._emit("error", f"打开串口失败: {exc}")
        finally:
            if self._serial is not None:
                try:
                    self._serial.close()
                finally:
                    self._serial = None
            self._emit("status", "串口已断开")

