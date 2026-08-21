from __future__ import annotations

import sys

from PySide6.QtWidgets import QApplication

from app.ui import MainWindow, apply_application_theme


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("LockerControl")
    app.setOrganizationName("Don1ng")
    apply_application_theme(app)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
