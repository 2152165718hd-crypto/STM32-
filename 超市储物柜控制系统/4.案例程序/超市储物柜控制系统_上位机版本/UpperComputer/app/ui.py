from __future__ import annotations

import datetime as _dt
import html
from dataclasses import asdict

from PySide6.QtCore import QSettings, Qt, Signal, QObject, QSize
from PySide6.QtGui import QColor, QFont, QPalette, QTextCursor
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QTabWidget,
    QTableWidget,
    QTableWidgetItem,
    QTextBrowser,
    QTimeEdit,
    QVBoxLayout,
    QWidget,
)

from .network import ConnectionConfig, DeviceWorker
from .protocol import (
    CMD_EMERGENCY_UNLOCK,
    CMD_LOCKER_CONTROL,
    DeviceSnapshot,
    ProtocolMode,
    RecordEntry,
    format_result_code,
)


class UiBus(QObject):
    configure = Signal(object)
    connectNow = Signal()
    disconnectNow = Signal()
    requestStatus = Signal()
    requestHeartbeat = Signal()
    requestLockerControl = Signal(int, int, int)
    requestClearAlarm = Signal()
    requestEmergencyUnlock = Signal()
    requestExport = Signal()
    requestSetTime = Signal(object)
    shutdown = Signal()


def apply_application_theme(app) -> None:
    app.setStyle("Fusion")

    palette = QPalette()
    palette.setColor(QPalette.Window, QColor("#081018"))
    palette.setColor(QPalette.WindowText, QColor("#EAF2FF"))
    palette.setColor(QPalette.Base, QColor("#0B1521"))
    palette.setColor(QPalette.AlternateBase, QColor("#111C2B"))
    palette.setColor(QPalette.ToolTipBase, QColor("#EAF2FF"))
    palette.setColor(QPalette.ToolTipText, QColor("#0B1521"))
    palette.setColor(QPalette.Text, QColor("#EAF2FF"))
    palette.setColor(QPalette.Button, QColor("#172232"))
    palette.setColor(QPalette.ButtonText, QColor("#EAF2FF"))
    palette.setColor(QPalette.BrightText, QColor("#FF5B5B"))
    palette.setColor(QPalette.Highlight, QColor("#19C3D6"))
    palette.setColor(QPalette.HighlightedText, QColor("#081018"))
    app.setPalette(palette)
    app.setFont(QFont("Microsoft YaHei UI", 10))

    app.setStyleSheet(
        """
        QWidget {
            color: #EAF2FF;
            font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
            font-size: 10pt;
        }
        QMainWindow {
            background: #081018;
        }
        QFrame#Card {
            background: #101A27;
            border: 1px solid #223449;
            border-radius: 16px;
        }
        QLabel#Title {
            font-size: 24px;
            font-weight: 700;
            color: #F8FBFF;
        }
        QLabel#Subtitle {
            color: #99AFC7;
            font-size: 10pt;
        }
        QLabel#SectionTitle {
            color: #D6E4FF;
            font-size: 11pt;
            font-weight: 700;
            padding-bottom: 4px;
        }
        QLabel#Chip {
            padding: 6px 10px;
            border-radius: 999px;
            background: #132336;
            border: 1px solid #2C4664;
            color: #B7CAEA;
        }
        QPushButton {
            background: #1B2738;
            border: 1px solid #2F4764;
            border-radius: 10px;
            padding: 8px 14px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #25374C;
            border-color: #3D6EA8;
        }
        QPushButton:pressed {
            background: #13202E;
        }
        QPushButton:disabled {
            color: #687B95;
            background: #101823;
            border-color: #1E2937;
        }
        QPushButton#PrimaryButton {
            background: #0F766E;
            border-color: #14B8A6;
        }
        QPushButton#DangerButton {
            background: #7F1D1D;
            border-color: #FB7185;
        }
        QPushButton#AccentButton {
            background: #123B5D;
            border-color: #2FA8E0;
        }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTimeEdit {
            background: #0B1521;
            border: 1px solid #2A415C;
            border-radius: 8px;
            padding: 6px 8px;
            selection-background-color: #19C3D6;
        }
        QComboBox::drop-down {
            border: 0px;
            width: 24px;
        }
        QTableWidget, QTextBrowser {
            background: #09111A;
            border: 1px solid #223449;
            border-radius: 12px;
        }
        QHeaderView::section {
            background: #132131;
            color: #DDE9F8;
            padding: 6px 8px;
            border: none;
            font-weight: 600;
        }
        QTableWidget::item {
            padding: 4px;
        }
        QTabWidget::pane {
            border: 1px solid #223449;
            border-radius: 12px;
            background: #09111A;
        }
        QTabBar::tab {
            background: #132131;
            color: #B6CAE8;
            padding: 8px 14px;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background: #1E3146;
            color: #F8FBFF;
        }
        """
    )


def _create_card(title: str) -> tuple[QFrame, QVBoxLayout]:
    frame = QFrame()
    frame.setObjectName("Card")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(14, 14, 14, 14)
    layout.setSpacing(10)
    label = QLabel(title)
    label.setObjectName("SectionTitle")
    layout.addWidget(label)
    return frame, layout


def _state_color(occupied: bool, relay: bool, alarm: bool) -> tuple[str, str]:
    if alarm:
        return "#7F1D1D", "#FB7185"
    if relay:
        return "#9A3412", "#FDBA74"
    if occupied:
        return "#1D4ED8", "#93C5FD"
    return "#14532D", "#86EFAC"


def _format_uptime(ms: int) -> str:
    total = max(0, ms // 1000)
    h, rem = divmod(total, 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"


class LockerCellButton(QPushButton):
    def __init__(self, locker_id: int, parent=None) -> None:
        super().__init__(parent)
        self.locker_id = locker_id
        self.setCheckable(True)
        self.setMinimumSize(QSize(92, 74))
        self.setCursor(Qt.PointingHandCursor)
        self._occupied = False
        self._relay = False
        self._alarm = False
        self._update_text()
        self._update_style()

    def set_state(self, occupied: bool, relay: bool, alarm: bool, selected: bool) -> None:
        self._occupied = occupied
        self._relay = relay
        self._alarm = alarm
        self.setChecked(selected)
        self._update_text()
        self._update_style()

    def _update_text(self) -> None:
        if self._alarm:
            state = "ALARM"
        elif self._relay:
            state = "OPEN" if self._occupied else "FORCE"
        elif self._occupied:
            state = "OCCUPIED"
        else:
            state = "EMPTY"
        self.setText(f"{self.locker_id:02d}\n{state}")

    def _update_style(self) -> None:
        bg, border = _state_color(self._occupied, self._relay, self._alarm)
        if self.isChecked():
            border = "#19C3D6"
        self.setStyleSheet(
            f"""
            QPushButton {{
                background: {bg};
                border: 2px solid {border};
                border-radius: 14px;
                color: #F8FBFF;
                font-size: 13px;
                font-weight: 700;
                padding: 8px;
                text-align: center;
            }}
            QPushButton:checked {{
                border-color: #19C3D6;
            }}
            """
        )


class LockerGridWidget(QFrame):
    lockerSelected = Signal(int)

    def __init__(self, count: int = 16, parent=None) -> None:
        super().__init__(parent)
        self.setObjectName("Card")
        self._count = count
        self._selected = 1
        self._occupied_mask = 0
        self._relay_mask = 0
        self._alarm = 0
        self._cells: list[LockerCellButton] = []

        outer = QVBoxLayout(self)
        outer.setContentsMargins(14, 14, 14, 14)
        outer.setSpacing(10)
        title = QLabel("储物柜网格")
        title.setObjectName("SectionTitle")
        outer.addWidget(title)

        self._grid = QGridLayout()
        self._grid.setSpacing(10)
        outer.addLayout(self._grid)

        for index in range(count):
            locker_id = index + 1
            button = LockerCellButton(locker_id)
            button.clicked.connect(lambda _checked=False, lid=locker_id: self._select(lid))
            self._cells.append(button)
            self._grid.addWidget(button, index // 4, index % 4)

        self._foot = QLabel("点击任意柜位后，再执行开/关/紧急操作")
        self._foot.setObjectName("Subtitle")
        outer.addWidget(self._foot)

    def _select(self, locker_id: int) -> None:
        self._selected = locker_id
        self._refresh()
        self.lockerSelected.emit(locker_id)

    def selected_locker(self) -> int:
        return self._selected

    def set_selected(self, locker_id: int) -> None:
        if 1 <= locker_id <= self._count:
            self._selected = locker_id
            self._refresh()

    def set_snapshot(self, snapshot: DeviceSnapshot | None) -> None:
        if snapshot is None:
            return
        self._occupied_mask = snapshot.occupied_mask
        self._relay_mask = snapshot.relay_mask
        self._alarm = snapshot.alarm
        if not (1 <= self._selected <= self._count):
            self._selected = max(1, snapshot.selected_locker or 1)
        self._refresh()

    def _refresh(self) -> None:
        for button in self._cells:
            mask = 1 << (button.locker_id - 1)
            button.set_state(
                bool(self._occupied_mask & mask),
                bool(self._relay_mask & mask),
                bool(self._alarm),
                button.locker_id == self._selected,
            )


class LogBrowser(QTextBrowser):
    def append_log(self, level: str, direction: str, message: str) -> None:
        palette = {
            "tx": "#7DD3FC",
            "rx": "#86EFAC",
            "retry": "#FDBA74",
            "warn": "#FBBF24",
            "error": "#F87171",
            "info": "#E5E7EB",
            "sys": "#C4B5FD",
        }
        color = palette.get(level.lower(), "#E5E7EB")
        timestamp = _dt.datetime.now().strftime("%H:%M:%S")
        self.append(
            f'<span style="color:{color}">[{timestamp}] [{html.escape(direction)}] {html.escape(message)}</span>'
        )
        self.moveCursor(QTextCursor.End)

    def append_raw(self, direction: str, data: bytes) -> None:
        timestamp = _dt.datetime.now().strftime("%H:%M:%S")
        hex_text = " ".join(f"{b:02X}" for b in data)
        self.append(
            f'<span style="color:#94A3B8">[{timestamp}] [{html.escape(direction)}] {html.escape(hex_text)}</span>'
        )
        self.moveCursor(QTextCursor.End)


class RecordTable(QTableWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(0, 5, parent)
        self.setHorizontalHeaderLabels(["时间", "操作", "柜号", "人脸", "结果"])
        self.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.verticalHeader().setVisible(False)
        self.setAlternatingRowColors(True)
        self.setEditTriggers(QTableWidget.NoEditTriggers)
        self.setSelectionBehavior(QTableWidget.SelectRows)
        self.setSelectionMode(QTableWidget.SingleSelection)

    def clear_records(self) -> None:
        self.setRowCount(0)

    def append_record(self, record: RecordEntry) -> None:
        row = self.rowCount()
        self.insertRow(row)
        values = [
            record.timestamp,
            record.label(),
            f"{record.locker_id:02d}" if record.locker_id else "--",
            str(record.face_id),
            "成功" if record.result else "失败",
        ]
        for col, value in enumerate(values):
            self.setItem(row, col, QTableWidgetItem(value))
        self.scrollToBottom()


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("超市储物柜控制中心")
        self.resize(1520, 920)
        self._settings = QSettings()
        self._last_snapshot: DeviceSnapshot | None = None
        self._bus = UiBus()
        self._worker = DeviceWorker()
        self._thread = None

        self._build_ui()
        self._load_settings()
        self._setup_worker()
        self._update_mode_capabilities()
        self._update_connection_ui("idle", False)

    def _build_ui(self) -> None:
        root = QWidget()
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(18, 18, 18, 18)
        root_layout.setSpacing(14)

        header = QFrame()
        header.setObjectName("Card")
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(18, 18, 18, 18)
        header_layout.setSpacing(16)
        title_box = QVBoxLayout()
        title = QLabel("超市储物柜控制中心")
        title.setObjectName("Title")
        subtitle = QLabel("Binary TCP dashboard with legacy JSON fallback")
        subtitle.setObjectName("Subtitle")
        title_box.addWidget(title)
        title_box.addWidget(subtitle)
        header_layout.addLayout(title_box, 1)

        self._state_chip = QLabel("DISCONNECTED")
        self._state_chip.setObjectName("Chip")
        self._proto_chip = QLabel("MODE")
        self._proto_chip.setObjectName("Chip")
        self._addr_chip = QLabel("ADDR 0x01")
        self._addr_chip.setObjectName("Chip")
        header_layout.addWidget(self._state_chip)
        header_layout.addWidget(self._proto_chip)
        header_layout.addWidget(self._addr_chip)
        root_layout.addWidget(header)

        body = QSplitter(Qt.Horizontal)
        body.setChildrenCollapsible(False)
        root_layout.addWidget(body, 1)

        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.setSpacing(14)
        body.addWidget(left)

        right = QSplitter(Qt.Vertical)
        right.setChildrenCollapsible(False)
        body.addWidget(right)
        body.setSizes([760, 720])

        conn_card, conn_layout = _create_card("连接与参数")
        left_layout.addWidget(conn_card)
        conn_grid = QGridLayout()
        conn_grid.setHorizontalSpacing(12)
        conn_grid.setVerticalSpacing(10)
        conn_layout.addLayout(conn_grid)

        self.host_edit = QLineEdit()
        self.port_spin = QSpinBox()
        self.port_spin.setRange(1, 65535)
        self.addr_spin = QSpinBox()
        self.addr_spin.setRange(1, 255)
        self.mode_combo = QComboBox()
        self.mode_combo.addItem("Binary", ProtocolMode.BINARY.value)
        self.mode_combo.addItem("Legacy JSON", ProtocolMode.LEGACY_JSON.value)
        self.heartbeat_spin = QSpinBox()
        self.heartbeat_spin.setRange(500, 60000)
        self.heartbeat_spin.setSingleStep(500)
        self.timeout_spin = QSpinBox()
        self.timeout_spin.setRange(200, 15000)
        self.timeout_spin.setSingleStep(100)
        self.retry_spin = QSpinBox()
        self.retry_spin.setRange(0, 10)
        self.reconnect_spin = QSpinBox()
        self.reconnect_spin.setRange(500, 60000)
        self.reconnect_spin.setSingleStep(500)
        self.auto_reconnect = QCheckBox("自动重连")
        self.auto_reconnect.setChecked(True)
        self.auto_status = QCheckBox("连接后自动同步状态")
        self.auto_status.setChecked(True)

        row = 0
        for label_text, widget in (
            ("主机", self.host_edit),
            ("端口", self.port_spin),
            ("设备地址", self.addr_spin),
            ("协议模式", self.mode_combo),
            ("心跳间隔(ms)", self.heartbeat_spin),
            ("超时(ms)", self.timeout_spin),
            ("重试次数", self.retry_spin),
            ("重连间隔(ms)", self.reconnect_spin),
        ):
            conn_grid.addWidget(QLabel(label_text), row // 2, (row % 2) * 2)
            conn_grid.addWidget(widget, row // 2, (row % 2) * 2 + 1)
            row += 1

        conn_grid.addWidget(self.auto_reconnect, 4, 0)
        conn_grid.addWidget(self.auto_status, 4, 1)

        btn_row = QHBoxLayout()
        self.connect_btn = QPushButton("连接")
        self.connect_btn.setObjectName("PrimaryButton")
        self.disconnect_btn = QPushButton("断开")
        self.disconnect_btn.setObjectName("AccentButton")
        self.sync_btn = QPushButton("立即同步")
        self.sync_btn.setObjectName("AccentButton")
        self.heartbeat_btn = QPushButton("心跳")
        self.heartbeat_btn.setObjectName("AccentButton")
        btn_row.addWidget(self.connect_btn)
        btn_row.addWidget(self.disconnect_btn)
        btn_row.addWidget(self.sync_btn)
        btn_row.addWidget(self.heartbeat_btn)
        conn_layout.addLayout(btn_row)

        status_card, status_layout = _create_card("设备状态")
        left_layout.addWidget(status_card)
        status_grid = QGridLayout()
        status_grid.setHorizontalSpacing(12)
        status_grid.setVerticalSpacing(10)
        status_layout.addLayout(status_grid)
        self.summary_labels: dict[str, QLabel] = {}
        summary_items = [
            ("连接", "离线"),
            ("协议", "Binary"),
            ("占用", "0"),
            ("继电器", "0"),
            ("门状态", "0"),
            ("报警", "0"),
            ("记录数", "0"),
            ("运行时长", "00:00:00"),
        ]
        for idx, (name, value) in enumerate(summary_items):
            key = name
            row_index = idx // 2
            col_index = (idx % 2) * 2
            status_grid.addWidget(QLabel(name), row_index, col_index)
            value_label = QLabel(value)
            value_label.setObjectName("Chip")
            status_grid.addWidget(value_label, row_index, col_index + 1)
            self.summary_labels[key] = value_label

        self.grid_widget = LockerGridWidget(16)
        left_layout.addWidget(self.grid_widget, 1)

        action_card, action_layout = _create_card("操作区")
        left_layout.addWidget(action_card)
        action_grid = QGridLayout()
        action_grid.setHorizontalSpacing(10)
        action_grid.setVerticalSpacing(10)
        action_layout.addLayout(action_grid)
        self.open_selected_btn = QPushButton("单箱开箱")
        self.open_selected_btn.setObjectName("PrimaryButton")
        self.close_selected_btn = QPushButton("单箱关锁")
        self.close_selected_btn.setObjectName("AccentButton")
        self.open_all_btn = QPushButton("一键全部开箱")
        self.open_all_btn.setObjectName("PrimaryButton")
        self.emergency_btn = QPushButton("紧急强制解锁")
        self.emergency_btn.setObjectName("DangerButton")
        self.clear_alarm_btn = QPushButton("清除报警")
        self.clear_alarm_btn.setObjectName("AccentButton")
        self.export_btn = QPushButton("导出记录")
        self.export_btn.setObjectName("AccentButton")
        self.set_time_btn = QPushButton("同步时间")
        self.set_time_btn.setObjectName("AccentButton")
        self.clear_records_btn = QPushButton("清空表格")
        self.clear_records_btn.setObjectName("AccentButton")
        buttons = [
            self.open_selected_btn,
            self.close_selected_btn,
            self.open_all_btn,
            self.emergency_btn,
            self.clear_alarm_btn,
            self.export_btn,
            self.set_time_btn,
            self.clear_records_btn,
        ]
        for idx, button in enumerate(buttons):
            action_grid.addWidget(button, idx // 2, idx % 2)

        right_logs, log_layout = _create_card("通信日志")
        self.log_tabs = QTabWidget()
        self.parsed_log = LogBrowser()
        self.raw_log = LogBrowser()
        self.log_tabs.addTab(self.parsed_log, "解析日志")
        self.log_tabs.addTab(self.raw_log, "原始帧")
        log_layout.addWidget(self.log_tabs)
        right.addWidget(right_logs)

        records_card, records_layout = _create_card("记录列表")
        self.record_table = RecordTable()
        records_layout.addWidget(self.record_table)
        right.addWidget(records_card)

        self.setCentralWidget(root)
        self.statusBar().showMessage("准备就绪")

        self.connect_btn.clicked.connect(self._on_connect_clicked)
        self.disconnect_btn.clicked.connect(self._on_disconnect_clicked)
        self.sync_btn.clicked.connect(lambda: self._bus.requestStatus.emit())
        self.heartbeat_btn.clicked.connect(lambda: self._bus.requestHeartbeat.emit())
        self.open_selected_btn.clicked.connect(self._on_open_selected)
        self.close_selected_btn.clicked.connect(self._on_close_selected)
        self.open_all_btn.clicked.connect(self._on_open_all)
        self.emergency_btn.clicked.connect(self._on_emergency)
        self.clear_alarm_btn.clicked.connect(lambda: self._bus.requestClearAlarm.emit())
        self.export_btn.clicked.connect(lambda: self._bus.requestExport.emit())
        self.set_time_btn.clicked.connect(self._on_set_time)
        self.clear_records_btn.clicked.connect(self.record_table.clear_records)
        self.grid_widget.lockerSelected.connect(self._on_locker_selected)
        self.mode_combo.currentIndexChanged.connect(self._save_settings)

    def _setup_worker(self) -> None:
        from PySide6.QtCore import QThread

        self._thread = QThread(self)
        self._worker.moveToThread(self._thread)
        self._thread.started.connect(self._worker.start)
        self._bus.configure.connect(self._worker.configure)
        self._bus.connectNow.connect(self._worker.connect_now)
        self._bus.disconnectNow.connect(self._worker.disconnect_now)
        self._bus.requestStatus.connect(self._worker.request_status)
        self._bus.requestHeartbeat.connect(self._worker.request_heartbeat)
        self._bus.requestLockerControl.connect(self._worker.send_locker_control)
        self._bus.requestClearAlarm.connect(self._worker.clear_alarm)
        self._bus.requestEmergencyUnlock.connect(self._worker.emergency_unlock)
        self._bus.requestExport.connect(self._worker.export_records)
        self._bus.requestSetTime.connect(self._worker.set_time)
        self._bus.shutdown.connect(self._worker.stop)

        self._worker.connectionStateChanged.connect(self._on_connection_state)
        self._worker.connectedChanged.connect(self._on_connected_changed)
        self._worker.protocolModeChanged.connect(self._on_protocol_mode_changed)
        self._worker.logMessage.connect(self._on_log_message)
        self._worker.rawFrame.connect(self._on_raw_frame)
        self._worker.snapshotReceived.connect(self._on_snapshot_received)
        self._worker.recordReceived.connect(self._on_record_received)
        self._worker.requestCompleted.connect(self._on_request_completed)
        self._worker.requestFailed.connect(self._on_request_failed)
        self._worker.exportProgress.connect(self._on_export_progress)
        self._worker.metricsChanged.connect(self._on_metrics_changed)
        self._thread.start()

    def _load_settings(self) -> None:
        self.host_edit.setText(self._settings.value("upper/host", "192.168.4.1"))
        self.port_spin.setValue(int(self._settings.value("upper/port", 9000)))
        self.addr_spin.setValue(int(self._settings.value("upper/addr", 1)))
        mode_name = str(self._settings.value("upper/mode", ProtocolMode.BINARY.value))
        mode_index = self.mode_combo.findData(mode_name if mode_name in ProtocolMode._value2member_map_ else ProtocolMode.BINARY.value)
        self.mode_combo.setCurrentIndex(max(0, mode_index))
        self.heartbeat_spin.setValue(int(self._settings.value("upper/heartbeat", 5000)))
        self.timeout_spin.setValue(int(self._settings.value("upper/timeout", 1500)))
        self.retry_spin.setValue(int(self._settings.value("upper/retries", 3)))
        self.reconnect_spin.setValue(int(self._settings.value("upper/reconnect", 3000)))
        self.auto_reconnect.setChecked(self._settings.value("upper/auto_reconnect", True, type=bool))
        self.auto_status.setChecked(self._settings.value("upper/auto_status", True, type=bool))

    def _save_settings(self) -> None:
        mode = self._current_protocol_mode()
        self._settings.setValue("upper/host", self.host_edit.text().strip())
        self._settings.setValue("upper/port", self.port_spin.value())
        self._settings.setValue("upper/addr", self.addr_spin.value())
        self._settings.setValue("upper/mode", mode.value)
        self._settings.setValue("upper/heartbeat", self.heartbeat_spin.value())
        self._settings.setValue("upper/timeout", self.timeout_spin.value())
        self._settings.setValue("upper/retries", self.retry_spin.value())
        self._settings.setValue("upper/reconnect", self.reconnect_spin.value())
        self._settings.setValue("upper/auto_reconnect", self.auto_reconnect.isChecked())
        self._settings.setValue("upper/auto_status", self.auto_status.isChecked())
        self._apply_config()

    def _apply_config(self) -> None:
        mode = self._current_protocol_mode()
        config = ConnectionConfig(
            host=self.host_edit.text().strip() or "192.168.4.1",
            port=self.port_spin.value(),
            addr=self.addr_spin.value(),
            protocol_mode=mode,
            heartbeat_interval_ms=self.heartbeat_spin.value(),
            response_timeout_ms=self.timeout_spin.value(),
            retry_count=self.retry_spin.value(),
            reconnect_interval_ms=self.reconnect_spin.value(),
            auto_reconnect=self.auto_reconnect.isChecked(),
            auto_status_on_connect=self.auto_status.isChecked(),
        )
        self._bus.configure.emit(config)
        self._addr_chip.setText(f"ADDR 0x{config.addr:02X}")

    def _update_mode_capabilities(self) -> None:
        mode = self._current_protocol_mode()
        if mode == ProtocolMode.LEGACY_JSON:
            self.open_all_btn.setToolTip("Legacy mode uses JSON payloads; new firmware still supports this.")
            self.emergency_btn.setToolTip("Supported by patched firmware; older firmware will ignore it.")
        else:
            self.open_all_btn.setToolTip("Open all lockers.")
            self.emergency_btn.setToolTip("Emergency unlock all lockers.")

    def _update_connection_ui(self, state: str, connected: bool) -> None:
        text = state.upper()
        chip_color = {
            "connected": ("#0F766E", "#14B8A6"),
            "connecting": ("#7C2D12", "#FDBA74"),
            "reconnecting": ("#7C2D12", "#FDBA74"),
            "stopped": ("#374151", "#6B7280"),
            "disconnected": ("#7F1D1D", "#FB7185"),
            "idle": ("#132336", "#2C4664"),
        }.get(state, ("#132336", "#2C4664"))
        self._state_chip.setText(text)
        self._state_chip.setStyleSheet(
            f"QLabel#Chip {{ background: {chip_color[0]}; border: 1px solid {chip_color[1]}; }}"
        )
        self.connect_btn.setText("断开" if connected else "连接")
        self.disconnect_btn.setEnabled(connected)
        for button in (
            self.sync_btn,
            self.heartbeat_btn,
            self.open_selected_btn,
            self.close_selected_btn,
            self.open_all_btn,
            self.emergency_btn,
            self.clear_alarm_btn,
            self.export_btn,
            self.set_time_btn,
        ):
            button.setEnabled(connected)

    def _collect_config(self) -> ConnectionConfig:
        mode = self._current_protocol_mode()
        return ConnectionConfig(
            host=self.host_edit.text().strip() or "192.168.4.1",
            port=self.port_spin.value(),
            addr=self.addr_spin.value(),
            protocol_mode=mode,
            heartbeat_interval_ms=self.heartbeat_spin.value(),
            response_timeout_ms=self.timeout_spin.value(),
            retry_count=self.retry_spin.value(),
            reconnect_interval_ms=self.reconnect_spin.value(),
            auto_reconnect=self.auto_reconnect.isChecked(),
            auto_status_on_connect=self.auto_status.isChecked(),
        )

    def _current_protocol_mode(self) -> ProtocolMode:
        raw = self.mode_combo.currentData()
        try:
            return ProtocolMode(str(raw))
        except ValueError:
            return ProtocolMode.BINARY

    def _on_connect_clicked(self) -> None:
        self._save_settings()
        if self.connect_btn.text() == "断开":
            self._bus.disconnectNow.emit()
            return
        self._bus.connectNow.emit()

    def _on_disconnect_clicked(self) -> None:
        self._bus.disconnectNow.emit()

    def _on_locker_selected(self, locker_id: int) -> None:
        self.grid_widget.set_selected(locker_id)
        self.statusBar().showMessage(f"选中柜位 {locker_id:02d}")

    def _on_open_selected(self) -> None:
        locker = self.grid_widget.selected_locker()
        self._bus.requestLockerControl.emit(1 << (locker - 1), 1, 0)

    def _on_close_selected(self) -> None:
        locker = self.grid_widget.selected_locker()
        self._bus.requestLockerControl.emit(1 << (locker - 1), 2, 0)

    def _on_open_all(self) -> None:
        self._bus.requestLockerControl.emit(0xFFFF, 1, 0)

    def _on_emergency(self) -> None:
        if QMessageBox.question(
            self,
            "确认紧急解锁",
            "确定要执行紧急强制解锁吗？这会打开所有柜门并清除报警。",
        ) == QMessageBox.Yes:
            self._bus.requestEmergencyUnlock.emit()

    def _on_set_time(self) -> None:
        self._bus.requestSetTime.emit(_dt.datetime.now())

    def _on_connection_state(self, state: str) -> None:
        connected = state == "connected"
        self._update_connection_ui(state, connected)
        self.statusBar().showMessage(state)

    def _on_connected_changed(self, connected: bool) -> None:
        self._update_connection_ui("connected" if connected else "disconnected", connected)

    def _on_protocol_mode_changed(self, mode_name: str) -> None:
        self._proto_chip.setText(f"MODE {mode_name.upper()}")
        self._proto_chip.setStyleSheet(
            "QLabel#Chip { background: #132336; border: 1px solid #2C4664; }"
        )
        self._update_mode_capabilities()

    def _on_log_message(self, level: str, direction: str, message: str) -> None:
        self.parsed_log.append_log(level, direction, message)

    def _on_raw_frame(self, data: bytes, direction: str) -> None:
        self.raw_log.append_raw(direction, data)

    def _on_snapshot_received(self, snapshot: DeviceSnapshot) -> None:
        self._last_snapshot = snapshot
        self.grid_widget.set_snapshot(snapshot)
        self.summary_labels["连接"].setText("在线" if snapshot.tcp_connected else "离线")
        self.summary_labels["协议"].setText("Binary" if snapshot.protocol_mode == 1 else "Legacy")
        self.summary_labels["占用"].setText(f"{snapshot.occupied_count}/{16}")
        self.summary_labels["继电器"].setText(f"{snapshot.relay_count}/{16}")
        self.summary_labels["门状态"].setText(f"{snapshot.door_count}")
        self.summary_labels["报警"].setText("ON" if snapshot.alarm else "OFF")
        self.summary_labels["记录数"].setText(str(snapshot.record_count))
        self.summary_labels["运行时长"].setText(_format_uptime(snapshot.uptime_ms))

    def _on_record_received(self, record: RecordEntry) -> None:
        self.record_table.append_record(record)
        self.parsed_log.append_log("info", "RX", f"record {record.label()} locker={record.locker_id:02d}")

    def _on_request_completed(self, seq: int, cmd: int, message: str) -> None:
        self.parsed_log.append_log("info", "SYS", f"done seq=0x{seq:04X} {command_name(cmd)} {message}")
        self.statusBar().showMessage(message)

    def _on_request_failed(self, seq: int, cmd: int, message: str) -> None:
        self.parsed_log.append_log("error", "SYS", f"fail seq=0x{seq:04X} {command_name(cmd)} {message}")
        self.statusBar().showMessage(message)

    def _on_export_progress(self, received: int, total: int) -> None:
        if total > 0:
            self.statusBar().showMessage(f"记录导出 {received}/{total}")

    def _on_metrics_changed(self, metrics: object) -> None:
        if isinstance(metrics, dict):
            text = (
                f"occupied={metrics.get('occupied', 0)} relay={metrics.get('relay', 0)} "
                f"door={metrics.get('door', 0)} alarm={metrics.get('alarm', 0)} records={metrics.get('records', 0)}"
            )
            self.summary_labels["占用"].setToolTip(text)

    def closeEvent(self, event) -> None:  # noqa: N802
        self._save_settings()
        self._bus.shutdown.emit()
        if self._thread is not None:
            self._thread.quit()
            self._thread.wait(2000)
        super().closeEvent(event)
