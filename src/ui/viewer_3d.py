"""
基于 pyqtgraph.opengl.GLViewWidget 的自定义 3D 查看器核心组件。

负责相机管理、鼠标/键盘交互、缩放动画和视图矩阵重写。
网格/坐标轴由 GridRenderer 管理，点/轨迹由 TrackRenderer 管理。
"""

import logging
import os
from math import tan, radians

from PySide6.QtCore import Qt, QPoint, QTimer, QTime, Signal
from PySide6.QtGui import QVector3D, QMatrix4x4
import pyqtgraph.opengl as gl

from src.ui.grid_renderer import GridRenderer
from src.ui.track_renderer import TrackRenderer

logger = logging.getLogger(__name__)


class Viewer3D(gl.GLViewWidget):
    """
    自定义 3D 查看器组件。
    鼠标交互方式：
    - 左键拖动：旋转（轨道）
    - 右键拖动：平移
    - 中键点击：复位视角（动画过渡）
    - 滚轮：缩放
    """

    log_message = Signal(str)
    camera_changed = Signal()
    projection_mode_changed = Signal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)

        # 相机初始状态
        self.initial_state = {
            'distance': 80,
            'elevation': 30,
            'azimuth': -135,
            'center': QVector3D(0, 0, 0),
            'pan_x': 0,
            'pan_y': -5,
            'scene_scale': 1.0,
        }

        self.setCameraPosition(
            distance=self.initial_state['distance'],
            elevation=self.initial_state['elevation'],
            azimuth=self.initial_state['azimuth'],
        )
        self.setBackgroundColor('#121212')

        self.scene_scale = self.initial_state['scene_scale']

        # 委托渲染器
        self.grid_renderer = GridRenderer(self)
        self.track_renderer = TrackRenderer(self)

        self.update_coordinate_system()

        self.mousePos = QPoint()

        self.pan_offset = QVector3D(
            self.initial_state['pan_x'],
            self.initial_state['pan_y'],
            0,
        )

        self.setFocusPolicy(Qt.StrongFocus)

        # 中键长按状态
        self.long_press_timer = QTimer(self)
        self.long_press_timer.setSingleShot(True)
        self.long_press_timer.timeout.connect(self.on_long_press_timeout)
        self.is_long_press = False

        self.animation_timer = QTimer(self)
        self.animation_timer.timeout.connect(self.update_animation)
        self.animation_start_time = None
        self.animation_duration = 800
        self.start_state = {}
        self.target_state = {}

        # 平滑缩放动画
        self._target_scene_scale = self.scene_scale
        self._zoom_anim_timer = QTimer(self)
        self._zoom_anim_timer.setInterval(16)
        self._zoom_anim_timer.timeout.connect(self._update_zoom_animation)

        # 投影模式（正交 / 透视）
        self._use_ortho = False
        self._ortho_blend = 0.0
        self._ortho_anim_timer = QTimer(self)
        self._ortho_anim_timer.setInterval(16)
        self._ortho_anim_timer.timeout.connect(self._update_ortho_animation)

        self.render_debug_enabled = (
            os.getenv("TRACER_RENDER_DEBUG", "0") == "1"
        )
        self.render_debug_verbose_point_updates = (
            os.getenv("TRACER_RENDER_DEBUG_VERBOSE", "0") == "1"
        )

    # ── Render debug ─────────────────────────────────────────────────

    def set_render_debug_options(self, enabled=None,
                                verbose_point_updates=None):
        if enabled is not None:
            self.render_debug_enabled = bool(enabled)
        if verbose_point_updates is not None:
            self.render_debug_verbose_point_updates = bool(
                verbose_point_updates,
            )

    # ── Delegation to GridRenderer ───────────────────────────────────

    def update_coordinate_system(self):
        dist = self.cameraParams()['distance']
        self.grid_renderer.update(dist, self.scene_scale)

    def _update_arrow_billboard(self):
        self.grid_renderer.update_arrow_billboard(
            scene_scale=self.scene_scale,
        )

    # ── Delegation to TrackRenderer ──────────────────────────────────

    def update_point(self, name, x, y, z, color=(1, 0, 0, 1), size=10):
        self.track_renderer.update_point(name, x, y, z, color, size)

    def clear_all(self):
        self.track_renderer.clear_all()

    def set_full_path_mode(self, enabled):
        self.track_renderer.set_full_path_mode(enabled)

    def set_trail_mode(self, enabled):
        self.track_renderer.set_trail_mode(enabled)

    def set_trail_length(self, length):
        self.track_renderer.set_trail_length(length)

    # ── Mouse events ─────────────────────────────────────────────────

    def mousePressEvent(self, ev):
        self.setFocus()
        self.mousePos = ev.pos()

        if ev.button() == Qt.MouseButton.MiddleButton:
            self.is_long_press = False
            self.long_press_timer.start(1000)
            ev.accept()
            return

        if (ev.button() == Qt.MouseButton.LeftButton
                or ev.button() == Qt.MouseButton.RightButton):
            if self.animation_timer.isActive():
                self.animation_timer.stop()
            ev.accept()
        else:
            super().mousePressEvent(ev)

    def mouseReleaseEvent(self, ev):
        if ev.button() == Qt.MouseButton.MiddleButton:
            if self.long_press_timer.isActive():
                self.long_press_timer.stop()
                if not self.is_long_press:
                    self.start_reset_animation(full_reset=False)
            self.is_long_press = False
            ev.accept()
        else:
            super().mouseReleaseEvent(ev)

    def on_long_press_timeout(self):
        self.is_long_press = True
        self.start_reset_animation(full_reset=True)

    def keyPressEvent(self, ev):
        if ev.key() == Qt.Key_R:
            self.auto_fit_view()
            ev.accept()
        else:
            super().keyPressEvent(ev)

    def mouseMoveEvent(self, ev):
        diff = ev.pos() - self.mousePos
        self.mousePos = ev.pos()

        if ev.buttons() == Qt.MouseButton.LeftButton:
            self.orbit(diff.x(), diff.y())
            self.update_coordinate_system()
            self.camera_changed.emit()

        elif ev.buttons() == Qt.MouseButton.RightButton:
            dist = self.cameraParams()['distance']
            scale = dist * 0.001
            self.pan_offset.setX(self.pan_offset.x() + diff.x() * scale)
            self.pan_offset.setY(self.pan_offset.y() - diff.y() * scale)
            self.update()

        else:
            super().mouseMoveEvent(ev)

    def wheelEvent(self, ev):
        if self.animation_timer.isActive():
            self.animation_timer.stop()

        delta = ev.angleDelta().y()
        factor = 1.1 if delta > 0 else (1.0 / 1.1)

        self._target_scene_scale *= factor
        if not self._zoom_anim_timer.isActive():
            self._zoom_anim_timer.start()
        ev.accept()

    # ── View matrix ──────────────────────────────────────────────────

    def viewMatrix(self):
        m = QMatrix4x4()
        m.translate(self.pan_offset.x(), self.pan_offset.y(), 0)
        m.translate(0, 0, -self.opts['distance'])
        m.rotate(self.opts['elevation'] - 90, 1, 0, 0)
        m.rotate(self.opts['azimuth'], 0, 0, 1)

        s = self.scene_scale
        m.scale(s, s, s)

        center = self.opts['center']
        m.translate(-center.x(), -center.y(), -center.z())

        return m

    # ── Projection matrix ─────────────────────────────────────────────

    def projectionMatrix(self, region, viewport):
        x0, y0, w, h = viewport
        dist = self.opts['distance']
        fov = self.opts['fov']
        nearClip = dist * 0.001
        farClip = dist * 1000.0

        r = nearClip * tan(0.5 * radians(fov))
        t = r * h / w

        left = r * ((region[0] - x0) * (2.0 / w) - 1)
        right = r * ((region[0] + region[2] - x0) * (2.0 / w) - 1)
        bottom = t * ((region[1] - y0) * (2.0 / h) - 1)
        top = t * ((region[1] + region[3] - y0) * (2.0 / h) - 1)

        if self._ortho_blend <= 0.0:
            tr = QMatrix4x4()
            tr.frustum(left, right, bottom, top, nearClip, farClip)
            return tr

        scale = dist / nearClip
        ol = left * scale
        or_ = right * scale
        ob = bottom * scale
        ot = top * scale

        if self._ortho_blend >= 1.0:
            tr = QMatrix4x4()
            tr.ortho(ol, or_, ob, ot, nearClip, farClip)
            return tr

        persp = QMatrix4x4()
        persp.frustum(left, right, bottom, top, nearClip, farClip)
        ortho = QMatrix4x4()
        ortho.ortho(ol, or_, ob, ot, nearClip, farClip)

        b = self._ortho_blend
        p = self._matrix4x4_to_list(persp)

        # 正交矩阵的 w 分量为常量 1，而透视矩阵的 w 分量与 z 相关。
        # 直接逐元素混合会在回切透视时生成数值尺度不一致的中间矩阵，
        # 导致动画帧出现裁剪/渲染异常。先做齐次等价缩放再混合可保持稳定。
        o = self._matrix4x4_to_list(ortho, scale=max(dist, 1e-6))

        blended = [p[i] + (o[i] - p[i]) * b for i in range(16)]
        return QMatrix4x4(*blended)

    @staticmethod
    def _matrix4x4_to_list(matrix, scale=1.0):
        values = []
        for row_idx in range(4):
            row = matrix.row(row_idx)
            values.extend([
                row.x() * scale,
                row.y() * scale,
                row.z() * scale,
                row.w() * scale,
            ])
        return values

    def toggle_projection(self):
        self._use_ortho = not self._use_ortho
        if not self._ortho_anim_timer.isActive():
            self._ortho_anim_timer.start()
        self.projection_mode_changed.emit(self._use_ortho)

    def _update_ortho_animation(self):
        target = 1.0 if self._use_ortho else 0.0
        lerp_speed = 0.18
        diff = target - self._ortho_blend
        if abs(diff) < 1e-4:
            self._ortho_blend = target
            self._ortho_anim_timer.stop()
        else:
            self._ortho_blend += diff * lerp_speed
        self.update_coordinate_system()
        self.camera_changed.emit()
        self.update()

    # ── Animation ────────────────────────────────────────────────────

    def _update_zoom_animation(self):
        lerp_speed = 0.28
        diff = self._target_scene_scale - self.scene_scale
        if abs(diff) / max(self.scene_scale, 1e-9) < 1e-4:
            self.scene_scale = self._target_scene_scale
            self._zoom_anim_timer.stop()
        else:
            self.scene_scale += diff * lerp_speed
        self.update_coordinate_system()
        self.update()

    def auto_fit_view(self):
        points = self.track_renderer.points
        if not points:
            return

        max_dist = 0
        for item in points.values():
            pos_data = item.pos
            if pos_data is None or len(pos_data) == 0:
                continue
            import numpy as np
            dists = np.linalg.norm(pos_data, axis=1)
            if len(dists) > 0:
                current_max = float(np.max(dists))
                if current_max > max_dist:
                    max_dist = current_max

        if max_dist < 1.0:
            target_dist = self.initial_state['distance']
        else:
            target_dist = max_dist * 3.5

        current_cam = self.cameraParams()
        self.start_state = {
            'distance': current_cam['distance'],
            'elevation': current_cam['elevation'],
            'azimuth': current_cam['azimuth'],
            'pan_x': self.pan_offset.x(),
            'pan_y': self.pan_offset.y(),
            'scene_scale': self.scene_scale,
        }
        self.target_state = {
            'distance': target_dist,
            'elevation': current_cam['elevation'],
            'azimuth': current_cam['azimuth'],
            'pan_x': self.initial_state['pan_x'],
            'pan_y': self.initial_state['pan_y'],
            'scene_scale': self.initial_state['scene_scale'],
        }

        self.animation_start_time = QTime.currentTime()
        self.animation_timer.start(16)

    def start_reset_animation(self, full_reset=True):
        if self._zoom_anim_timer.isActive():
            self._zoom_anim_timer.stop()

        current_params = self.cameraParams()

        if full_reset:
            target_distance = self.initial_state['distance']
            target_scene_scale = self.initial_state['scene_scale']
        else:
            target_distance = current_params['distance']
            target_scene_scale = self.scene_scale

        self._target_scene_scale = target_scene_scale

        current_azim = current_params['azimuth']
        target_azim = self.initial_state['azimuth']
        diff = target_azim - current_azim
        diff = (diff + 180) % 360 - 180
        effective_target_azim = current_azim + diff

        self.start_state = {
            'distance': current_params['distance'],
            'elevation': current_params['elevation'],
            'azimuth': current_params['azimuth'],
            'pan_x': self.pan_offset.x(),
            'pan_y': self.pan_offset.y(),
            'scene_scale': self.scene_scale,
        }
        self.target_state = {
            'distance': target_distance,
            'elevation': self.initial_state['elevation'],
            'azimuth': effective_target_azim,
            'pan_x': self.initial_state['pan_x'],
            'pan_y': self.initial_state['pan_y'],
            'scene_scale': target_scene_scale,
        }

        self.animation_start_time = QTime.currentTime()
        self.animation_timer.start(16)

    def animate_to_view(self, elevation, azimuth):
        """Smoothly animate the camera to the given orientation."""
        if self._zoom_anim_timer.isActive():
            self._zoom_anim_timer.stop()

        current_cam = self.cameraParams()
        current_azim = current_cam['azimuth']
        diff = azimuth - current_azim
        diff = (diff + 180) % 360 - 180
        effective_target_azim = current_azim + diff

        self.start_state = {
            'distance': current_cam['distance'],
            'elevation': current_cam['elevation'],
            'azimuth': current_cam['azimuth'],
            'pan_x': self.pan_offset.x(),
            'pan_y': self.pan_offset.y(),
            'scene_scale': self.scene_scale,
        }
        self.target_state = {
            'distance': current_cam['distance'],
            'elevation': elevation,
            'azimuth': effective_target_azim,
            'pan_x': self.pan_offset.x(),
            'pan_y': self.pan_offset.y(),
            'scene_scale': self.scene_scale,
        }

        self._target_scene_scale = self.scene_scale
        self.animation_start_time = QTime.currentTime()
        self.animation_timer.start(16)

    def update_animation(self):
        if not self.animation_start_time:
            return

        elapsed = self.animation_start_time.msecsTo(QTime.currentTime())
        progress = elapsed / self.animation_duration

        if progress >= 1.0:
            progress = 1.0
            self.animation_timer.stop()

        ease = 1 - (1 - progress) * (1 - progress)

        new_dist = (
            self.start_state['distance']
            + (self.target_state['distance'] - self.start_state['distance'])
            * ease
        )
        new_elev = (
            self.start_state['elevation']
            + (self.target_state['elevation'] - self.start_state['elevation'])
            * ease
        )
        new_azim = (
            self.start_state['azimuth']
            + (self.target_state['azimuth'] - self.start_state['azimuth'])
            * ease
        )

        new_pan_x = (
            self.start_state['pan_x']
            + (self.target_state['pan_x'] - self.start_state['pan_x']) * ease
        )
        new_pan_y = (
            self.start_state['pan_y']
            + (self.target_state['pan_y'] - self.start_state['pan_y']) * ease
        )
        self.pan_offset.setX(new_pan_x)
        self.pan_offset.setY(new_pan_y)

        start_scale = self.start_state.get('scene_scale', 1.0)
        target_scale = self.target_state.get('scene_scale', 1.0)
        self.scene_scale = start_scale + (target_scale - start_scale) * ease
        self._target_scene_scale = self.scene_scale

        self.setCameraPosition(
            distance=new_dist,
            elevation=new_elev,
            azimuth=new_azim,
        )
        self.update_coordinate_system()
        self.camera_changed.emit()
        self.update()
