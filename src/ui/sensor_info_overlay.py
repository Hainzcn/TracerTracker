import math
from PySide6.QtWidgets import QWidget, QGridLayout, QLabel
from PySide6.QtCore import Qt


class SensorInfoOverlay(QWidget):
    """
    位于 3D 视图右下角的半透明叠加层，显示加速度、速度和海拔变化信息。
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedWidth(300)
        self.setStyleSheet(
            "SensorInfoOverlay {"
            "  background-color: rgba(30, 30, 30, 200);"
            "  border: 1px solid rgba(80, 80, 80, 150);"
            "  border-radius: 8px;"
            "}"
        )

        layout = QGridLayout(self)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setHorizontalSpacing(8)
        layout.setVerticalSpacing(4)

        label_title_style = (
            "QLabel {"
            "  color: #888888;"
            "  font-family: 'Microsoft YaHei', sans-serif;"
            "  font-size: 12px;"
            "  font-weight: bold;"
            "  background: transparent;"
            "}"
        )
        
        label_value_style = (
            "QLabel {"
            "  color: #e0e0e0;"
            "  font-family: 'Consolas', 'JetBrains Mono', monospace;"
            "  font-size: 13px;"
            "  background: transparent;"
            "}"
        )

        # Row 0: ACC
        acc_title = QLabel("ACC")
        acc_title.setStyleSheet(label_title_style)
        layout.addWidget(acc_title, 0, 0, alignment=Qt.AlignRight | Qt.AlignVCenter)
        
        self.acc_val_label = QLabel("--")
        self.acc_val_label.setStyleSheet(label_value_style)
        layout.addWidget(self.acc_val_label, 0, 1, alignment=Qt.AlignLeft | Qt.AlignVCenter)

        # Row 1: VEL
        vel_title = QLabel("VEL")
        vel_title.setStyleSheet(label_title_style)
        layout.addWidget(vel_title, 1, 0, alignment=Qt.AlignRight | Qt.AlignVCenter)
        
        self.vel_val_label = QLabel("--")
        self.vel_val_label.setStyleSheet(label_value_style)
        layout.addWidget(self.vel_val_label, 1, 1, alignment=Qt.AlignLeft | Qt.AlignVCenter)

        # Row 2: ΔAlt
        alt_title = QLabel("ΔAlt")
        alt_title.setStyleSheet(label_title_style)
        layout.addWidget(alt_title, 2, 0, alignment=Qt.AlignRight | Qt.AlignVCenter)
        
        self.alt_val_label = QLabel("--")
        self.alt_val_label.setStyleSheet(label_value_style)
        layout.addWidget(self.alt_val_label, 2, 1, alignment=Qt.AlignLeft | Qt.AlignVCenter)

        layout.setColumnStretch(1, 1)
        self.adjustSize()

        self._ref_pressure = None
        self._ref_altitude = None
        self._has_data = False
        self.setVisible(False)

    def reset(self):
        """Clear all sensor data and hide the widget."""
        self._has_data = False
        self._ref_pressure = None
        self._ref_altitude = None
        self.acc_val_label.setText("--")
        self.vel_val_label.setText("--")
        self.alt_val_label.setText("--")
        self.setVisible(False)

    def update_acceleration(self, ax, ay, az):
        """更新显示的加速度 (m/s^2)。"""
        if not self._has_data:
            self._has_data = True
            self.setVisible(True)
        self.acc_val_label.setText(f"<span style='color:#ff6b6b'>{ax:+8.2f}</span> <span style='color:#69db7c'>{ay:+8.2f}</span> <span style='color:#4dabf7'>{az:+8.2f}</span> <span style='color:#888'>m/s²</span>")

    def update_velocity(self, vx, vy, vz):
        """更新显示的速度 (m/s)。"""
        self.vel_val_label.setText(f"<span style='color:#ff6b6b'>{vx:+8.3f}</span> <span style='color:#69db7c'>{vy:+8.3f}</span> <span style='color:#4dabf7'>{vz:+8.3f}</span> <span style='color:#888'>m/s</span>")

    def update_altitude(self, pressure=None, altitude=None):
        """
        更新海拔变化显示。
        如果提供了直接的海拔值，则使用该值。否则回退到气压公式：
        h = 44330 * (1 - (P/P0)^(1/5.255))，其中 P0 为收到的第一个气压值。
        """
        if altitude is not None and altitude != 0.0:
            if self._ref_altitude is None:
                self._ref_altitude = altitude
            delta = altitude - self._ref_altitude
            self.alt_val_label.setText(f"<span style='color:#fcc419'>{delta:+8.2f}</span> <span style='color:#888'>m  ({altitude:.1f} m)</span>")
            return

        if pressure is not None and pressure > 0:
            if self._ref_pressure is None:
                self._ref_pressure = pressure
            h_now = 44330.0 * (1.0 - math.pow(pressure / self._ref_pressure, 1.0 / 5.255))
            self.alt_val_label.setText(f"<span style='color:#fcc419'>{h_now:+8.2f}</span> <span style='color:#888'>m  (P={pressure:.0f} Pa)</span>")

    # 让鼠标事件穿透
    def mousePressEvent(self, ev):
        ev.ignore()

    def mouseReleaseEvent(self, ev):
        ev.ignore()

    def mouseMoveEvent(self, ev):
        ev.ignore()

    def wheelEvent(self, ev):
        ev.ignore()
