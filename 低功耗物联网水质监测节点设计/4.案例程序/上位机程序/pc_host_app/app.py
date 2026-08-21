from __future__ import annotations

import csv
import os
import queue
import sys
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk

import matplotlib

matplotlib.use("TkAgg")
matplotlib.rcParams["font.sans-serif"] = [
    "Microsoft YaHei",
    "SimHei",
    "Noto Sans CJK SC",
    "Arial Unicode MS",
    "DejaVu Sans",
]
matplotlib.rcParams["axes.unicode_minus"] = False
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.dates import AutoDateLocator, ConciseDateFormatter
from matplotlib.figure import Figure

if __package__ in {None, ""}:
    sys.path.append(str(Path(__file__).resolve().parent.parent))
    from pc_host_app.protocol import (
        ConfigRecord,
        Packet,
        build_json_command,
        config_from_packet,
        parse_packet_line,
        telemetry_from_packet,
    )
    from pc_host_app.serial_worker import SerialEvent, SerialWorker, enumerate_ports
    from pc_host_app.storage import HistoryStore
else:
    from .protocol import (
        ConfigRecord,
        Packet,
        build_json_command,
        config_from_packet,
        parse_packet_line,
        telemetry_from_packet,
    )
    from .serial_worker import SerialEvent, SerialWorker, enumerate_ports
    from .storage import HistoryStore


APP_TITLE = "水质监测上位机"
APP_NAME = "WaterHostApp"
DEFAULT_BAUDRATE = 9600
DEFAULT_DB_NAME = "history.db"


def _now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _hours_ago_text(hours: int) -> str:
    return (datetime.now() - timedelta(hours=hours)).strftime("%Y-%m-%d %H:%M:%S")


def _parse_time(value: str) -> str:
    text = value.strip()
    if not text:
        return ""
    datetime.strptime(text, "%Y-%m-%d %H:%M:%S")
    return text


def _format_float(value: float, digits: int = 2) -> str:
    return f"{value:.{digits}f}"


def _app_data_dir() -> Path:
    local_appdata = os.getenv("LOCALAPPDATA")
    if local_appdata:
        return Path(local_appdata) / APP_NAME
    return Path.home() / f".{APP_NAME}"


class WaterHostApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1540x960")
        self.minsize(1320, 820)

        self.data_dir = _app_data_dir()
        self.db_path = self.data_dir / DEFAULT_DB_NAME
        self.store = HistoryStore(self.db_path)

        self.event_queue: "queue.Queue[SerialEvent]" = queue.Queue()
        self.worker = SerialWorker(self.event_queue)
        self.current_rows: list[dict[str, Any]] = []

        self.port_map: dict[str, str] = {}
        self.connected_var = tk.StringVar(value="未连接")
        self.connection_hint_var = tk.StringVar(value="请选择串口并连接")

        self.device_id_var = tk.StringVar(value="-")
        self.seq_var = tk.StringVar(value="-")
        self.uptime_var = tk.StringVar(value="-")
        self.temp_var = tk.StringVar(value="-")
        self.ph_var = tk.StringVar(value="-")
        self.turb_var = tk.StringVar(value="-")
        self.alarm_var = tk.StringVar(value="正常")
        self.err_var = tk.StringVar(value="0x00")
        self.recv_time_var = tk.StringVar(value="-")

        self.latest_device_var = tk.StringVar(value="")
        self.sample_period_var = tk.StringVar(value="60")
        self.ph_min_var = tk.StringVar(value="6.50")
        self.ph_max_var = tk.StringVar(value="8.50")
        self.temp_min_var = tk.StringVar(value="0.0")
        self.temp_max_var = tk.StringVar(value="40.0")
        self.turb_max_var = tk.StringVar(value="100.0")
        self.ph_k_var = tk.StringVar(value="-5.70")
        self.ph_b_var = tk.StringVar(value="21.34")
        self.turb_a_var = tk.StringVar(value="-1120.4")
        self.turb_b_var = tk.StringVar(value="5742.3")
        self.turb_c_var = tk.StringVar(value="-4352.9")

        self.query_device_var = tk.StringVar(value="<全部>")
        self.query_start_var = tk.StringVar(value=_hours_ago_text(24))
        self.query_end_var = tk.StringVar(value=_now_text())
        self.query_limit_var = tk.StringVar(value="500")

        self._build_style()
        self._build_ui()
        self.refresh_ports()
        self.refresh_device_list()
        self.after(100, self._poll_events)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("Header.TFrame", padding=8)
        style.configure("Section.TLabelframe", padding=10)
        style.configure("Section.TLabelframe.Label", font=("Microsoft YaHei UI", 10, "bold"))
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 12, "bold"))

    def _build_ui(self) -> None:
        root = ttk.Frame(self, padding=10)
        root.pack(fill="both", expand=True)

        self._build_connection_bar(root)
        self._build_status_panel(root)
        self._build_notebook(root)
        self._build_footer(root)

    def _build_connection_bar(self, parent: ttk.Frame) -> None:
        bar = ttk.LabelFrame(parent, text="通信连接", style="Section.TLabelframe")
        bar.pack(fill="x", pady=(0, 10))

        ttk.Label(bar, text="串口").grid(row=0, column=0, sticky="w")
        self.port_combo = ttk.Combobox(bar, width=34, state="readonly")
        self.port_combo.grid(row=0, column=1, padx=(8, 18), sticky="w")

        ttk.Label(bar, text="波特率").grid(row=0, column=2, sticky="w")
        self.baud_combo = ttk.Combobox(
            bar,
            width=12,
            values=["9600", "19200", "38400", "57600", "115200"],
            state="readonly",
        )
        self.baud_combo.set(str(DEFAULT_BAUDRATE))
        self.baud_combo.grid(row=0, column=3, padx=(8, 18), sticky="w")

        ttk.Button(bar, text="刷新串口", command=self.refresh_ports).grid(row=0, column=4, padx=(0, 8))
        self.connect_btn = ttk.Button(bar, text="连接", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=5, padx=(0, 8))
        ttk.Button(bar, text="读取配置", command=self.send_get_config).grid(row=0, column=6, padx=(0, 8))
        ttk.Button(bar, text="立即采样", command=self.send_sample_now).grid(row=0, column=7)

        ttk.Label(bar, textvariable=self.connected_var).grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 0))
        ttk.Label(bar, textvariable=self.connection_hint_var).grid(
            row=1, column=2, columnspan=6, sticky="w", pady=(8, 0)
        )

        for column in range(8):
            bar.columnconfigure(column, weight=1 if column in (1, 3) else 0)

    def _build_status_panel(self, parent: ttk.Frame) -> None:
        panel = ttk.LabelFrame(parent, text="最新数据", style="Section.TLabelframe")
        panel.pack(fill="x", pady=(0, 10))

        items = [
            ("设备编号", self.device_id_var),
            ("序号", self.seq_var),
            ("运行秒数", self.uptime_var),
            ("采集时间", self.recv_time_var),
            ("温度(℃)", self.temp_var),
            ("pH", self.ph_var),
            ("浑浊度(NTU)", self.turb_var),
            ("错误码", self.err_var),
        ]

        for index, (label, var) in enumerate(items):
            row = index // 4
            column = (index % 4) * 2
            ttk.Label(panel, text=label).grid(row=row, column=column, sticky="w", padx=(0, 6), pady=4)
            ttk.Label(panel, textvariable=var, width=18).grid(row=row, column=column + 1, sticky="w", pady=4)

        ttk.Label(panel, text="报警状态").grid(row=2, column=0, sticky="w", padx=(0, 6), pady=(8, 0))
        self.alarm_value_label = tk.Label(
            panel,
            textvariable=self.alarm_var,
            width=14,
            anchor="center",
            relief="groove",
            bg="#e7f5ea",
            fg="#146c2e",
        )
        self.alarm_value_label.grid(row=2, column=1, sticky="w", pady=(8, 0))

        ttk.Label(panel, text="最近设备").grid(row=2, column=2, sticky="w", padx=(0, 6), pady=(8, 0))
        ttk.Label(panel, textvariable=self.latest_device_var).grid(row=2, column=3, sticky="w", pady=(8, 0))

        for column in range(8):
            panel.columnconfigure(column, weight=1)

    def _build_notebook(self, parent: ttk.Frame) -> None:
        self.nb = ttk.Notebook(parent)
        self.nb.pack(fill="both", expand=True)

        self.history_tab = ttk.Frame(self.nb, padding=10)
        self.config_tab = ttk.Frame(self.nb, padding=10)
        self.log_tab = ttk.Frame(self.nb, padding=10)

        self.nb.add(self.history_tab, text="历史查询")
        self.nb.add(self.config_tab, text="设备配置")
        self.nb.add(self.log_tab, text="通信日志")

        self._build_history_tab(self.history_tab)
        self._build_config_tab(self.config_tab)
        self._build_log_tab(self.log_tab)

    def _build_history_tab(self, parent: ttk.Frame) -> None:
        query = ttk.LabelFrame(parent, text="查询条件", style="Section.TLabelframe")
        query.pack(fill="x", pady=(0, 10))

        ttk.Label(query, text="设备编号").grid(row=0, column=0, sticky="w")
        self.query_device_combo = ttk.Combobox(query, textvariable=self.query_device_var, width=18, state="readonly")
        self.query_device_combo.grid(row=0, column=1, padx=(8, 18), pady=4, sticky="w")

        ttk.Label(query, text="开始时间").grid(row=0, column=2, sticky="w")
        ttk.Entry(query, textvariable=self.query_start_var, width=24).grid(row=0, column=3, padx=(8, 18), pady=4, sticky="w")

        ttk.Label(query, text="结束时间").grid(row=0, column=4, sticky="w")
        ttk.Entry(query, textvariable=self.query_end_var, width=24).grid(row=0, column=5, padx=(8, 18), pady=4, sticky="w")

        ttk.Label(query, text="条数上限").grid(row=0, column=6, sticky="w")
        ttk.Entry(query, textvariable=self.query_limit_var, width=10).grid(row=0, column=7, padx=(8, 18), pady=4, sticky="w")

        ttk.Button(query, text="查询", command=self.query_history).grid(row=0, column=8, padx=(0, 8))
        ttk.Button(query, text="导出CSV", command=self.export_history).grid(row=0, column=9, padx=(0, 8))
        ttk.Button(query, text="刷新设备列表", command=self.refresh_device_list).grid(row=0, column=10)

        for column in range(11):
            query.columnconfigure(column, weight=1 if column in (1, 3, 5, 7) else 0)

        chart_frame = ttk.LabelFrame(parent, text="曲线图", style="Section.TLabelframe")
        chart_frame.pack(fill="both", expand=False, pady=(0, 10))

        self.figure = Figure(figsize=(11.5, 5.8), dpi=100)
        self.ax_temp = self.figure.add_subplot(311)
        self.ax_ph = self.figure.add_subplot(312, sharex=self.ax_temp)
        self.ax_turb = self.figure.add_subplot(313, sharex=self.ax_temp)
        self.figure.tight_layout(pad=2.0)

        self.chart_canvas = FigureCanvasTkAgg(self.figure, master=chart_frame)
        self.chart_canvas.draw()
        toolbar = NavigationToolbar2Tk(self.chart_canvas, chart_frame, pack_toolbar=False)
        toolbar.update()
        toolbar.pack(fill="x")
        self.chart_canvas.get_tk_widget().pack(fill="both", expand=True)

        table_frame = ttk.LabelFrame(parent, text="历史列表", style="Section.TLabelframe")
        table_frame.pack(fill="both", expand=True)

        columns = ("recv_time", "device_id", "seq", "uptime_s", "temp_c", "ph", "turb_ntu", "alarm", "err")
        self.table = ttk.Treeview(table_frame, columns=columns, show="headings", height=12)
        headings = {
            "recv_time": "接收时间",
            "device_id": "设备编号",
            "seq": "序号",
            "uptime_s": "运行秒数",
            "temp_c": "温度(℃)",
            "ph": "pH",
            "turb_ntu": "浑浊度(NTU)",
            "alarm": "报警",
            "err": "错误码",
        }
        widths = {
            "recv_time": 165,
            "device_id": 100,
            "seq": 80,
            "uptime_s": 90,
            "temp_c": 90,
            "ph": 80,
            "turb_ntu": 120,
            "alarm": 70,
            "err": 80,
        }
        for column in columns:
            self.table.heading(column, text=headings[column])
            self.table.column(column, width=widths[column], anchor="center", stretch=False)

        yscroll = ttk.Scrollbar(table_frame, orient="vertical", command=self.table.yview)
        xscroll = ttk.Scrollbar(table_frame, orient="horizontal", command=self.table.xview)
        self.table.configure(yscrollcommand=yscroll.set, xscrollcommand=xscroll.set)
        self.table.grid(row=0, column=0, sticky="nsew")
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")
        table_frame.rowconfigure(0, weight=1)
        table_frame.columnconfigure(0, weight=1)

    def _build_config_tab(self, parent: ttk.Frame) -> None:
        top = ttk.Frame(parent)
        top.pack(fill="both", expand=True)

        general = ttk.LabelFrame(top, text="基本配置", style="Section.TLabelframe")
        general.grid(row=0, column=0, sticky="nsew", padx=(0, 10), pady=(0, 10))
        threshold = ttk.LabelFrame(top, text="阈值设置", style="Section.TLabelframe")
        threshold.grid(row=0, column=1, sticky="nsew", pady=(0, 10))
        calibration = ttk.LabelFrame(top, text="校准参数", style="Section.TLabelframe")
        calibration.grid(row=1, column=0, columnspan=2, sticky="nsew")

        top.columnconfigure(0, weight=1)
        top.columnconfigure(1, weight=1)
        top.rowconfigure(0, weight=1)
        top.rowconfigure(1, weight=1)

        self._add_config_row(general, "设备编号", self.latest_device_var, 0, readonly=True)
        self._add_config_row(general, "采样周期(s)", self.sample_period_var, 1)
        ttk.Button(general, text="下发采样周期", command=self.send_set_sample).grid(row=2, column=0, columnspan=2, sticky="ew", pady=(8, 0))

        self._add_config_row(threshold, "pH下限", self.ph_min_var, 0)
        self._add_config_row(threshold, "pH上限", self.ph_max_var, 1)
        self._add_config_row(threshold, "温度下限", self.temp_min_var, 2)
        self._add_config_row(threshold, "温度上限", self.temp_max_var, 3)
        self._add_config_row(threshold, "浑浊度上限", self.turb_max_var, 4)
        ttk.Button(threshold, text="下发阈值", command=self.send_set_threshold).grid(
            row=5, column=0, columnspan=2, sticky="ew", pady=(8, 0)
        )

        self._add_config_row(calibration, "pH K", self.ph_k_var, 0)
        self._add_config_row(calibration, "pH B", self.ph_b_var, 1)
        self._add_config_row(calibration, "浑浊 A", self.turb_a_var, 2)
        self._add_config_row(calibration, "浑浊 B", self.turb_b_var, 3)
        self._add_config_row(calibration, "浑浊 C", self.turb_c_var, 4)

        button_row = ttk.Frame(calibration)
        button_row.grid(row=5, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        ttk.Button(button_row, text="下发校准", command=self.send_set_cal).pack(side="left", fill="x", expand=True, padx=(0, 6))
        ttk.Button(button_row, text="下发全部配置", command=self.send_all_config).pack(side="left", fill="x", expand=True, padx=6)
        ttk.Button(button_row, text="从设备读取", command=self.send_get_config).pack(side="left", fill="x", expand=True, padx=(6, 0))

        self._set_group_padding(general)
        self._set_group_padding(threshold)
        self._set_group_padding(calibration)

    def _add_config_row(self, parent: ttk.LabelFrame, label: str, var: tk.StringVar, row: int, readonly: bool = False) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=(0, 8), pady=4)
        entry = ttk.Entry(parent, textvariable=var, width=24)
        if readonly:
            entry.state(["readonly"])
        entry.grid(row=row, column=1, sticky="ew", pady=4)
        parent.columnconfigure(1, weight=1)

    def _set_group_padding(self, parent: ttk.LabelFrame) -> None:
        for child in parent.winfo_children():
            try:
                child.configure()
            except tk.TclError:
                pass

    def _build_log_tab(self, parent: ttk.Frame) -> None:
        self.log_text = scrolledtext.ScrolledText(parent, height=24, wrap="word")
        self.log_text.pack(fill="both", expand=True)
        self.log_text.configure(state="disabled")

    def _build_footer(self, parent: ttk.Frame) -> None:
        footer = ttk.Frame(parent)
        footer.pack(fill="x", pady=(8, 0))
        ttk.Label(footer, text="默认数据库:").pack(side="left")
        ttk.Label(footer, text=str(self.db_path), foreground="#555555").pack(side="left", padx=(6, 0))

    def refresh_ports(self) -> None:
        ports = enumerate_ports()
        display_values = [info.display for info in ports]
        self.port_map = {info.display: info.device for info in ports}
        self.port_combo["values"] = display_values

        if display_values:
            current = self.port_combo.get()
            if current not in display_values:
                self.port_combo.set(display_values[0])
            self.connection_hint_var.set(f"发现 {len(display_values)} 个串口")
        else:
            self.port_combo.set("")
            self.connection_hint_var.set("未发现串口，请检查 USB 转串口或 LoRa 模块")

    def refresh_device_list(self) -> None:
        devices = self.store.list_device_ids()
        values = ["<全部>"] + devices
        self.query_device_combo["values"] = values
        if self.query_device_var.get() not in values:
            self.query_device_var.set("<全部>")

    def toggle_connection(self) -> None:
        if self.worker.connected:
            self.worker.disconnect()
            self.connect_btn.configure(text="连接")
            self.connected_var.set("未连接")
            self.connection_hint_var.set("串口已断开")
            return

        display = self.port_combo.get().strip()
        if not display:
            messagebox.showwarning(APP_TITLE, "请选择串口。")
            return

        port = self.port_map.get(display, display.split(" - ", 1)[0])
        try:
            baudrate = int(self.baud_combo.get())
        except ValueError:
            messagebox.showwarning(APP_TITLE, "波特率必须是数字。")
            return

        try:
            self.worker.connect(port, baudrate)
        except Exception as exc:
            messagebox.showerror(APP_TITLE, str(exc))
            return

        self.connect_btn.configure(text="断开")
        self.connected_var.set(f"连接中: {port} @ {baudrate}")
        self.connection_hint_var.set("正在等待设备数据")
        self.append_log(f"开始连接串口 {port} @ {baudrate}")

    def _poll_events(self) -> None:
        while True:
            try:
                event = self.event_queue.get_nowait()
            except queue.Empty:
                break
            self._handle_serial_event(event)
        self.after(100, self._poll_events)

    def _handle_serial_event(self, event: SerialEvent) -> None:
        if event.kind == "status":
            self.connection_hint_var.set(event.text)
            self.append_log(event.text)
            if "断开" in event.text:
                self.connect_btn.configure(text="连接")
                self.connected_var.set("未连接")
            elif "连接" in event.text:
                self.connect_btn.configure(text="断开")
                self.connected_var.set("已连接")
            return

        if event.kind == "error":
            self.append_log(event.text)
            self.connection_hint_var.set(event.text)
            self.connect_btn.configure(text="连接")
            self.connected_var.set("未连接")
            return

        if event.kind == "line":
            self._handle_line(event.text)

    def _handle_line(self, line: str) -> None:
        received_at = _now_text()
        packet = parse_packet_line(line)
        if packet.kind == "telemetry":
            record = telemetry_from_packet(packet, received_at)
            self.store.insert_telemetry(record)
            self._apply_telemetry(record)
            self.refresh_device_list()
            self.append_log(
                f"[{received_at}] 遥测 {record.device_id} seq={record.seq} temp={record.temp_c:.1f} "
                f"pH={record.ph:.2f} turb={record.turb_ntu:.1f} alarm={record.alarm} err={record.err}"
            )
        elif packet.kind == "config":
            record = config_from_packet(packet, received_at)
            self._apply_config(record)
            self.append_log(
                f"[{received_at}] 配置 {record.device_id} period={record.sample_period_s}s "
                f"pH={record.ph_min:.2f}~{record.ph_max:.2f} temp={record.temp_min:.1f}~{record.temp_max:.1f} "
                f"turb<={record.turb_max:.1f}"
            )
        elif packet.kind == "ack":
            cmd = str(packet.data.get("cmd", ""))
            ok = int(packet.data.get("ok", 0)) == 1
            err_text = str(packet.data.get("err", ""))
            self.append_log(f"[{received_at}] 应答 {cmd} {'OK' if ok else 'ERR'}{f' ({err_text})' if err_text else ''}")
        elif packet.kind in {"empty", "ignored"}:
            return
        else:
            self.append_log(f"[{received_at}] {line}")

    def _apply_telemetry(self, record: "TelemetryRecord") -> None:
        self.latest_device_var.set(record.device_id)
        self.device_id_var.set(record.device_id or "-")
        self.seq_var.set(str(record.seq))
        self.uptime_var.set(str(record.uptime_s))
        self.temp_var.set(_format_float(record.temp_c, 1))
        self.ph_var.set(_format_float(record.ph, 2))
        self.turb_var.set(_format_float(record.turb_ntu, 1))
        self.recv_time_var.set(record.received_at)
        self.err_var.set(f"0x{record.err:02X}")
        if record.alarm:
            self.alarm_var.set("报警")
            self.alarm_value_label.configure(bg="#c62828", fg="white")
        else:
            self.alarm_var.set("正常")
            self.alarm_value_label.configure(bg="#e7f5ea", fg="#146c2e")

    def _apply_config(self, record: ConfigRecord) -> None:
        self.latest_device_var.set(record.device_id)
        self.device_id_var.set(record.device_id or "-")
        self.sample_period_var.set(str(record.sample_period_s))
        self.ph_min_var.set(_format_float(record.ph_min, 2))
        self.ph_max_var.set(_format_float(record.ph_max, 2))
        self.temp_min_var.set(_format_float(record.temp_min, 1))
        self.temp_max_var.set(_format_float(record.temp_max, 1))
        self.turb_max_var.set(_format_float(record.turb_max, 1))
        self.ph_k_var.set(_format_float(record.ph_k, 2))
        self.ph_b_var.set(_format_float(record.ph_b, 2))
        self.turb_a_var.set(_format_float(record.turb_a, 1))
        self.turb_b_var.set(_format_float(record.turb_b, 1))
        self.turb_c_var.set(_format_float(record.turb_c, 1))

    def append_log(self, text: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _require_connected(self) -> bool:
        if not self.worker.connected:
            messagebox.showwarning(APP_TITLE, "请先连接串口。")
            return False
        return True

    def send_get_config(self) -> None:
        if not self._require_connected():
            return
        self._send_json_command("get_config")

    def send_sample_now(self) -> None:
        if not self._require_connected():
            return
        self._send_json_command("sample_now")

    def send_set_sample(self) -> bool:
        if not self._require_connected():
            return False
        try:
            period_s = int(float(self.sample_period_var.get()))
        except ValueError:
            messagebox.showwarning(APP_TITLE, "采样周期必须是数字。")
            return False
        if period_s < 5 or period_s > 86400:
            messagebox.showwarning(APP_TITLE, "采样周期范围应为 5 ~ 86400 秒。")
            return False
        self._send_json_command("set_sample", period_s=period_s)
        return True

    def send_set_threshold(self) -> bool:
        if not self._require_connected():
            return False
        try:
            payload = {
                "ph_min": float(self.ph_min_var.get()),
                "ph_max": float(self.ph_max_var.get()),
                "temp_min": float(self.temp_min_var.get()),
                "temp_max": float(self.temp_max_var.get()),
                "turb_max": float(self.turb_max_var.get()),
            }
        except ValueError:
            messagebox.showwarning(APP_TITLE, "阈值参数必须是数字。")
            return False
        if payload["ph_min"] >= payload["ph_max"]:
            messagebox.showwarning(APP_TITLE, "pH 下限必须小于上限。")
            return False
        if payload["temp_min"] >= payload["temp_max"]:
            messagebox.showwarning(APP_TITLE, "温度下限必须小于上限。")
            return False
        if payload["turb_max"] < 0:
            messagebox.showwarning(APP_TITLE, "浑浊度上限不能小于 0。")
            return False
        self._send_json_command("set_threshold", **payload)
        return True

    def send_set_cal(self) -> bool:
        if not self._require_connected():
            return False
        try:
            payload = {
                "ph_k": float(self.ph_k_var.get()),
                "ph_b": float(self.ph_b_var.get()),
                "turb_a": float(self.turb_a_var.get()),
                "turb_b": float(self.turb_b_var.get()),
                "turb_c": float(self.turb_c_var.get()),
            }
        except ValueError:
            messagebox.showwarning(APP_TITLE, "校准参数必须是数字。")
            return False
        self._send_json_command("set_cal", **payload)
        return True

    def send_all_config(self) -> None:
        if self.send_set_sample() and self.send_set_threshold() and self.send_set_cal():
            self.append_log("全部配置已下发")

    def _send_json_command(self, cmd: str, **fields: Any) -> None:
        try:
            payload = build_json_command(cmd, **fields)
            self.worker.send_bytes(payload)
            field_text = " ".join(f"{k}={v}" for k, v in fields.items())
            self.append_log(f"-> {cmd}{(' ' + field_text) if field_text else ''}")
        except Exception as exc:
            messagebox.showerror(APP_TITLE, str(exc))

    def query_history(self) -> None:
        try:
            device_id = self.query_device_var.get().strip()
            if device_id == "<全部>":
                device_id = ""
            start_time = _parse_time(self.query_start_var.get())
            end_time = _parse_time(self.query_end_var.get())
            limit_text = self.query_limit_var.get().strip()
            limit = int(limit_text) if limit_text else 0
            if limit < 0:
                raise ValueError("条数上限不能为负数")
        except ValueError as exc:
            messagebox.showwarning(APP_TITLE, f"查询条件无效: {exc}")
            return

        rows = self.store.query_history(
            device_id=device_id,
            start_time=start_time,
            end_time=end_time,
            limit=limit,
        )
        self.current_rows = rows
        self._render_history_table(rows)
        self._render_history_chart(rows)
        self.append_log(f"查询完成: {len(rows)} 条记录")

    def _render_history_table(self, rows: list[dict[str, Any]]) -> None:
        for item in self.table.get_children():
            self.table.delete(item)
        for row in rows:
            self.table.insert(
                "",
                "end",
                values=(
                    row.get("recv_time", ""),
                    row.get("device_id", ""),
                    row.get("seq", ""),
                    row.get("uptime_s", ""),
                    _format_float(float(row.get("temp_c", 0.0)), 1),
                    _format_float(float(row.get("ph", 0.0)), 2),
                    _format_float(float(row.get("turb_ntu", 0.0)), 1),
                    row.get("alarm", ""),
                    f"0x{int(row.get('err', 0)):02X}",
                ),
            )

    def _render_history_chart(self, rows: list[dict[str, Any]]) -> None:
        self.ax_temp.clear()
        self.ax_ph.clear()
        self.ax_turb.clear()

        if not rows:
            for ax, title in (
                (self.ax_temp, "温度"),
                (self.ax_ph, "pH"),
                (self.ax_turb, "浑浊度"),
            ):
                ax.set_title(f"{title} - 无数据")
                ax.grid(True, alpha=0.25)
            self.figure.tight_layout(pad=2.0)
            self.chart_canvas.draw_idle()
            return

        times = [datetime.fromisoformat(str(row["recv_time"])) for row in rows]
        temps = [float(row["temp_c"]) for row in rows]
        phs = [float(row["ph"]) for row in rows]
        turbs = [float(row["turb_ntu"]) for row in rows]

        self.ax_temp.plot(times, temps, color="#d32f2f", linewidth=1.8, marker="o", markersize=3)
        self.ax_temp.set_ylabel("℃")
        self.ax_temp.set_title("温度曲线")
        self.ax_temp.grid(True, alpha=0.25)

        self.ax_ph.plot(times, phs, color="#2e7d32", linewidth=1.8, marker="o", markersize=3)
        self.ax_ph.set_ylabel("pH")
        self.ax_ph.set_title("pH 曲线")
        self.ax_ph.grid(True, alpha=0.25)

        self.ax_turb.plot(times, turbs, color="#1565c0", linewidth=1.8, marker="o", markersize=3)
        self.ax_turb.set_ylabel("NTU")
        self.ax_turb.set_title("浑浊度曲线")
        self.ax_turb.grid(True, alpha=0.25)

        locator = AutoDateLocator()
        formatter = ConciseDateFormatter(locator)
        self.ax_turb.xaxis.set_major_locator(locator)
        self.ax_turb.xaxis.set_major_formatter(formatter)
        self.ax_turb.set_xlabel("接收时间")

        self.figure.tight_layout(pad=2.0)
        self.chart_canvas.draw_idle()

    def export_history(self) -> None:
        if not self.current_rows:
            messagebox.showinfo(APP_TITLE, "当前没有可导出的查询结果。")
            return

        default_name = f"history_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        file_path = filedialog.asksaveasfilename(
            title="导出历史数据",
            defaultextension=".csv",
            initialfile=default_name,
            filetypes=[("CSV 文件", "*.csv")],
        )
        if not file_path:
            return

        self.store.export_rows(self.current_rows, Path(file_path))
        self.append_log(f"历史数据已导出: {file_path}")
        messagebox.showinfo(APP_TITLE, "导出完成。")

    def _on_close(self) -> None:
        try:
            self.worker.disconnect()
        finally:
            self.store.close()
            self.destroy()


def main() -> None:
    app = WaterHostApp()
    app.mainloop()


if __name__ == "__main__":
    main()
