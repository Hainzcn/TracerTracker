import math
from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel
from PySide6.QtCore import Qt


class SensorInfoOverlay(QWidget):
    """
    Semi-transparent overlay at the bottom-left of the 3D view showing
    acceleration, velocity, and altitude-change information.
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

    def update_acceleration(self, ax, ay, az):
        """Update displayed acceleration (m/s^2)."""
        if not self._has_data:
            self._has_data = True
            self.setVisible(True)
        self.acc_label.setText(f"ACC: {ax:+8.2f} {ay:+8.2f} {az:+8.2f} m/s\u00b2")

    def update_velocity(self, vx, vy, vz):
        """Update displayed velocity (m/s)."""
        self.vel_label.setText(f"VEL: {vx:+8.3f} {vy:+8.3f} {vz:+8.3f} m/s")

    def update_altitude(self, pressure=None, altitude=None):
        """
        Update altitude change display.
        Uses direct altitude value if provided.  Falls back to barometric
        formula: h = 44330 * (1 - (P/P0)^(1/5.255)) with the first
        received pressure as P0.
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

    # Let mouse events fall through
    def mousePressEvent(self, ev):
        ev.ignore()

    def mouseReleaseEvent(self, ev):
        ev.ignore()

    def mouseMoveEvent(self, ev):
        ev.ignore()

    def wheelEvent(self, ev):
        ev.ignore()
