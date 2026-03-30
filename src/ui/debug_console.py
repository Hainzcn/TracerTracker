"""
调试控制台组件：原始数据日志与调试信息显示。
"""

import time

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QSplitter, QTextEdit, QCheckBox,
)
from PySide6.QtCore import Qt

from src.ui.styles import STYLE_CHECKBOX, CONSOLE_STYLE


class DebugConsole(QSplitter):
    """可切换显示的调试控制台，包含原始数据和调试日志两个面板。"""

    def __init__(self, parent=None):
        super().__init__(Qt.Horizontal, parent)
        self.setFixedHeight(200)
        self.hide()

        self.show_parsed_data = False

        # 左侧：原始数据控制台 + 解析视图切换
        self.left_console_container = QWidget()
        self.left_console_layout = QVBoxLayout(self.left_console_container)
        self.left_console_layout.setContentsMargins(0, 0, 0, 0)
        self.left_console_layout.setSpacing(0)

        self.left_console_top_bar = QWidget()
        self.left_console_top_bar.setStyleSheet("background-color: transparent;")
        self.left_console_top_layout = QHBoxLayout(self.left_console_top_bar)
        self.left_console_top_layout.setContentsMargins(5, 5, 5, 0)

        self.parsed_view_checkbox = QCheckBox("解析视图")
        self.parsed_view_checkbox.setStyleSheet(STYLE_CHECKBOX)
        self.parsed_view_checkbox.stateChanged.connect(self._toggle_parse_view)

        self.left_console_top_layout.addWidget(self.parsed_view_checkbox)
        self.left_console_top_layout.addStretch()

        self.left_console_layout.addWidget(self.left_console_top_bar)

        self.raw_data_console = QTextEdit()
        self.raw_data_console.setReadOnly(True)
        self.raw_data_console.document().setMaximumBlockCount(500)
        self.raw_data_console.setPlaceholderText("原始数据日志...")
        self.raw_data_console.setStyleSheet(CONSOLE_STYLE)
        self.left_console_layout.addWidget(self.raw_data_console)

        # 右侧：调试信息控制台
        self.debug_info_console = QTextEdit()
        self.debug_info_console.setReadOnly(True)
        self.debug_info_console.document().setMaximumBlockCount(500)
        self.debug_info_console.setPlaceholderText("调试信息与姿态处理日志...")
        self.debug_info_console.setStyleSheet(CONSOLE_STYLE)

        self.addWidget(self.left_console_container)
        self.addWidget(self.debug_info_console)
        self.setSizes([640, 640])

    # ── Public API ───────────────────────────────────────────────────

    def toggle_visibility(self, state):
        """切换调试控制台的可见性。"""
        if state:
            self.show()
        else:
            self.hide()
            self.raw_data_console.clear()
            self.debug_info_console.clear()

    def on_raw_data_received(self, source, raw_text):
        """处理原始数据日志。"""
        if not self.isVisible() or self.show_parsed_data:
            return

        timestamp = time.strftime("%H:%M:%S", time.localtime(time.time()))
        source_color = "#4dabf7" if source == "udp" else "#69db7c"

        html_msg = (
            f"<span style='color:#888888'>[{timestamp}]</span> "
            f"<span style='color:{source_color}; font-weight:bold'>"
            f"[{source.upper()}]</span> "
            f"<span style='color:#d4d4d4'>{raw_text}</span>"
        )
        self.raw_data_console.append(html_msg)

    def on_parsed_data_updated(self, source, prefix, linear_acc, gyr, mag):
        """处理解析后的数据更新，用于解析视图日志。"""
        if not self.isVisible() or not self.show_parsed_data:
            return

        timestamp = time.strftime("%H:%M:%S", time.localtime(time.time()))
        parsed_str = self._format_parsed_data(prefix, linear_acc, gyr, mag)
        source_color = "#4dabf7" if source == "udp" else "#69db7c"

        html_msg = (
            f"<span style='color:#888888'>[{timestamp}]</span> "
            f"<span style='color:{source_color}; font-weight:bold'>"
            f"[{source.upper()}]</span> "
            f"<span style='color:#fcc419; font-weight:bold'>[PARSED]</span> "
            f"<span style='color:#e0e0e0'>{parsed_str}</span>"
        )
        self.raw_data_console.append(html_msg)

    def on_pose_log(self, message):
        """处理来自 PoseProcessor 的日志消息。"""
        if not self.isVisible():
            return
        color = "#d4d4d4"
        if "stationary detected" in message.lower():
            color = "#fcc419"
        elif "error" in message.lower() or "failed" in message.lower():
            color = "#ff6b6b"
        html_msg = f"<span style='color:{color}'>{message}</span>"
        self.debug_info_console.append(html_msg)

    # ── Private ──────────────────────────────────────────────────────

    def _toggle_parse_view(self, state):
        self.show_parsed_data = bool(state)
        self.raw_data_console.clear()

    @staticmethod
    def _format_parsed_data(prefix, linear_acc, gyr, mag):
        parts = []
        if linear_acc is not None:
            parts.append(
                f"LinACC(X:{linear_acc[0]:+6.2f},"
                f" Y:{linear_acc[1]:+6.2f},"
                f" Z:{linear_acc[2]:+6.2f})",
            )
        else:
            parts.append(f"{'LinACC: N/A':<38}")

        if gyr is not None:
            parts.append(
                f"GYR(X:{gyr[0]:+7.2f},"
                f" Y:{gyr[1]:+7.2f},"
                f" Z:{gyr[2]:+7.2f})",
            )

        if mag is not None:
            parts.append(
                f"MAG(X:{mag[0]:+7.2f},"
                f" Y:{mag[1]:+7.2f},"
                f" Z:{mag[2]:+7.2f})",
            )

        return " | ".join(parts).replace(" ", "&nbsp;")
