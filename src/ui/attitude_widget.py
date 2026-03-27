import math
import numpy as np
from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
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
    """

    _CUBE_SIZE = 120
    _WIDGET_WIDTH = 150

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TranslucentBackground)

        self._title_style = (
            "QLabel {"
            "  color: #999999;"
            "  background: transparent;"
            "  font-family: Consolas, monospace;"
            "  font-size: 9px;"
            "  padding: 0px;"
            "  margin: 0px;"
            "}"
        )
        self._angle_style = (
            "QLabel {"
            "  color: #cccccc;"
            "  background: transparent;"
            "  font-family: Consolas, monospace;"
            "  font-size: 9px;"
            "  padding: 0px;"
            "  margin: 0px;"
            "}"
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 2, 4, 2)
        layout.setSpacing(0)

        self.cube_raw, self.label_raw = self._add_cube_group(layout, "Raw")
        self.cube_madgwick, self.label_madgwick = self._add_cube_group(layout, "Madgwick")
        self.cube_mahony, self.label_mahony = self._add_cube_group(layout, "Mahony")

        group_h = 12 + self._CUBE_SIZE + 14
        total_h = 2 + 3 * group_h + 2
        self.setFixedSize(self._WIDGET_WIDTH, total_h)
        self.setStyleSheet(
            "AttitudeWidget { background: rgba(18, 18, 18, 180); border-radius: 6px; }"
        )

        self._has_data = False
        self.setVisible(False)

    def _add_cube_group(self, parent_layout, title_text):
        title = QLabel(title_text)
        title.setAlignment(Qt.AlignCenter)
        title.setFixedHeight(12)
        title.setStyleSheet(self._title_style)
        parent_layout.addWidget(title)

        cube = _CubeGLWidget()
        cube.setFixedSize(self._CUBE_SIZE, self._CUBE_SIZE)
        parent_layout.addWidget(cube, alignment=Qt.AlignCenter)

        label = QLabel("R:  --    P:  --    Y:  --")
        label.setAlignment(Qt.AlignCenter)
        label.setFixedHeight(14)
        label.setStyleSheet(self._angle_style)
        parent_layout.addWidget(label)

        return cube, label

    @staticmethod
    def _euler_text(roll, pitch, yaw):
        return f"R:{roll:+6.1f}\u00b0 P:{pitch:+6.1f}\u00b0 Y:{yaw:+6.1f}\u00b0"

    def reset(self):
        """Clear all attitude data and hide the widget."""
        self._has_data = False
        self.setVisible(False)
        placeholder = "R:  --    P:  --    Y:  --"
        self.label_raw.setText(placeholder)
        self.label_madgwick.setText(placeholder)
        self.label_mahony.setText(placeholder)

    def _ensure_visible(self):
        if not self._has_data:
            self._has_data = True
            self.setVisible(True)

    def update_quaternion(self, q0, q1, q2, q3):
        """原生四元数 -- 旋转第一个立方体，显示姿态角。"""
        self._ensure_visible()
        self.cube_raw.set_rotation_quaternion(q0, q1, q2, q3)
        r, p, y = _quat_to_euler(q0, q1, q2, q3)
        self.label_raw.setText(self._euler_text(r, p, y))

    def update_euler(self, roll, pitch, yaw):
        """原生欧拉角 -- 旋转第一个立方体，显示姿态角。"""
        self._ensure_visible()
        self.cube_raw.set_rotation_euler(roll, pitch, yaw)
        self.label_raw.setText(self._euler_text(roll, pitch, yaw))

    def update_madgwick_quaternion(self, q0, q1, q2, q3):
        """Madgwick 滤波器四元数 -- 旋转第二个立方体，显示姿态角。"""
        self._ensure_visible()
        self.cube_madgwick.set_rotation_quaternion(q0, q1, q2, q3)
        r, p, y = _quat_to_euler(q0, q1, q2, q3)
        self.label_madgwick.setText(self._euler_text(r, p, y))

    def update_mahony_quaternion(self, q0, q1, q2, q3):
        """Mahony 滤波器四元数 -- 旋转第三个立方体，显示姿态角。"""
        self._ensure_visible()
        self.cube_mahony.set_rotation_quaternion(q0, q1, q2, q3)
        r, p, y = _quat_to_euler(q0, q1, q2, q3)
        self.label_mahony.setText(self._euler_text(r, p, y))

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

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setBackgroundColor("#1a1a1a")
        self.setCameraPosition(distance=4.5, elevation=25, azimuth=-45)
        self._build_scene()

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

        axis_len = 1.6
        axis_defs = [
            ([[0, 0, 0], [axis_len, 0, 0]], (1, 0.3, 0.3, 1), "X", QColor(255, 80, 80)),
            ([[0, 0, 0], [0, axis_len, 0]], (0.3, 1, 0.3, 1), "Y", QColor(80, 255, 80)),
            ([[0, 0, 0], [0, 0, axis_len]], (0.3, 0.3, 1, 1), "Z", QColor(80, 80, 255)),
        ]
        for pos_data, color, text, lbl_color in axis_defs:
            line = gl.GLLinePlotItem(
                pos=np.array(pos_data, dtype=np.float32),
                color=color,
                width=2,
                antialias=True,
            )
            self.addItem(line)
            self._body_items.append(line)
            offset = np.array(pos_data[1], dtype=float) * (1 + 0.15 / axis_len)
            try:
                lbl = gl.GLTextItem(pos=offset, text=text, color=lbl_color)
                self.addItem(lbl)
                self._body_items.append(lbl)
            except Exception:
                pass

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
