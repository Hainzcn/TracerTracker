"""
顶部工具栏组件：串口和 UDP 连接控制。
"""

import logging

from PySide6.QtWidgets import (
    QWidget, QHBoxLayout, QLabel, QPushButton, QSpinBox,
    QComboBox, QStyledItemDelegate, QAbstractItemView,
)
from PySide6.QtCore import Qt, Signal, QSize
import serial.tools.list_ports

from src.ui.styles import (
    STYLE_LABEL, STYLE_COMBO, STYLE_SPINBOX,
    STYLE_BTN_IDLE, STYLE_BTN_ACTIVE, TOP_BAR_STYLE,
)

logger = logging.getLogger(__name__)


class _CompactItemDelegate(QStyledItemDelegate):
    """Item delegate that enforces a fixed row height in combo box dropdowns."""

    def __init__(self, height=20, parent=None):
        super().__init__(parent)
        self._height = height

    def sizeHint(self, option, index):
        size = super().sizeHint(option, index)
        size.setHeight(self._height)
        return size


class _SeamlessComboBox(QComboBox):
    """QComboBox whose popup tucks under the rounded bottom corners."""

    _radius = 4

    def showPopup(self):
        super().showPopup()
        popup = self.view().window()
        geo = popup.geometry()
        geo.setTop(geo.top() - self._radius)
        popup.setGeometry(geo)
        if popup.layout():
            popup.layout().setContentsMargins(0, self._radius, 0, 0)


class ToolBar(QWidget):
    """顶部工具栏，提供串口和 UDP 连接控制。"""

    serial_start_requested = Signal(str, int, str, int, int, int)
    serial_stop_requested = Signal()
    udp_start_requested = Signal(str, int)
    udp_stop_requested = Signal()

    def __init__(self, config_loader, data_receiver, parent=None):
        super().__init__(parent)
        self.config_loader = config_loader
        self.data_receiver = data_receiver

        self.setFixedHeight(32)
        self.setStyleSheet(TOP_BAR_STYLE)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(10, 0, 10, 0)
        layout.setSpacing(8)

        ctrl_h = 24

        # ── Serial section ──
        serial_label = QLabel("串口 ")
        serial_label.setStyleSheet(STYLE_LABEL)
        layout.addWidget(serial_label)

        self.serial_combo = _SeamlessComboBox()
        self.serial_combo.setFixedHeight(ctrl_h)
        self.serial_combo.setStyleSheet(STYLE_COMBO)
        self.serial_combo.setItemDelegate(
            _CompactItemDelegate(20, self.serial_combo),
        )

        popup_window = self.serial_combo.view().window()
        popup_window.setWindowFlags(
            Qt.Popup | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint,
        )
        popup_window.setAttribute(Qt.WA_TranslucentBackground)
        self.serial_combo.view().setVerticalScrollMode(
            QAbstractItemView.ScrollMode.ScrollPerPixel,
        )
        self.serial_combo.view().setContentsMargins(0, 0, 0, 0)
        self.serial_combo.view().setSpacing(0)

        self._refresh_serial_ports()
        layout.addWidget(self.serial_combo)

        self.serial_refresh_btn = QPushButton("↻")
        self.serial_refresh_btn.setFixedSize(ctrl_h, ctrl_h)
        self.serial_refresh_btn.setStyleSheet(
            STYLE_BTN_IDLE
            + "QPushButton { font-size: 14px; font-weight: bold; padding: 0px; }",
        )
        self.serial_refresh_btn.setToolTip("刷新串口列表")
        self.serial_refresh_btn.clicked.connect(self._refresh_serial_ports)
        layout.addWidget(self.serial_refresh_btn)

        self.serial_toggle_btn = QPushButton("打开串口")
        self.serial_toggle_btn.setFixedHeight(ctrl_h)
        self.serial_toggle_btn.setStyleSheet(STYLE_BTN_IDLE)
        self.serial_toggle_btn.clicked.connect(self._toggle_serial)
        layout.addWidget(self.serial_toggle_btn)

        layout.addSpacing(16)

        # ── UDP section ──
        udp_port_label = QLabel("UDP端口 ")
        udp_port_label.setStyleSheet(STYLE_LABEL)
        layout.addWidget(udp_port_label)

        udp_config = self.config_loader.get_udp_config()
        self.udp_port_spin = QSpinBox()
        self.udp_port_spin.setRange(1, 65535)
        self.udp_port_spin.setValue(udp_config.get("port", 8888))
        self.udp_port_spin.setFixedHeight(ctrl_h)
        self.udp_port_spin.setFixedWidth(72)
        self.udp_port_spin.setStyleSheet(STYLE_SPINBOX)
        layout.addWidget(self.udp_port_spin)

        self.udp_toggle_btn = QPushButton("接收UDP")
        self.udp_toggle_btn.setFixedHeight(ctrl_h)
        self.udp_toggle_btn.setStyleSheet(STYLE_BTN_IDLE)
        self.udp_toggle_btn.clicked.connect(self._toggle_udp)
        layout.addWidget(self.udp_toggle_btn)

        layout.addStretch()

        # 串口异步断开时更新 UI
        self.data_receiver.serial_stopped.connect(self._on_serial_stopped)

    # ── Serial ───────────────────────────────────────────────────────

    def _refresh_serial_ports(self):
        current = self.serial_combo.currentText()
        self.serial_combo.clear()
        ports = serial.tools.list_ports.comports()
        serial_config = self.config_loader.get_serial_config()
        cfg_port = serial_config.get("port", "")
        select_idx = 0
        for i, info in enumerate(sorted(ports, key=lambda p: p.device)):
            label = (
                f"{info.device}  {info.description}"
                if info.description and info.description != "n/a"
                else info.device
            )
            self.serial_combo.addItem(label, userData=info.device)
            if info.device == current or (
                not current and info.device == cfg_port
            ):
                select_idx = i
        if self.serial_combo.count() > 0:
            self.serial_combo.setCurrentIndex(select_idx)

    def _toggle_serial(self):
        if self.data_receiver.is_serial_running:
            self.data_receiver.stop_serial()
            self._set_serial_ui_idle()
        else:
            if self.serial_combo.count() == 0:
                return
            port = self.serial_combo.currentData()
            serial_cfg = self.config_loader.get_serial_config()
            self.data_receiver.start_serial(
                port=port,
                baudrate=serial_cfg.get("baudrate", 115200),
                protocol=serial_cfg.get("protocol", "csv"),
                timeout=serial_cfg.get("timeout", 1),
                acc_fsr=serial_cfg.get("acc_fsr", 4),
                gyro_fsr=serial_cfg.get("gyro_fsr", 2000),
            )
            self.serial_toggle_btn.setText("关闭串口")
            self.serial_toggle_btn.setStyleSheet(STYLE_BTN_ACTIVE)
            self.serial_combo.setEnabled(False)
            self.serial_refresh_btn.setEnabled(False)

    def _set_serial_ui_idle(self):
        self.serial_toggle_btn.setText("打开串口")
        self.serial_toggle_btn.setStyleSheet(STYLE_BTN_IDLE)
        self.serial_combo.setEnabled(True)
        self.serial_refresh_btn.setEnabled(True)
        self.serial_stop_requested.emit()

    def _on_serial_stopped(self):
        """Handle asynchronous serial disconnect."""
        self._set_serial_ui_idle()

    # ── UDP ──────────────────────────────────────────────────────────

    def _toggle_udp(self):
        if self.data_receiver.is_udp_running:
            self.data_receiver.stop_udp()
            self.udp_toggle_btn.setText("接收UDP")
            self.udp_toggle_btn.setStyleSheet(STYLE_BTN_IDLE)
            self.udp_port_spin.setEnabled(True)
        else:
            udp_cfg = self.config_loader.get_udp_config()
            ip = udp_cfg.get("ip", "127.0.0.1")
            port = self.udp_port_spin.value()
            self.data_receiver.start_udp(ip, port)
            self.udp_toggle_btn.setText("停止接收")
            self.udp_toggle_btn.setStyleSheet(STYLE_BTN_ACTIVE)
            self.udp_port_spin.setEnabled(False)
