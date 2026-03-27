import logging
import time

from PySide6.QtWidgets import (
    QMainWindow, QVBoxLayout, QWidget, QLabel, QHBoxLayout, QSizePolicy,
    QTextEdit, QCheckBox, QSpinBox, QSplitter, QPushButton, QComboBox, QFrame,
)
from PySide6.QtCore import QTimer, Qt, QEvent
from PySide6.QtGui import QTextCursor
import serial.tools.list_ports
from src.ui.viewer_3d import Viewer3D
from src.ui.attitude_widget import AttitudeWidget
from src.ui.sensor_info_overlay import SensorInfoOverlay
from src.utils.config_loader import ConfigLoader
from src.utils.data_receiver import DataReceiver
from src.utils.pose_processor import PoseProcessor

logger = logging.getLogger(__name__)

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        
        self.setWindowTitle("TracerTracker - 3D 路径可视化")
        self.resize(1280, 720)
        
        self.config_loader = ConfigLoader()
        self.data_receiver = DataReceiver(self.config_loader)
        self.data_receiver.data_received.connect(self.on_data_received)
        self.data_receiver.raw_data_received.connect(self.on_raw_data_received)
        self.data_receiver.serial_stopped.connect(self._on_serial_stopped)
        
        self.pose_processor = PoseProcessor(self.config_loader)
        self.pose_processor.position_updated.connect(self.on_pose_updated)
        self.pose_processor.log_message.connect(self.on_pose_log)
        self.pose_processor.parsed_data_updated.connect(self.on_parsed_data_updated)
        self.pose_processor.filter_quaternions_updated.connect(self.on_filter_quaternions_updated)
        
        # 中心部件与布局
        self.central_widget = QWidget()
        self.setCentralWidget(self.central_widget)
        self.layout = QVBoxLayout(self.central_widget)
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(0)
        
        self._build_top_bar()
        self.layout.addWidget(self.top_bar_widget)
        
        self.viewer = Viewer3D()
        self.viewer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.viewer.log_message.connect(self.on_pose_log)
        self.layout.addWidget(self.viewer, 1)
        render_debug_cfg = self.config_loader.get_render_debug_config()
        self.viewer.set_render_debug_options(
            enabled=render_debug_cfg.get("enabled", False),
            verbose_point_updates=render_debug_cfg.get("verbose_point_updates", False)
        )
        
        # 叠加部件（作为 viewer 的子部件，使其浮动在 3D 场景之上）
        self.attitude_widget = AttitudeWidget(self.viewer)
        self.sensor_overlay = SensorInfoOverlay(self.viewer)
        self.pose_processor.velocity_updated.connect(self.sensor_overlay.update_velocity)
        self.viewer.installEventFilter(self)
        
        # 检查配置以确定是否需要四元数数据
        self._has_quaternion_point = any(
            p.get("purpose") == "quaternion"
            for p in self.config_loader.get("points", [])
        )
        
        # 调试控制台区域（默认隐藏）
        self.debug_splitter = QSplitter(Qt.Horizontal)
        self.debug_splitter.setFixedHeight(200)
        self.debug_splitter.hide()
        
        # 控制台通用样式
        console_style = """
            QTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                border: 1px solid #333;
                font-family: Consolas, monospace;
                font-size: 11px;
            }
        """
        
        # 左侧控制台：原始数据
        self.raw_data_console = QTextEdit()
        self.raw_data_console.setReadOnly(True)
        self.raw_data_console.setPlaceholderText("原始数据日志...")
        self.raw_data_console.setStyleSheet(console_style)

        # 左侧控制台 + 叠加按钮的容器
        self.left_console_container = QWidget()
        self.left_console_layout = QVBoxLayout(self.left_console_container)
        self.left_console_layout.setContentsMargins(0, 0, 0, 0)
        self.left_console_layout.setSpacing(0)
        
        # 左侧控制台顶部栏（按钮区域）
        self.left_console_top_bar = QWidget()
        self.left_console_top_bar.setStyleSheet("background-color: transparent;")
        self.left_console_top_layout = QHBoxLayout(self.left_console_top_bar)
        self.left_console_top_layout.setContentsMargins(5, 5, 5, 0)
        
        # 切换解析视图复选框
        self.parsed_view_checkbox = QCheckBox("解析视图")
        self.parsed_view_checkbox.setStyleSheet(self._style_checkbox)
        self.parsed_view_checkbox.stateChanged.connect(self.toggle_parse_view)
        self.show_parsed_data = False
        
        self.left_console_top_layout.addWidget(self.parsed_view_checkbox)
        self.left_console_top_layout.addStretch()
        
        # 将顶部栏和控制台添加到容器
        # 注意：为了使按钮位于左上角，我们将其放在垂直布局的顶部
        self.left_console_layout.addWidget(self.left_console_top_bar)
        self.left_console_layout.addWidget(self.raw_data_console)
        
        # 右侧控制台：调试信息
        self.debug_info_console = QTextEdit()
        self.debug_info_console.setReadOnly(True)
        self.debug_info_console.setPlaceholderText("调试信息与姿态处理日志...")
        self.debug_info_console.setStyleSheet(console_style)
        
        self.debug_splitter.addWidget(self.left_console_container)
        self.debug_splitter.addWidget(self.debug_info_console)
        # 设置初始大小 (50/50)
        self.debug_splitter.setSizes([640, 640])
        
        self.layout.addWidget(self.debug_splitter)
        
        # 状态栏区域
        self.status_bar_widget = QWidget()
        self.status_bar_layout = QHBoxLayout(self.status_bar_widget)
        self.status_bar_layout.setContentsMargins(10, 0, 10, 0)
        self.status_bar_widget.setStyleSheet("background-color: #252526; border-top: 1px solid #333;")
        self.status_bar_widget.setFixedHeight(32)

        self._status_label_style = "color: #999; font-size: 12px; border: none;"

        self.udp_status_label = QLabel("UDP: Idle")
        self.udp_status_label.setStyleSheet(self._status_label_style)
        self.serial_status_label = QLabel("Serial: Idle")
        self.serial_status_label.setStyleSheet(self._status_label_style)
        self._status_label_active_style = "color: #4CAF50; font-size: 12px; border: none;"

        self.status_bar_layout.addWidget(self.udp_status_label)
        self.status_bar_layout.addSpacing(20)
        self.status_bar_layout.addWidget(self.serial_status_label)
        self.status_bar_layout.addStretch()

        self.debug_checkbox = QCheckBox("调试日志")
        self.debug_checkbox.setStyleSheet(self._style_checkbox)
        self.debug_checkbox.stateChanged.connect(self.toggle_debug_console)
        self.status_bar_layout.addWidget(self.debug_checkbox)

        self.full_path_checkbox = QCheckBox("全路径")
        self.full_path_checkbox.setStyleSheet(self._style_checkbox)
        self.full_path_checkbox.toggled.connect(self.toggle_full_path_mode)
        self.status_bar_layout.addSpacing(12)
        self.status_bar_layout.addWidget(self.full_path_checkbox)

        self.trail_checkbox = QCheckBox("速度尾迹")
        self.trail_checkbox.setStyleSheet(self._style_checkbox)
        self.trail_checkbox.toggled.connect(self.toggle_trail_mode)
        self.status_bar_layout.addSpacing(8)
        self.status_bar_layout.addWidget(self.trail_checkbox)

        self.trail_length_label = QLabel("长度")
        self.trail_length_label.setStyleSheet("color: #555; font-size: 12px; border: none;")
        self.status_bar_layout.addSpacing(4)
        self.status_bar_layout.addWidget(self.trail_length_label)

        self.trail_length_spinbox = QSpinBox()
        self.trail_length_spinbox.setRange(10, 5000)
        self.trail_length_spinbox.setValue(120)
        self.trail_length_spinbox.setFixedWidth(72)
        self.trail_length_spinbox.setFixedHeight(20)
        self.trail_length_spinbox.setStyleSheet(self._style_spinbox)
        self.trail_length_spinbox.valueChanged.connect(self.on_trail_length_changed)
        self.status_bar_layout.addWidget(self.trail_length_spinbox)
        self.trail_length_spinbox.setEnabled(False)
        self.trail_length_label.setEnabled(False)
        
        self.layout.addWidget(self.status_bar_widget)
        
        # 为窗口设置现代暗色主题
        self.setStyleSheet("""
            QMainWindow {
                background-color: #1e1e1e;
            }
        """)
        
        # 定时器：若未收到数据则重置状态
        self.status_timer = QTimer(self)
        self.status_timer.timeout.connect(self.check_status_timeout)
        self.status_timer.start(1000)
        
        self.last_udp_time = 0
        self.last_serial_time = 0
        self.viewer.set_trail_length(self.trail_length_spinbox.value())
        QTimer.singleShot(0, self._reposition_overlays)
        
    # ── Shared bar styles ────────────────────────────────────────────

    def _init_bar_styles(self):
        """Define unified style constants shared by top bar and bottom bar."""
        self._style_label = "color: #999; font-size: 12px; border: none;"
        self._style_combo = """
            QComboBox {
                color: #ccc; font-size: 12px;
                background: transparent;
                border: 1px solid #444; border-radius: 4px;
                padding: 2px 22px 2px 8px; min-width: 120px;
            }
            QComboBox:disabled { color: #555; border-color: #333; }
            QComboBox::drop-down {
                subcontrol-origin: padding;
                subcontrol-position: center right;
                width: 18px; border: none;
            }
            QComboBox::down-arrow {
                image: none;
                border-left: 4px solid transparent;
                border-right: 4px solid transparent;
                border-top: 5px solid #999;
                width: 0; height: 0;
                margin-right: 4px;
            }
            QComboBox QAbstractItemView {
                color: #ccc; background-color: #2d2d2d;
                selection-background-color: #094771;
                font-size: 12px;
                border: none; outline: none;
            }
        """
        self._style_spinbox = """
            QSpinBox {
                color: #ccc; font-size: 12px;
                background: transparent;
                border: 1px solid #444; border-radius: 4px;
                padding: 2px 8px;
            }
            QSpinBox:disabled { color: #555; border-color: #333; }
            QSpinBox::up-button, QSpinBox::down-button {
                width: 0; height: 0; border: none;
            }
        """
        self._style_btn_idle = """
            QPushButton {
                color: #ccc; font-size: 12px;
                background: transparent;
                border: 1px solid #444; border-radius: 4px;
                padding: 2px 12px;
            }
            QPushButton:hover { background-color: #3a3a3a; }
            QPushButton:disabled { color: #555; border-color: #333; }
        """
        self._style_btn_active = """
            QPushButton {
                color: #fff; font-size: 12px;
                background-color: #388E3C;
                border: 1px solid #4CAF50; border-radius: 4px;
                padding: 2px 12px;
            }
            QPushButton:hover { background-color: #43A047; }
        """
        self._style_checkbox = """
            QCheckBox {
                color: #999; font-size: 12px; spacing: 4px;
            }
            QCheckBox:disabled { color: #555; }
            QCheckBox::indicator {
                width: 13px; height: 13px;
                border: 1px solid #444; border-radius: 2px;
                background: transparent;
            }
            QCheckBox::indicator:checked {
                background: #4CAF50; border-color: #4CAF50;
            }
            QCheckBox::indicator:disabled {
                border-color: #333; background: transparent;
            }
            QCheckBox::indicator:checked:disabled {
                background: #555; border-color: #555;
            }
        """

    # ── Top bar construction & handlers ──────────────────────────────

    def _build_top_bar(self):
        self._init_bar_styles()

        self.top_bar_widget = QWidget()
        self.top_bar_widget.setFixedHeight(32)
        self.top_bar_widget.setStyleSheet(
            "background-color: #252526; border-bottom: 1px solid #333;"
        )
        top_layout = QHBoxLayout(self.top_bar_widget)
        top_layout.setContentsMargins(10, 0, 10, 0)
        top_layout.setSpacing(6)

        ctrl_h = 24

        # ── Serial section ──
        serial_label = QLabel("串口:")
        serial_label.setStyleSheet(self._style_label)
        top_layout.addWidget(serial_label)

        self.serial_combo = QComboBox()
        self.serial_combo.setFixedHeight(ctrl_h)
        self.serial_combo.setStyleSheet(self._style_combo)
        self.serial_combo.view().window().setWindowFlags(
            Qt.Popup | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint
        )
        self._refresh_serial_ports()
        top_layout.addWidget(self.serial_combo)

        self.serial_refresh_btn = QPushButton("⟳")
        self.serial_refresh_btn.setFixedSize(ctrl_h, ctrl_h)
        self.serial_refresh_btn.setStyleSheet(self._style_btn_idle)
        self.serial_refresh_btn.setToolTip("刷新串口列表")
        self.serial_refresh_btn.clicked.connect(self._refresh_serial_ports)
        top_layout.addWidget(self.serial_refresh_btn)

        self.serial_toggle_btn = QPushButton("打开串口")
        self.serial_toggle_btn.setFixedHeight(ctrl_h)
        self.serial_toggle_btn.setStyleSheet(self._style_btn_idle)
        self.serial_toggle_btn.clicked.connect(self._toggle_serial)
        top_layout.addWidget(self.serial_toggle_btn)

        # ── Separator ──
        sep = QFrame()
        sep.setFrameShape(QFrame.VLine)
        sep.setStyleSheet("color: #444; border: none;")
        top_layout.addWidget(sep)

        # ── UDP section ──
        udp_label = QLabel("UDP端口:")
        udp_label.setStyleSheet(self._style_label)
        top_layout.addWidget(udp_label)

        udp_config = self.config_loader.get_udp_config()
        self.udp_port_spin = QSpinBox()
        self.udp_port_spin.setRange(1, 65535)
        self.udp_port_spin.setValue(udp_config.get("port", 8888))
        self.udp_port_spin.setFixedHeight(ctrl_h)
        self.udp_port_spin.setFixedWidth(72)
        self.udp_port_spin.setStyleSheet(self._style_spinbox)
        top_layout.addWidget(self.udp_port_spin)

        self.udp_toggle_btn = QPushButton("接收UDP")
        self.udp_toggle_btn.setFixedHeight(ctrl_h)
        self.udp_toggle_btn.setStyleSheet(self._style_btn_idle)
        self.udp_toggle_btn.clicked.connect(self._toggle_udp)
        top_layout.addWidget(self.udp_toggle_btn)

        top_layout.addStretch()

    def _refresh_serial_ports(self):
        current = self.serial_combo.currentText()
        self.serial_combo.clear()
        ports = serial.tools.list_ports.comports()
        serial_config = self.config_loader.get_serial_config()
        cfg_port = serial_config.get("port", "")
        select_idx = 0
        for i, info in enumerate(sorted(ports, key=lambda p: p.device)):
            label = f"{info.device}  {info.description}" if info.description and info.description != "n/a" else info.device
            self.serial_combo.addItem(label, userData=info.device)
            if info.device == current or (not current and info.device == cfg_port):
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
            self.serial_toggle_btn.setStyleSheet(self._style_btn_active)
            self.serial_combo.setEnabled(False)
            self.serial_refresh_btn.setEnabled(False)

    def _toggle_udp(self):
        if self.data_receiver.is_udp_running:
            self.data_receiver.stop_udp()
            self.udp_toggle_btn.setText("接收UDP")
            self.udp_toggle_btn.setStyleSheet(self._style_btn_idle)
            self.udp_port_spin.setEnabled(True)
        else:
            udp_cfg = self.config_loader.get_udp_config()
            ip = udp_cfg.get("ip", "127.0.0.1")
            port = self.udp_port_spin.value()
            self.data_receiver.start_udp(ip, port)
            self.udp_toggle_btn.setText("停止接收")
            self.udp_toggle_btn.setStyleSheet(self._style_btn_active)
            self.udp_port_spin.setEnabled(False)

    def _set_serial_ui_idle(self):
        self.serial_toggle_btn.setText("打开串口")
        self.serial_toggle_btn.setStyleSheet(self._style_btn_idle)
        self.serial_combo.setEnabled(True)
        self.serial_refresh_btn.setEnabled(True)
        self._clear_scene()

    def _on_serial_stopped(self):
        """Handle asynchronous serial disconnect (cable unplug, error, etc.)."""
        self._set_serial_ui_idle()

    def _clear_scene(self):
        """Clear all cached data, 3D points, overlays and reset processors."""
        self.viewer.clear_all()
        self.attitude_widget.reset()
        self.sensor_overlay.reset()
        self.pose_processor.reset()

    def toggle_debug_console(self, state):
        """切换调试控制台的可见性。"""
        if state:
            self.debug_splitter.show()
        else:
            self.debug_splitter.hide()
            self.raw_data_console.clear()
            self.debug_info_console.clear()

    def toggle_full_path_mode(self, checked):
        self.viewer.set_full_path_mode(checked)

    def toggle_trail_mode(self, checked):
        self.viewer.set_trail_mode(checked)
        self.trail_length_spinbox.setEnabled(checked)
        self.trail_length_label.setEnabled(checked)
        self.trail_length_label.setStyleSheet(
            self._status_label_style if checked else "color: #555; font-size: 12px; border: none;"
        )

    def on_trail_length_changed(self, value):
        self.viewer.set_trail_length(value)

    def toggle_parse_view(self, state):
        self.show_parsed_data = bool(state)
        # 切换模式时清除控制台以避免混淆
        self.raw_data_console.clear()

    def format_parsed_data(self, prefix, linear_acc, gyr, mag):
        """
        将解析后的数据格式化为可读字符串，显示：
        - 线性加速度（已去重力）
        - 陀螺仪
        - 磁力计
        """
        parts = []
        if linear_acc is not None:
            parts.append(f"LinACC(X:{linear_acc[0]:.2f}, Y:{linear_acc[1]:.2f}, Z:{linear_acc[2]:.2f})")
        else:
            parts.append("LinACC: N/A")
            
        if gyr is not None:
            parts.append(f"GYR(X:{gyr[0]:.2f}, Y:{gyr[1]:.2f}, Z:{gyr[2]:.2f})")
            
        if mag is not None:
            parts.append(f"MAG(X:{mag[0]:.2f}, Y:{mag[1]:.2f}, Z:{mag[2]:.2f})")
            
        return " | ".join(parts)

    def on_pose_updated(self, name, x, y, z):
        """处理来自 PoseProcessor 的位置更新。"""
        # 计算路径使用青色
        self.viewer.update_point(name, x, y, z, color=[0, 255, 255, 255], size=15)

    def on_filter_quaternions_updated(self, madgwick_q, mahony_q):
        """更新 Madgwick / Mahony 姿态立方体。"""
        self.attitude_widget.update_madgwick_quaternion(*madgwick_q)
        self.attitude_widget.update_mahony_quaternion(*mahony_q)
        
    def on_pose_log(self, message):
        """处理来自 PoseProcessor 的日志消息。"""
        if self.debug_splitter.isVisible():
            self.debug_info_console.moveCursor(QTextCursor.End)
            self.debug_info_console.insertPlainText(message + "\n")
            self.debug_info_console.moveCursor(QTextCursor.End)

    def on_raw_data_received(self, source, raw_text):
        """处理原始数据日志。"""
        if self.debug_splitter.isVisible() and not self.show_parsed_data:
            timestamp = time.strftime("%H:%M:%S", time.localtime(time.time()))
            log_msg = f"[{timestamp}] [{source.upper()}] {raw_text}"
            
            self.raw_data_console.moveCursor(QTextCursor.End)
            self.raw_data_console.insertPlainText(log_msg + "\n")
            self.raw_data_console.moveCursor(QTextCursor.End)

    def on_parsed_data_updated(self, source, prefix, linear_acc, gyr, mag):
        """处理解析后的数据更新，用于叠加层和解析视图日志。"""
        if linear_acc is not None:
            self.sensor_overlay.update_acceleration(
                linear_acc[0], linear_acc[1], linear_acc[2]
            )

        if self.debug_splitter.isVisible() and self.show_parsed_data:
            timestamp = time.strftime("%H:%M:%S", time.localtime(time.time()))
            parsed_str = self.format_parsed_data(prefix, linear_acc, gyr, mag)
            log_msg = f"[{timestamp}] [{source.upper()}] [PARSED] {parsed_str}"
            
            self.raw_data_console.moveCursor(QTextCursor.End)
            self.raw_data_console.insertPlainText(log_msg + "\n")
            self.raw_data_console.moveCursor(QTextCursor.End)

    def _update_overlays(self, data):
        """根据数据快照更新传感器叠加部件。"""
        if len(data) >= 19:
            # ATK-MS901M 快照：已知索引布局
            if self._has_quaternion_point:
                self.attitude_widget.update_quaternion(
                    data[6], data[7], data[8], data[9]
                )
            else:
                self.attitude_widget.update_euler(
                    data[14], data[15], data[16]
                )
            self.sensor_overlay.update_altitude(
                pressure=data[17], altitude=data[18]
            )

    def _reposition_overlays(self):
        """将叠加部件放置在查看器的角落。"""
        vw = self.viewer.width()
        vh = self.viewer.height()
        margin = 10
        aw = self.attitude_widget
        aw.move(vw - aw.width() - margin, margin)
        so = self.sensor_overlay
        so.adjustSize()
        so.move(vw - so.width() - margin, vh - so.height() - margin)

    def on_data_received(self, source, prefix, data):
        """处理从 UDP 或串口接收到的数据。"""
        current_time = time.time()
        
        # 首先处理位姿估计数据
        self.pose_processor.process(source, prefix, data)
        
        # 更新叠加部件
        self._update_overlays(data)
        
        # 更新状态指示器
        status_text = f"接收中 ({len(data)} 个值)"
        if prefix:
            status_text += f" [{prefix}]"
            
        if source == "udp":
            self.last_udp_time = current_time
            self.udp_status_label.setText(f"UDP: {status_text}")
            self.udp_status_label.setStyleSheet(self._status_label_active_style)
        elif source == "serial":
            self.last_serial_time = current_time
            self.serial_status_label.setText(f"Serial: {status_text}")
            self.serial_status_label.setStyleSheet(self._status_label_active_style)
        
        points_config = self.config_loader.get("points", [])
        
        for point_cfg in points_config:
            # 跳过特殊用途的点（由 PoseProcessor 处理）
            if point_cfg.get("purpose") in ["accelerometer", "gyroscope", "magnetic_field"]:
                continue

            # 检查此配置是否适用于当前源
            cfg_source = point_cfg.get("source", "any")
            
            # 允许 "any", "udp", "serial" 或特定列表匹配
            if cfg_source != "any" and cfg_source != source:
                continue
                
            # 检查前缀匹配
            # 如果配置有前缀，必须与接收到的前缀匹配
            # 如果配置无前缀，仅当接收到的前缀为 None 时匹配
            cfg_prefix = point_cfg.get("prefix", None)
            if cfg_prefix == "": cfg_prefix = None
            
            if cfg_prefix != prefix:
                continue
                
            try:
                # 获取索引和乘数
                x_cfg = point_cfg.get("x", {})
                y_cfg = point_cfg.get("y", {})
                z_cfg = point_cfg.get("z", {})
                
                x_idx = x_cfg.get("index", 0)
                y_idx = y_cfg.get("index", 1)
                z_idx = z_cfg.get("index", 2)
                
                x_mult = x_cfg.get("multiplier", 1.0)
                y_mult = y_cfg.get("multiplier", 1.0)
                z_mult = z_cfg.get("multiplier", 1.0)
                
                # 检查数据是否有足够元素
                required_len = max(x_idx, y_idx, z_idx) + 1
                if len(data) >= required_len:
                    x = float(data[x_idx]) * x_mult
                    y = float(data[y_idx]) * y_mult
                    z = float(data[z_idx]) * z_mult
                    
                    name = point_cfg.get("name", "Unknown")
                    # 使用配置中的颜色或默认红色
                    color = point_cfg.get("color", [255, 0, 0, 255])
                    size = point_cfg.get("size", 10)
                    
                    self.viewer.update_point(name, x, y, z, color, size)
            except (IndexError, ValueError, TypeError, KeyError) as e:
                logger.warning("处理点 %s 时出错: %s", point_cfg.get('name'), e)
        
    def check_status_timeout(self):
        """如果一段时间未收到数据，则重置状态标签。"""
        current_time = time.time()
        timeout = 2.0 # 秒
        
        if current_time - self.last_udp_time > timeout:
            self.udp_status_label.setText("UDP: Idle")
            self.udp_status_label.setStyleSheet(self._status_label_style)

        if current_time - self.last_serial_time > timeout:
            self.serial_status_label.setText("Serial: Idle")
            self.serial_status_label.setStyleSheet(self._status_label_style)

    def eventFilter(self, obj, event):
        if obj is self.viewer and event.type() == QEvent.Resize:
            self._reposition_overlays()
        return super().eventFilter(obj, event)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._reposition_overlays()

    def closeEvent(self, event):
        """关闭时清理资源。"""
        self.data_receiver.stop()
        super().closeEvent(event)
