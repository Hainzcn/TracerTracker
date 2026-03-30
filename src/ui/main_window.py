"""
TracerTracker 主窗口 — 负责布局组装与事件路由。

UI 构建委托给 ToolBar、DebugConsole 等子组件，
样式常量统一来自 styles 模块。
"""

import logging
import time

from PySide6.QtWidgets import (
    QMainWindow, QVBoxLayout, QWidget, QLabel, QHBoxLayout, QSizePolicy,
    QCheckBox, QSpinBox,
)
from PySide6.QtCore import QTimer, Qt, QEvent

from src.ui.styles import (
    STYLE_CHECKBOX, STYLE_SPINBOX, STATUS_LABEL_STYLE,
    STATUS_LABEL_ACTIVE_STYLE, MAIN_WINDOW_STYLE, STATUS_BAR_STYLE,
)
from src.ui.viewer_3d import Viewer3D
from src.ui.toolbar import ToolBar
from src.ui.debug_console import DebugConsole
from src.ui.attitude_widget import AttitudeWidget
from src.ui.sensor_info_overlay import SensorInfoOverlay
from src.ui.view_gizmo import ViewOrientationGizmo
from src.utils.config_loader import ConfigLoader
from src.utils.data_receiver import DataReceiver
from src.ins import PoseProcessor

logger = logging.getLogger(__name__)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("TracerTracker")
        self.resize(1280, 720)

        self.config_loader = ConfigLoader()
        self.data_receiver = DataReceiver(self.config_loader)
        self.data_receiver.data_received.connect(self.on_data_received)

        self.pose_processor = PoseProcessor(self.config_loader)
        self.pose_processor.position_updated.connect(self.on_pose_updated)

        # 中心部件与布局
        self.central_widget = QWidget()
        self.setCentralWidget(self.central_widget)
        self.layout = QVBoxLayout(self.central_widget)
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(0)

        # 顶部工具栏
        self.toolbar = ToolBar(self.config_loader, self.data_receiver)
        self.toolbar.serial_stop_requested.connect(self._clear_scene)
        self.layout.addWidget(self.toolbar)

        # 3D 查看器
        self.viewer = Viewer3D()
        self.viewer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.viewer.log_message.connect(self._on_viewer_log)
        self.layout.addWidget(self.viewer, 1)
        render_debug_cfg = self.config_loader.get_render_debug_config()
        self.viewer.set_render_debug_options(
            enabled=render_debug_cfg.get("enabled", False),
            verbose_point_updates=render_debug_cfg.get(
                "verbose_point_updates", False,
            ),
        )

        # 叠加部件（浮动在 3D 场景之上）
        self.attitude_widget = AttitudeWidget(self.viewer)
        self.sensor_overlay = SensorInfoOverlay(self.viewer)
        self.pose_processor.velocity_updated.connect(
            self.sensor_overlay.update_velocity,
        )
        self.pose_processor.parsed_data_updated.connect(
            self.on_parsed_data_updated,
        )
        self.pose_processor.log_message.connect(self._on_pose_log)
        self.pose_processor.filter_quaternions_updated.connect(
            self.on_filter_quaternions_updated,
        )

        self.view_gizmo = ViewOrientationGizmo(self.viewer, parent=self.viewer)
        self.viewer.camera_changed.connect(self.view_gizmo.update_orientation)
        self.view_gizmo.view_selected.connect(self.viewer.animate_to_view)
        self.view_gizmo.show()

        self.viewer.installEventFilter(self)

        self._has_quaternion_point = any(
            p.get("purpose") == "quaternion"
            for p in self.config_loader.get("points", [])
        )

        # 调试控制台
        self.debug_console = DebugConsole()
        self.data_receiver.raw_data_received.connect(
            self.debug_console.on_raw_data_received,
        )
        self.layout.addWidget(self.debug_console)

        # 状态栏
        self._build_status_bar()
        self.layout.addWidget(self.status_bar_widget)

        self.debug_console.all_collapsed.connect(
            lambda: self.debug_checkbox.setChecked(False)
        )

        # 全局样式
        self.setStyleSheet(MAIN_WINDOW_STYLE)

        # 状态超时定时器
        self.status_timer = QTimer(self)
        self.status_timer.timeout.connect(self.check_status_timeout)
        self.status_timer.start(1000)

        self.last_udp_time = 0
        self.last_serial_time = 0
        self.viewer.set_trail_length(self.trail_length_spinbox.value())
        QTimer.singleShot(0, self._reposition_overlays)

    # ── Status bar ───────────────────────────────────────────────────

    def _build_status_bar(self):
        self.status_bar_widget = QWidget()
        self.status_bar_layout = QHBoxLayout(self.status_bar_widget)
        self.status_bar_layout.setContentsMargins(10, 0, 10, 0)
        self.status_bar_widget.setStyleSheet(STATUS_BAR_STYLE)
        self.status_bar_widget.setFixedHeight(28)

        self.udp_status_label = QLabel("⚪ UDP: 无")
        self.udp_status_label.setStyleSheet(STATUS_LABEL_STYLE)
        self.serial_status_label = QLabel("⚪ 串口: 无")
        self.serial_status_label.setStyleSheet(STATUS_LABEL_STYLE)

        self.status_bar_layout.addWidget(self.udp_status_label)
        self.status_bar_layout.addSpacing(24)
        self.status_bar_layout.addWidget(self.serial_status_label)
        self.status_bar_layout.addStretch()

        self.debug_checkbox = QCheckBox("调试日志")
        self.debug_checkbox.setStyleSheet(STYLE_CHECKBOX)
        self.debug_checkbox.stateChanged.connect(
            self.debug_console.toggle_visibility,
        )
        self.status_bar_layout.addWidget(self.debug_checkbox)

        self.full_path_checkbox = QCheckBox("全路径")
        self.full_path_checkbox.setStyleSheet(STYLE_CHECKBOX)
        self.full_path_checkbox.toggled.connect(self.toggle_full_path_mode)
        self.status_bar_layout.addSpacing(16)
        self.status_bar_layout.addWidget(self.full_path_checkbox)

        self.trail_checkbox = QCheckBox("速度尾迹")
        self.trail_checkbox.setStyleSheet(STYLE_CHECKBOX)
        self.trail_checkbox.toggled.connect(self.toggle_trail_mode)
        self.status_bar_layout.addSpacing(16)
        self.status_bar_layout.addWidget(self.trail_checkbox)

        self.trail_length_label = QLabel("长度")
        self.trail_length_label.setStyleSheet(
            "color: #666666; font-size: 12px;"
            " font-family: 'Microsoft YaHei', sans-serif; border: none;",
        )
        self.status_bar_layout.addSpacing(8)
        self.status_bar_layout.addWidget(self.trail_length_label)

        self.trail_length_spinbox = QSpinBox()
        self.trail_length_spinbox.setRange(10, 5000)
        self.trail_length_spinbox.setValue(120)
        self.trail_length_spinbox.setFixedWidth(72)
        self.trail_length_spinbox.setFixedHeight(22)
        self.trail_length_spinbox.setStyleSheet(STYLE_SPINBOX)
        self.trail_length_spinbox.valueChanged.connect(
            self.on_trail_length_changed,
        )
        self.status_bar_layout.addWidget(self.trail_length_spinbox)
        self.trail_length_spinbox.setEnabled(False)
        self.trail_length_label.setEnabled(False)

    # ── Toggle handlers ──────────────────────────────────────────────

    def toggle_full_path_mode(self, checked):
        self.viewer.set_full_path_mode(checked)

    def toggle_trail_mode(self, checked):
        self.viewer.set_trail_mode(checked)
        self.trail_length_spinbox.setEnabled(checked)
        self.trail_length_label.setEnabled(checked)
        self.trail_length_label.setStyleSheet(
            STATUS_LABEL_STYLE if checked
            else "color: #555; font-size: 12px; border: none;",
        )

    def on_trail_length_changed(self, value):
        self.viewer.set_trail_length(value)

    # ── Data handling ────────────────────────────────────────────────

    def on_pose_updated(self, name, x, y, z):
        self.viewer.update_point(name, x, y, z, color=[0, 255, 255, 255], size=15)

    def on_filter_quaternions_updated(self, madgwick_q, mahony_q):
        self.attitude_widget.update_madgwick_quaternion(*madgwick_q)
        self.attitude_widget.update_mahony_quaternion(*mahony_q)

    def _on_pose_log(self, message):
        self.debug_console.on_pose_log(message)

    def _on_viewer_log(self, message):
        self.debug_console.on_pose_log(message)

    def on_parsed_data_updated(self, source, prefix, linear_acc, gyr, mag):
        if linear_acc is not None:
            self.sensor_overlay.update_acceleration(
                linear_acc[0], linear_acc[1], linear_acc[2],
            )

    def _update_overlays(self, data):
        if len(data) >= 19:
            if self._has_quaternion_point:
                self.attitude_widget.update_quaternion(
                    data[6], data[7], data[8], data[9],
                )
            else:
                self.attitude_widget.update_euler(
                    data[14], data[15], data[16],
                )
            self.sensor_overlay.update_altitude(
                pressure=data[17], altitude=data[18],
            )

    def _clear_scene(self):
        self.viewer.clear_all()
        self.attitude_widget.reset()
        self.sensor_overlay.reset()
        self.pose_processor.reset()

    def on_data_received(self, source, prefix, data):
        current_time = time.time()

        self.pose_processor.process(source, prefix, data)
        self._update_overlays(data)

        status_text = f"接收中 ({len(data)} 个值)"
        if prefix:
            status_text += f" [{prefix}]"

        if source == "udp":
            self.last_udp_time = current_time
            self.udp_status_label.setText(f"🟢 UDP: {status_text}")
            self.udp_status_label.setStyleSheet(STATUS_LABEL_ACTIVE_STYLE)
        elif source == "serial":
            self.last_serial_time = current_time
            self.serial_status_label.setText(f"🟢 Serial: {status_text}")
            self.serial_status_label.setStyleSheet(STATUS_LABEL_ACTIVE_STYLE)

        points_config = self.config_loader.get("points", [])

        for point_cfg in points_config:
            if point_cfg.get("purpose") in [
                "accelerometer", "gyroscope", "magnetic_field",
            ]:
                continue

            cfg_source = point_cfg.get("source", "any")
            if cfg_source != "any" and cfg_source != source:
                continue

            cfg_prefix = point_cfg.get("prefix", None)
            if cfg_prefix == "":
                cfg_prefix = None
            if cfg_prefix != prefix:
                continue

            try:
                x_cfg = point_cfg.get("x", {})
                y_cfg = point_cfg.get("y", {})
                z_cfg = point_cfg.get("z", {})

                x_idx = x_cfg.get("index", 0)
                y_idx = y_cfg.get("index", 1)
                z_idx = z_cfg.get("index", 2)

                x_mult = x_cfg.get("multiplier", 1.0)
                y_mult = y_cfg.get("multiplier", 1.0)
                z_mult = z_cfg.get("multiplier", 1.0)

                required_len = max(x_idx, y_idx, z_idx) + 1
                if len(data) >= required_len:
                    x = float(data[x_idx]) * x_mult
                    y = float(data[y_idx]) * y_mult
                    z = float(data[z_idx]) * z_mult

                    name = point_cfg.get("name", "Unknown")
                    color = point_cfg.get("color", [255, 0, 0, 255])
                    size = point_cfg.get("size", 10)

                    self.viewer.update_point(name, x, y, z, color, size)
            except (IndexError, ValueError, TypeError, KeyError) as e:
                logger.warning(
                    "处理点 %s 时出错: %s", point_cfg.get('name'), e,
                )

    def check_status_timeout(self):
        current_time = time.time()
        timeout = 2.0

        if current_time - self.last_udp_time > timeout:
            self.udp_status_label.setText("⚪ UDP: Idle")
            self.udp_status_label.setStyleSheet(STATUS_LABEL_STYLE)

        if current_time - self.last_serial_time > timeout:
            self.serial_status_label.setText("⚪ Serial: Idle")
            self.serial_status_label.setStyleSheet(STATUS_LABEL_STYLE)

    # ── Layout / events ──────────────────────────────────────────────

    def _reposition_overlays(self):
        vw = self.viewer.width()
        vh = self.viewer.height()
        margin = 10
        aw = self.attitude_widget
        aw.move(margin, margin)
        so = self.sensor_overlay
        so.adjustSize()
        so.move(vw - so.width() - margin, vh - so.height() - margin)
        vg = self.view_gizmo
        vg.move(vw - vg.width() - margin, margin)

    def eventFilter(self, obj, event):
        if obj is self.viewer and event.type() == QEvent.Resize:
            self._reposition_overlays()
        return super().eventFilter(obj, event)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._reposition_overlays()

    def closeEvent(self, event):
        self.data_receiver.stop()
        super().closeEvent(event)
