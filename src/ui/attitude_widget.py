import math
import numpy as np
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel
from PySide6.QtCore import Qt, QPointF
from PySide6.QtGui import QColor, QPainter, QPen, QBrush, QPolygonF


def _quat_to_euler(w, x, y, z):
    """四元数 [w,x,y,z] -> (roll, pitch, yaw) 度，ZYX 顺序。"""
    sinr_cosp = 2.0 * (w * x + y * z)
    cosr_cosp = 1.0 - 2.0 * (x * x + y * y)
    roll = math.atan2(sinr_cosp, cosr_cosp)

    sinp = 2.0 * (w * y - z * x)
    sinp = max(-1.0, min(1.0, sinp))
    pitch = math.asin(sinp)

    siny_cosp = 2.0 * (w * z + x * y)
    cosy_cosp = 1.0 - 2.0 * (y * y + z * z)
    yaw = math.atan2(siny_cosp, cosy_cosp)

    return math.degrees(roll), math.degrees(pitch), math.degrees(yaw)

class AttitudeWidget(QWidget):
    """
    显示 3 个 3D 姿态立方体和姿态角文本的叠加部件。
    第一个为原生四元数，第二个为 Madgwick，第三个为 Mahony。
    姿态角分三行显示于立方体左侧，Roll/Pitch/Yaw 分别着色。
    """

    _CUBE_SIZE = 90
    _ANGLE_COL_WIDTH = 80
    _WIDGET_WIDTH = _ANGLE_COL_WIDTH + _CUBE_SIZE + 10

    _ANGLE_FONT = "font-family: Consolas, monospace; font-size: 13px;"
    _ANGLE_COMMON = "background: transparent; padding: 0px; margin: 0px;"
    _ROLL_STYLE  = f"QLabel {{ color: #ff6b6b; {_ANGLE_FONT} {_ANGLE_COMMON} }}"
    _PITCH_STYLE = f"QLabel {{ color: #69db7c; {_ANGLE_FONT} {_ANGLE_COMMON} }}"
    _YAW_STYLE   = f"QLabel {{ color: #4dabf7; {_ANGLE_FONT} {_ANGLE_COMMON} }}"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TranslucentBackground)

        self._title_style = (
            "QLabel {"
            "  color: #999999;"
            "  background: transparent;"
            "  font-family: Consolas, monospace;"
            "  font-size: 12px;"
            "  padding: 0px;"
            "  margin: 0px;"
            "}"
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 2, 4, 2)
        layout.setSpacing(0)

        self.cube_raw, self.labels_raw = self._add_cube_group(layout, "Raw")
        self.cube_madgwick, self.labels_madgwick = self._add_cube_group(layout, "Madgwick")
        self.cube_mahony, self.labels_mahony = self._add_cube_group(layout, "Mahony")

        group_h = 16 + self._CUBE_SIZE
        total_h = 2 + 3 * group_h + 2
        self.setFixedSize(self._WIDGET_WIDTH, total_h)

        self._has_data = False
        self.setVisible(False)

    def _add_cube_group(self, parent_layout, title_text):
        row = QHBoxLayout()
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(8)

        angle_col = QVBoxLayout()
        angle_col.setContentsMargins(0, 0, 0, 0)
        angle_col.setSpacing(6)

        roll_lbl = QLabel("R:   --")
        roll_lbl.setStyleSheet(self._ROLL_STYLE)
        pitch_lbl = QLabel("P:   --")
        pitch_lbl.setStyleSheet(self._PITCH_STYLE)
        yaw_lbl = QLabel("Y:   --")
        yaw_lbl.setStyleSheet(self._YAW_STYLE)
        for lbl in (roll_lbl, pitch_lbl, yaw_lbl):
            lbl.setFixedWidth(self._ANGLE_COL_WIDTH)

        angle_col.addSpacing(16)
        angle_col.addStretch()
        angle_col.addWidget(roll_lbl)
        angle_col.addWidget(pitch_lbl)
        angle_col.addWidget(yaw_lbl)
        angle_col.addStretch()

        cube_col = QVBoxLayout()
        cube_col.setContentsMargins(0, 0, 0, 0)
        cube_col.setSpacing(0)

        title = QLabel(title_text)
        title.setAlignment(Qt.AlignCenter)
        title.setFixedSize(self._CUBE_SIZE, 16)
        title.setStyleSheet(self._title_style)
        cube_col.addWidget(title)

        cube = _CubePaintWidget()
        cube.setFixedSize(self._CUBE_SIZE, self._CUBE_SIZE)
        cube_col.addWidget(cube)

        row.addLayout(cube_col)
        row.addLayout(angle_col)

        parent_layout.addLayout(row)

        return cube, (roll_lbl, pitch_lbl, yaw_lbl)

    @staticmethod
    def _set_euler_labels(labels, roll, pitch, yaw):
        labels[0].setText(f"R:{roll:+6.1f}\u00b0")
        labels[1].setText(f"P:{pitch:+6.1f}\u00b0")
        labels[2].setText(f"Y:{yaw:+6.1f}\u00b0")

    def reset(self):
        """Clear all attitude data and hide the widget."""
        self._has_data = False
        self.setVisible(False)
        for labels in (self.labels_raw, self.labels_madgwick, self.labels_mahony):
            labels[0].setText("R:   --")
            labels[1].setText("P:   --")
            labels[2].setText("Y:   --")

    def _ensure_visible(self):
        if not self._has_data:
            self._has_data = True
            self.setVisible(True)

    def update_quaternion(self, q0, q1, q2, q3):
        """原生四元数 -- 旋转第一个立方体，显示姿态角。"""
        self._ensure_visible()
        self.cube_raw.set_rotation_quaternion(q0, q1, q2, q3)
        r, p, y = _quat_to_euler(q0, q1, q2, q3)
        self._set_euler_labels(self.labels_raw, r, p, y)

    def update_euler(self, roll, pitch, yaw):
        """原生欧拉角 -- 旋转第一个立方体，显示姿态角。"""
        self._ensure_visible()
        self.cube_raw.set_rotation_euler(roll, pitch, yaw)
        self._set_euler_labels(self.labels_raw, roll, pitch, yaw)

    def update_madgwick_quaternion(self, q0, q1, q2, q3):
        """Madgwick 滤波器四元数 -- 旋转第二个立方体，显示姿态角。"""
        self._ensure_visible()
        self.cube_madgwick.set_rotation_quaternion(q0, q1, q2, q3)
        r, p, y = _quat_to_euler(q0, q1, q2, q3)
        self._set_euler_labels(self.labels_madgwick, r, p, y)

    def update_mahony_quaternion(self, q0, q1, q2, q3):
        """Mahony 滤波器四元数 -- 旋转第三个立方体，显示姿态角。"""
        self._ensure_visible()
        self.cube_mahony.set_rotation_quaternion(q0, q1, q2, q3)
        r, p, y = _quat_to_euler(q0, q1, q2, q3)
        self._set_euler_labels(self.labels_mahony, r, p, y)

    # 让鼠标事件穿透到下方的 3D 视图
    def mousePressEvent(self, ev):
        ev.ignore()

    def mouseReleaseEvent(self, ev):
        ev.ignore()

    def mouseMoveEvent(self, ev):
        ev.ignore()

    def wheelEvent(self, ev):
        ev.ignore()

class _CubePaintWidget(QWidget):
    _BG_COLOR = QColor(50, 50, 50, 100)
    _EDGE_COLOR = QColor(255, 255, 255, 76)
    _CAMERA_DISTANCE = 3.6
    _CAMERA_ELEVATION = math.radians(25.0)
    _CAMERA_AZIMUTH = math.radians(-45.0)
    _FACE_COLORS = [
        QColor(64, 64, 242),
        QColor(38, 38, 140),
        QColor(242, 64, 64),
        QColor(140, 38, 38),
        QColor(64, 242, 64),
        QColor(38, 140, 38),
    ]
    _FACES = [
        (4, 5, 6, 7),
        (1, 0, 3, 2),
        (1, 2, 6, 5),
        (0, 4, 7, 3),
        (3, 7, 6, 2),
        (0, 1, 5, 4),
    ]
    _EDGES = [
        (0, 1), (1, 2), (2, 3), (3, 0),
        (4, 5), (5, 6), (6, 7), (7, 4),
        (0, 4), (1, 5), (2, 6), (3, 7),
    ]
    _VERTICES = np.array(
        [
            [-0.7, -0.7, -0.7],
            [0.7, -0.7, -0.7],
            [0.7, 0.7, -0.7],
            [-0.7, 0.7, -0.7],
            [-0.7, -0.7, 0.7],
            [0.7, -0.7, 0.7],
            [0.7, 0.7, 0.7],
            [-0.7, 0.7, 0.7],
        ],
        dtype=np.float64,
    )

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self._transform = np.identity(3, dtype=np.float64)

    def _project(self, point):
        rotated = self._transform @ point
        cos_a = math.cos(self._CAMERA_AZIMUTH)
        sin_a = math.sin(self._CAMERA_AZIMUTH)
        sin_e = math.sin(self._CAMERA_ELEVATION)
        cos_e = math.cos(self._CAMERA_ELEVATION)

        x1 = rotated[0] * cos_a - rotated[1] * sin_a
        y1 = rotated[0] * sin_a + rotated[1] * cos_a
        z1 = rotated[2]

        x2 = x1
        y2 = y1 * sin_e + z1 * cos_e
        z2 = -y1 * cos_e + z1 * sin_e

        scale = (self.width() * 0.25) * (
            self._CAMERA_DISTANCE / max(self._CAMERA_DISTANCE - z2, 0.2)
        )
        cx = self.width() / 2.0
        cy = self.height() / 2.0
        x = cx + x2 * scale
        y = cy - y2 * scale
        return QPointF(float(x), float(y)), float(z2)

    def _draw_background(self, painter):
        painter.setPen(Qt.NoPen)
        painter.setBrush(QBrush(self._BG_COLOR))
        painter.drawRect(0, 0, self.width(), self.height())

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        self._draw_background(painter)

        projected = [self._project(v) for v in self._VERTICES]
        face_order = []
        for idx, face in enumerate(self._FACES):
            avg_depth = sum(projected[i][1] for i in face) / 4.0
            face_order.append((avg_depth, idx, face))
        face_order.sort()

        for _depth, idx, face in face_order:
            poly = QPolygonF([projected[i][0] for i in face])
            painter.setPen(Qt.NoPen)
            painter.setBrush(QBrush(self._FACE_COLORS[idx]))
            painter.drawPolygon(poly)

        painter.setPen(QPen(self._EDGE_COLOR, 1.2))
        painter.setBrush(Qt.NoBrush)
        for a, b in self._EDGES:
            painter.drawLine(projected[a][0], projected[b][0])
        super().paintEvent(event)

    def _apply_rotation(self, rotation):
        self._transform = rotation
        self.update()

    def set_rotation_quaternion(self, q0, q1, q2, q3):
        w, x, y, z = q0, q1, q2, q3
        n2 = w * w + x * x + y * y + z * z
        if n2 < 1e-12:
            return
        inv = 1.0 / math.sqrt(n2)
        w *= inv
        x *= inv
        y *= inv
        z *= inv

        rotation = np.array(
            [
                [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
                [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
                [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)],
            ],
            dtype=np.float64,
        )
        self._apply_rotation(rotation)

    def set_rotation_euler(self, roll_deg, pitch_deg, yaw_deg):
        r = math.radians(roll_deg)
        p = math.radians(pitch_deg)
        y = math.radians(yaw_deg)
        cr, sr = math.cos(r), math.sin(r)
        cp, sp = math.cos(p), math.sin(p)
        cy, sy = math.cos(y), math.sin(y)

        rotation = np.array(
            [
                [cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr],
                [sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr],
                [-sp, cp * sr, cp * cr],
            ],
            dtype=np.float64,
        )
        self._apply_rotation(rotation)
