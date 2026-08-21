from __future__ import annotations

import csv
import sqlite3
from pathlib import Path
from typing import Any, Iterable

from .protocol import TelemetryRecord


class HistoryStore:
    def __init__(self, db_path: Path) -> None:
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.conn = sqlite3.connect(str(self.db_path))
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA foreign_keys = ON")
        self.conn.execute("PRAGMA journal_mode = WAL")
        self._ensure_schema()

    def _ensure_schema(self) -> None:
        self.conn.execute(
            """
            CREATE TABLE IF NOT EXISTS history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT NOT NULL,
                recv_time TEXT NOT NULL,
                seq INTEGER NOT NULL,
                uptime_s INTEGER NOT NULL,
                temp_c REAL NOT NULL,
                ph REAL NOT NULL,
                turb_ntu REAL NOT NULL,
                alarm INTEGER NOT NULL,
                err INTEGER NOT NULL,
                raw_json TEXT NOT NULL
            )
            """
        )
        self.conn.execute(
            """
            CREATE INDEX IF NOT EXISTS idx_history_device_time
            ON history(device_id, recv_time)
            """
        )
        self.conn.execute(
            """
            CREATE INDEX IF NOT EXISTS idx_history_time
            ON history(recv_time)
            """
        )
        self.conn.commit()

    def insert_telemetry(self, record: TelemetryRecord) -> None:
        self.conn.execute(
            """
            INSERT INTO history (
                device_id, recv_time, seq, uptime_s, temp_c,
                ph, turb_ntu, alarm, err, raw_json
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                record.device_id,
                record.received_at,
                record.seq,
                record.uptime_s,
                record.temp_c,
                record.ph,
                record.turb_ntu,
                record.alarm,
                record.err,
                record.raw_json,
            ),
        )
        self.conn.commit()

    def list_device_ids(self) -> list[str]:
        cur = self.conn.execute(
            """
            SELECT DISTINCT device_id
            FROM history
            ORDER BY device_id ASC
            """
        )
        return [str(row[0]) for row in cur.fetchall() if row[0]]

    def query_history(
        self,
        device_id: str = "",
        start_time: str = "",
        end_time: str = "",
        limit: int = 0,
    ) -> list[dict[str, Any]]:
        sql = [
            "SELECT id, device_id, recv_time, seq, uptime_s, temp_c, ph, turb_ntu, alarm, err",
            "FROM history",
            "WHERE 1 = 1",
        ]
        params: list[Any] = []

        if device_id:
            sql.append("AND device_id = ?")
            params.append(device_id)
        if start_time:
            sql.append("AND recv_time >= ?")
            params.append(start_time)
        if end_time:
            sql.append("AND recv_time <= ?")
            params.append(end_time)

        sql.append("ORDER BY recv_time ASC, id ASC")
        if limit and limit > 0:
            sql.append("LIMIT ?")
            params.append(limit)

        cur = self.conn.execute("\n".join(sql), params)
        return [dict(row) for row in cur.fetchall()]

    def export_rows(self, rows: Iterable[dict[str, Any]], csv_path: Path) -> None:
        csv_path = Path(csv_path)
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        fieldnames = ["id", "device_id", "recv_time", "seq", "uptime_s", "temp_c", "ph", "turb_ntu", "alarm", "err"]
        with csv_path.open("w", newline="", encoding="utf-8-sig") as fh:
            writer = csv.DictWriter(fh, fieldnames=fieldnames)
            writer.writeheader()
            for row in rows:
                writer.writerow({key: row.get(key, "") for key in fieldnames})

    def close(self) -> None:
        self.conn.close()

