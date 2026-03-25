import math
import numpy as np
from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
import pyqtgraph.opengl as gl


class AttitudeWidget(QWidget):
    """
    显示 3D 姿态立方体和方向文本的叠加部件。
    旨在放置在主 3D 视图的右上角。
    默认隐藏；一旦接收到姿态数据即变为可见。
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(180, 230)
        self.setStyleSheet(
            "AttitudeWidget { background: rgba(18, 18, 18, 210); border-radius: 6px; }"
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        layout.setSpacing(4)

        self.cube_view = _CubeGLWidget()
        self.cube_view.setFixedSize(170, 170)
        layout.addWidget(self.cube_view, alignment=Qt.AlignCenter)

        self.label = QLabel("--")
        self.label.setAlignment(Qt.AlignCenter)
        self.label.setWordWrap(True)
        self.label.setStyleSheet(
            "QLabel {"
            "  color: #e0e0e0;"
            "  background-color: rgba(30, 30, 30, 220);"
            "  border-radius: 4px;"
            "  padding: 4px 8px;"
            "  font-family: Consolas, monospace;"
            "  font-size: 11px;"
            "}"
        )
        layout.addWidget(self.label)

        self._has_data = False
        self.setVisible(False)

    def update_quaternion(self, q0, q1, q2, q3):
        """根据四元数旋转立方体并显示四元数值。"""
        if not self._has_data:
            self._has_data = True
            self.setVisible(True)
        self.cube_view.set_rotation_quaternion(q0, q1, q2, q3)
        self.label.setText(
            f"Q: {q0:.3f}, {q1:.3f}\n   {q2:.3f}, {q3:.3f}"
        )

    def update_euler(self, roll, pitch, yaw):
        """根据欧拉角（度）旋转立方体并显示数值。"""
        if not self._has_data:
            self._has_data = True
            self.setVisible(True)
        self.cube_view.set_rotation_euler(roll, pitch, yaw)
        self.label.setText(f"R:{roll:+7.1f}\u00b0  P:{pitch:+7.1f}\u00b0\nY:{yaw:+7.1f}\u00b0")

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
