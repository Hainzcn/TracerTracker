import math
from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel
from PySide6.QtCore import Qt


class SensorInfoOverlay(QWidget):
    """
    位于 3D 视图左下角的半透明叠加层，显示加速度、速度和海拔变化信息。
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedWidth(260)
        self.setStyleSheet(
            "SensorInfoOverlay {"
            "  background: rgba(18, 18, 18, 200);"
            "  border-radius: 6px;"
            "}"
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(2)

        label_style = (
            "QLabel {"
            "  color: #d4d4d4;"
            "  font-family: Consolas, monospace;"
            "  font-size: 11px;"
            "  background: transparent;"
            "}"
        )

        self.acc_label = QLabel("ACC: --")
        self.acc_label.setStyleSheet(label_style)
        layout.addWidget(self.acc_label)

        self.vel_label = QLabel("VEL: --")
        self.vel_label.setStyleSheet(label_style)
        layout.addWidget(self.vel_label)

        self.alt_label = QLabel("\u0394Alt: --")
        self.alt_label.setStyleSheet(label_style)
        layout.addWidget(self.alt_label)

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
        self.acc_label.setText("ACC: --")
        self.vel_label.setText("VEL: --")
        self.alt_label.setText("\u0394Alt: --")
        self.setVisible(False)

    def update_acceleration(self, ax, ay, az):
        """更新显示的加速度 (m/s^2)。"""
        if not self._has_data:
            self._has_data = True
            self.setVisible(True)
        self.acc_label.setText(f"ACC: {ax:+8.2f} {ay:+8.2f} {az:+8.2f} m/s\u00b2")

    def update_velocity(self, vx, vy, vz):
        """更新显示的速度 (m/s)。"""
        self.vel_label.setText(f"VEL: {vx:+8.3f} {vy:+8.3f} {vz:+8.3f} m/s")

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
            self.alt_label.setText(f"\u0394Alt: {delta:+8.2f} m  ({altitude:.1f} m)")
            return

        if pressure is not None and pressure > 0:
            if self._ref_pressure is None:
                self._ref_pressure = pressure
            h_now = 44330.0 * (1.0 - math.pow(pressure / self._ref_pressure, 1.0 / 5.255))
            self.alt_label.setText(f"\u0394Alt: {h_now:+8.2f} m  (P={pressure:.0f} Pa)")

    # 让鼠标事件穿透
    def mousePressEvent(self, ev):
        ev.ignore()

    def mouseReleaseEvent(self, ev):
        ev.ignore()

    def mouseMoveEvent(self, ev):
        ev.ignore()

    def wheelEvent(self, ev):
        ev.ignore()
