import math
import numpy as np
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QPainterPath, QRegion
import pyqtgraph.opengl as gl


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

    _CUBE_SIZE = 120
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
        row.setSpacing(1)

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

        angle_col.addStretch()
        angle_col.addWidget(roll_lbl)
        angle_col.addWidget(pitch_lbl)
        angle_col.addWidget(yaw_lbl)
        angle_col.addStretch()

        row.addLayout(angle_col)

        cube_col = QVBoxLayout()
        cube_col.setContentsMargins(0, 0, 0, 0)
        cube_col.setSpacing(0)

        title = QLabel(title_text)
        title.setAlignment(Qt.AlignCenter)
        title.setFixedSize(self._CUBE_SIZE, 16)
        title.setStyleSheet(self._title_style)
        cube_col.addWidget(title)

        cube = _CubeGLWidget()
        cube.setFixedSize(self._CUBE_SIZE, self._CUBE_SIZE)
        cube_col.addWidget(cube)

        row.addLayout(cube_col)

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


class _CubeGLWidget(gl.GLViewWidget):
    """渲染彩色姿态立方体的小型非交互式 GL 部件。"""

    _CORNER_RADIUS = 12

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setBackgroundColor(QColor(30, 30, 30, 45))
        self.setCameraPosition(distance=3.6, elevation=25, azimuth=-45)
        self._build_scene()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        path = QPainterPath()
        path.addRoundedRect(
            0, 0, self.width(), self.height(),
            self._CORNER_RADIUS, self._CORNER_RADIUS,
        )
        self.setMask(QRegion(path.toFillPolygon().toPolygon()))

    # ---- 场景构建 ------------------------------------------------

    def _build_scene(self):
        verts, faces, colors = self._cube_geometry()
        md = gl.MeshData(vertexes=verts, faces=faces)
        md.setFaceColors(colors)
        self.cube_item = gl.GLMeshItem(
            meshdata=md,
            smooth=False,
            drawEdges=True,
            edgeColor=(1, 1, 1, 0.3),
        )
        self.cube_item.setGLOptions("opaque")
        self.addItem(self.cube_item)

        self._body_items = [self.cube_item]

    @staticmethod
    def _cube_geometry():
        """单位立方体的顶点、三角形面和每面 RGBA 颜色。"""
        s = 0.7
        verts = np.array(
            [
                [-s, -s, -s],
                [s, -s, -s],
                [s, s, -s],
                [-s, s, -s],
                [-s, -s, s],
                [s, -s, s],
                [s, s, s],
                [-s, s, s],
            ],
            dtype=np.float32,
        )
        faces = np.array(
            [
                [4, 5, 6], [4, 6, 7],   # Z+  蓝色
                [1, 0, 3], [1, 3, 2],   # Z-  深蓝色
                [1, 2, 6], [1, 6, 5],   # X+  红色
                [0, 4, 7], [0, 7, 3],   # X-  深红色
                [3, 7, 6], [3, 6, 2],   # Y+  绿色
                [0, 1, 5], [0, 5, 4],   # Y-  深绿色
            ],
            dtype=np.uint32,
        )
        colors = np.array(
            [
                [0.25, 0.25, 0.95, 0.92], [0.25, 0.25, 0.95, 0.92],
                [0.15, 0.15, 0.55, 0.92], [0.15, 0.15, 0.55, 0.92],
                [0.95, 0.25, 0.25, 0.92], [0.95, 0.25, 0.25, 0.92],
                [0.55, 0.15, 0.15, 0.92], [0.55, 0.15, 0.15, 0.92],
                [0.25, 0.95, 0.25, 0.92], [0.25, 0.95, 0.25, 0.92],
                [0.15, 0.55, 0.15, 0.92], [0.15, 0.55, 0.15, 0.92],
            ],
            dtype=np.float32,
        )
        return verts, faces, colors

    # ---- 旋转设置 --------------------------------------------------

    def _apply_body_transform(self, tr):
        """将相同的变换应用到立方体和所有机体坐标系项目。"""
        for item in self._body_items:
            item.setTransform(tr)
        self.update()

    def set_rotation_quaternion(self, q0, q1, q2, q3):
        """应用来自四元数 (w=q0, x=q1, y=q2, z=q3) 的旋转。"""
        w, x, y, z = q0, q1, q2, q3
        n2 = w * w + x * x + y * y + z * z
        if n2 < 1e-12:
            return
        inv = 1.0 / math.sqrt(n2)
        w *= inv
        x *= inv
        y *= inv
        z *= inv

        tr = np.array(
            [
                [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y), 0],
                [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x), 0],
                [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y), 0],
                [0, 0, 0, 1],
            ],
            dtype=np.float64,
        )
        self._apply_body_transform(tr)

    def set_rotation_euler(self, roll_deg, pitch_deg, yaw_deg):
        """应用来自欧拉角（度）的旋转，顺序 Z-Y-X。"""
        r = math.radians(roll_deg)
        p = math.radians(pitch_deg)
        y = math.radians(yaw_deg)
        cr, sr = math.cos(r), math.sin(r)
        cp, sp = math.cos(p), math.sin(p)
        cy, sy = math.cos(y), math.sin(y)

        tr = np.array(
            [
                [cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr, 0],
                [sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr, 0],
                [-sp, cp * sr, cp * cr, 0],
                [0, 0, 0, 1],
            ],
            dtype=np.float64,
        )
        self._apply_body_transform(tr)

    # ---- 禁用鼠标交互 -----------------------------------------

    def mousePressEvent(self, ev):
        ev.ignore()

    def mouseReleaseEvent(self, ev):
        ev.ignore()

    def mouseMoveEvent(self, ev):
        ev.ignore()

    def wheelEvent(self, ev):
        ev.ignore()
