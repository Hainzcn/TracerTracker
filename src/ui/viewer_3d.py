import logging
import math
import os
import time

from PySide6.QtCore import Qt, QPoint, QTimer, QTime, Signal
from PySide6.QtGui import QVector3D, QColor, QMatrix4x4
import pyqtgraph.opengl as gl
import numpy as np
from OpenGL.GL import GL_DEPTH_TEST, GL_BLEND, GL_ALPHA_TEST, GL_CULL_FACE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA

logger = logging.getLogger(__name__)

class Viewer3D(gl.GLViewWidget):
    """
    基于 pyqtgraph.opengl.GLViewWidget 的自定义 3D 查看器组件。
    鼠标交互方式：
    - 左键拖动：旋转（轨道）
    - 右键拖动：平移
    - 中键点击：复位视角（动画过渡）
    - 滚轮：缩放
    """
    # 调试日志信号
    log_message = Signal(str)
    camera_changed = Signal()

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
        
        # 初始化相机与背景
        self.setCameraPosition(
            distance=self.initial_state['distance'],
            elevation=self.initial_state['elevation'],
            azimuth=self.initial_state['azimuth']
        )
        self.setBackgroundColor('#121212')  # 深色背景

        # 坐标系视觉参数（必须在 add_custom_axes 之前设置）
        self.AXIS_VISUAL_RATIO = 0.28
        self.TICK_LABEL_POOL_SIZE = 60
        self.TICK_LINE_LENGTH_RATIO = 0.02
        self.scene_scale = self.initial_state['scene_scale']
        
        # 双层 XOY 网格：主网格（粗线）和副网格（细线），一级 = 5× 二级
        self.GRID_MAJOR_WIDTH = 1.6
        self.GRID_MINOR_WIDTH_MIN = 0.5
        self.GRID_MAJOR_ALPHA = 40

        _grid_gl_opts = {
            GL_DEPTH_TEST: False,
            GL_BLEND: True,
            GL_ALPHA_TEST: False,
            GL_CULL_FACE: False,
            'glBlendFunc': (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
        }

        self.grid_major_item = gl.GLLinePlotItem(mode='lines', width=self.GRID_MAJOR_WIDTH, antialias=True)
        self.grid_major_item.setGLOptions(_grid_gl_opts)
        self.grid_major_item.setDepthValue(-1000)
        self.addItem(self.grid_major_item)

        self.grid_minor_item = gl.GLLinePlotItem(mode='lines', width=self.GRID_MINOR_WIDTH_MIN, antialias=True)
        self.grid_minor_item.setGLOptions(_grid_gl_opts)
        self.grid_minor_item.setDepthValue(-1000)
        self.addItem(self.grid_minor_item)
        
        # 添加加粗加长的自定义坐标轴
        self.add_custom_axes()
        
        self.points = {}  # {名称: GLScatterPlotItem}
        self.point_histories = {}
        self.point_speeds = {}
        self.point_times = {}
        self.point_colors = {}
        self.path_items = {}
        self.trail_items = {}
        self.full_path_mode = False
        self.trail_mode = False
        self.trail_length = 120
        self.trail_width_min = 2.4
        self.trail_width_max = 6.0
        self.max_history_length = 10000
        
        # 自适应缩放状态
        self.first_point_rendered = False
        
        self.mousePos = QPoint()
        
        # 自定义相机平移偏移（屏幕空间平移）
        self.pan_offset = QVector3D(
            self.initial_state['pan_x'],
            self.initial_state['pan_y'],
            0,
        )
        
        # 启用键盘焦点
        self.setFocusPolicy(Qt.StrongFocus)
        
        # 中键长按状态
        self.long_press_timer = QTimer(self)
        self.long_press_timer.setSingleShot(True)
        self.long_press_timer.timeout.connect(self.on_long_press_timeout)
        self.is_long_press = False
        
        self.animation_timer = QTimer(self)
        self.animation_timer.timeout.connect(self.update_animation)
        self.animation_start_time = None
        self.animation_duration = 800  # 毫秒
        self.start_state = {}
        self.target_state = {}
        # 平滑缩放动画
        self._target_scene_scale = self.scene_scale
        self._zoom_anim_timer = QTimer(self)
        self._zoom_anim_timer.setInterval(16)
        self._zoom_anim_timer.timeout.connect(self._update_zoom_animation)

        self.render_debug_enabled = os.getenv("TRACER_RENDER_DEBUG", "0") == "1"
        self.render_debug_verbose_point_updates = os.getenv("TRACER_RENDER_DEBUG_VERBOSE", "0") == "1"

    def set_render_debug_options(self, enabled=None, verbose_point_updates=None):
        if enabled is not None:
            self.render_debug_enabled = bool(enabled)
        if verbose_point_updates is not None:
            self.render_debug_verbose_point_updates = bool(verbose_point_updates)

    def _render_debug(self, code, detail, name=None, level="INFO", verbose=False):
        if not self.render_debug_enabled:
            return
        if verbose and not self.render_debug_verbose_point_updates:
            return
        point_text = f"[{name}]" if name is not None else "[GLOBAL]"
        msg = f"[Viewer3D][{level}]{point_text}[{code}] {detail}"
        logger.debug(msg)
        self.log_message.emit(msg)

    def _create_axis_label(self, text, pos, color):
        try:
            label = gl.GLTextItem(pos=np.asarray(pos, dtype=float), text=text, color=color)
            label.setData(alignment=Qt.AlignmentFlag.AlignCenter)
            self.addItem(label)
            return label
        except Exception:
            return None

    def _build_line_segments(self, vertices, colors=None):
        pts = np.asarray(vertices, dtype=np.float32)
        if pts.ndim != 2 or pts.shape[1] != 3:
            return None, None, f"顶点维度无效: shape={getattr(pts, 'shape', None)}"
        if colors is None:
            valid_mask = np.isfinite(pts).all(axis=1)
            pts = pts[valid_mask]
            if len(pts) < 2:
                return None, None, f"有效顶点不足: valid_points={len(pts)}"
            return np.repeat(pts, 2, axis=0)[1:-1], None, None
        cols = np.asarray(colors, dtype=np.float32)
        if cols.ndim != 2 or cols.shape[1] != 4 or len(cols) != len(pts):
            return None, None, f"颜色维度无效: pts_shape={pts.shape}, colors_shape={getattr(cols, 'shape', None)}"
        valid_mask = np.isfinite(pts).all(axis=1) & np.isfinite(cols).all(axis=1)
        pts = pts[valid_mask]
        cols = cols[valid_mask]
        if len(pts) < 2:
            return None, None, f"过滤后有效顶点不足: valid_points={len(pts)}"
        segment_pos = np.repeat(pts, 2, axis=0)[1:-1]
        segment_color = np.repeat(cols, 2, axis=0)[1:-1]
        return segment_pos, segment_color, None

    def _normalize_speed_for_trail(self, speeds):
        values = np.asarray(speeds, dtype=np.float32)
        if values.size == 0:
            return values, 0.0, 0.0
        low = float(np.percentile(values, 15))
        high = float(np.percentile(values, 85))
        denom = high - low
        if denom < 1e-6:
            low = float(np.min(values))
            high = float(np.max(values))
            denom = high - low
        if denom < 1e-6:
            return np.zeros_like(values), low, high
        norm = np.clip((values - low) / denom, 0.0, 1.0)
        return norm, low, high

    def _speed_to_comet_rgb(self, speed_norm):
        n = np.clip(speed_norm, 0.0, 1.0)
        r = 1.0 - 0.95 * n
        g = 0.1 + 0.9 * n
        b = 0.06 + 0.14 * (1.0 - np.abs(n - 0.5) * 2.0)
        rgb = np.stack([r, g, b], axis=1).astype(np.float32)
        return np.clip(rgb, 0.0, 1.0)

    def _hide_trail_item(self, item):
        if item is None:
            return
        if isinstance(item, dict):
            for layer_item in item.get('core', []):
                layer_item.setVisible(False)
            for layer_item in item.get('glow', []):
                layer_item.setVisible(False)
            return
        item.setVisible(False)

    _LOG5 = math.log(5.0)

    @staticmethod
    def _compute_grid_spacings(view_range, target_minor_count=15):
        """Compute two-level grid spacings using a pure x5 level sequence.

        The minor spacing follows ..., 0.04, 0.2, 1, 5, 25, 125, ...
        Major spacing is always 5x minor, guaranteeing that at level
        transitions old minor lines become new major lines seamlessly.

        Returns (minor_spacing, major_spacing, phase_t) where phase_t
        in [0, 1] drives the minor-grid fade animation:
          phase_t near 1 -> just entered level (dense), minor faint
          phase_t near 0 -> about to exit level (sparse), minor prominent
        """
        if view_range <= 0:
            return 1.0, 5.0, 0.5
        ideal = view_range / target_minor_count
        log5_val = math.log(max(ideal, 1e-15)) / Viewer3D._LOG5
        level = math.floor(log5_val)
        minor = 5.0 ** level
        major = 5.0 * minor
        phase = log5_val - level
        return minor, major, max(0.0, min(1.0, phase))

    @staticmethod
    def _build_grid_lines(spacing, half_extent, skip_multiple=0,
                          base_rgba=None, fade_radius=None):
        """Generate line-pair vertices with per-line edge fade on XOY (z=0).

        Returns (positions, colors) where positions is (N,3) and colors is
        (N,4).  Lines far from the origin fade to transparent so the grid
        boundary is never a hard edge.

        fade_radius -- absolute distance from origin where fade begins.
                       Lines closer than this are at full alpha; lines
                       beyond fade out quadratically toward half_extent.
                       If None, no fade is applied.
        """
        empty_pos = np.zeros((0, 3), dtype=np.float32)
        empty_col = np.zeros((0, 4), dtype=np.float32)
        if spacing <= 0 or half_extent <= 0 or base_rgba is None:
            return empty_pos, empty_col

        n = int(half_extent / spacing + 0.5)
        verts = []
        colors = []

        do_fade = fade_radius is not None and fade_radius < half_extent
        inv_fade_range = 1.0 / max(half_extent - fade_radius, 1e-9) if do_fade else 1.0

        for i in range(-n, n + 1):
            if skip_multiple > 0 and i % skip_multiple == 0:
                continue
            coord = i * spacing
            d = abs(coord)

            if do_fade and d > fade_radius:
                alpha_mult = max(0.0, 1.0 - ((d - fade_radius) * inv_fade_range) ** 2)
                if alpha_mult < 1e-4:
                    continue
            else:
                alpha_mult = 1.0

            c_max = base_rgba.copy()
            c_max[3] *= alpha_mult

            if do_fade:
                c_zero = c_max.copy()
                c_zero[3] = 0.0
                
                # 平行于 X 轴的线 (y = coord)
                verts.extend([[-half_extent, coord, 0.0], [-fade_radius, coord, 0.0]])
                colors.extend([c_zero, c_max])
                verts.extend([[-fade_radius, coord, 0.0], [fade_radius, coord, 0.0]])
                colors.extend([c_max, c_max])
                verts.extend([[fade_radius, coord, 0.0], [half_extent, coord, 0.0]])
                colors.extend([c_max, c_zero])

                # 平行于 Y 轴的线 (x = coord)
                verts.extend([[coord, -half_extent, 0.0], [coord, -fade_radius, 0.0]])
                colors.extend([c_zero, c_max])
                verts.extend([[coord, -fade_radius, 0.0], [coord, fade_radius, 0.0]])
                colors.extend([c_max, c_max])
                verts.extend([[coord, fade_radius, 0.0], [coord, half_extent, 0.0]])
                colors.extend([c_max, c_zero])
            else:
                verts.extend([[-half_extent, coord, 0.0], [half_extent, coord, 0.0]])
                colors.extend([c_max, c_max])
                verts.extend([[coord, -half_extent, 0.0], [coord, half_extent, 0.0]])
                colors.extend([c_max, c_max])

        if not verts:
            return empty_pos, empty_col
        return np.array(verts, dtype=np.float32), np.array(colors, dtype=np.float32)

    def add_custom_axes(self):
        self.axes_width = 3

        self.x_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)
        self.y_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)
        self.z_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)

        self.addItem(self.x_axis)
        self.addItem(self.y_axis)
        self.addItem(self.z_axis)

        # 坐标轴箭头（填充三角形 + Billboard）
        self.axis_arrows = gl.GLMeshItem(smooth=True, computeNormals=False, glOptions='translucent')
        self.addItem(self.axis_arrows)

        self.x_label = self._create_axis_label('X', [20.0, 0.0, 0.0], QColor(255, 0, 0, 255))
        self.y_label = self._create_axis_label('Y', [0.0, 20.0, 0.0], QColor(0, 255, 0, 255))
        self.z_label = self._create_axis_label('Z', [0.0, 0.0, 20.0], QColor(0, 0, 255, 255))

        # 刻度线：用单个 GLLinePlotItem 容纳所有刻度几何体
        self.tick_line_item = gl.GLLinePlotItem(width=1.5, antialias=True, mode='lines')
        self.tick_line_item.setGLOptions('translucent')
        self.addItem(self.tick_line_item)

        # 刻度标签：预分配的 GLTextItem 对象池
        self.tick_label_pool = []
        tick_label_color = QColor(180, 180, 180, 200)
        for _ in range(self.TICK_LABEL_POOL_SIZE):
            lbl = gl.GLTextItem(pos=np.zeros(3, dtype=float), text='', color=tick_label_color)
            lbl.setVisible(False)
            self.addItem(lbl)
            self.tick_label_pool.append(lbl)

        self.update_coordinate_system()

    def _update_arrow_billboard(self, pos_ext=None, axis_length=None):
        """Recalculate billboard arrow triangles so they always face the camera.

        When called from update_coordinate_system(), pos_ext and axis_length are
        passed directly.  When called standalone (e.g. during orbit), they are
        derived from the current camera distance.
        """
        if pos_ext is None or axis_length is None:
            dist = self.cameraParams()['distance']
            axis_length = dist * self.AXIS_VISUAL_RATIO / self.scene_scale
            pos_ext = axis_length

        arrow_len = axis_length * 0.05
        arrow_width = arrow_len * 0.4

        cam_params = self.cameraParams()
        elev = np.radians(cam_params['elevation'])
        azim = np.radians(cam_params['azimuth'])
        cam_dir = np.array([
            np.cos(elev) * np.sin(azim),
            np.cos(elev) * np.cos(azim),
            -np.sin(elev)
        ], dtype=np.float32)

        cam_len = np.linalg.norm(cam_dir)
        if cam_len > 1e-6:
            cam_dir /= cam_len
        else:
            cam_dir = np.array([0, 1, 0], dtype=np.float32)

        def _width_vec(axis_dir, fallback):
            w = np.cross(axis_dir, cam_dir)
            wl = np.linalg.norm(w)
            return w / wl if wl > 1e-6 else fallback

        dir_x = np.array([1, 0, 0], dtype=np.float32)
        dir_y = np.array([0, 1, 0], dtype=np.float32)
        dir_z = np.array([0, 0, 1], dtype=np.float32)

        w_x = _width_vec(dir_x, dir_y) * arrow_width
        w_y = _width_vec(dir_y, dir_x) * arrow_width
        w_z = _width_vec(dir_z, dir_x) * arrow_width

        tip_x = dir_x * (pos_ext + arrow_len)
        base_x = dir_x * pos_ext
        tip_y = dir_y * (pos_ext + arrow_len)
        base_y = dir_y * pos_ext
        tip_z = dir_z * (pos_ext + arrow_len)
        base_z = dir_z * pos_ext

        verts = np.array([
            tip_x,        base_x - w_x, base_x + w_x,  # X
            tip_y,        base_y - w_y, base_y + w_y,  # Y
            tip_z,        base_z - w_z, base_z + w_z,  # Z
        ], dtype=np.float32)

        faces = np.array([
            [0, 1, 2],
            [3, 4, 5],
            [6, 7, 8],
        ], dtype=np.uint32)

        vertex_colors = np.array([
            [1, 0, 0, 1], [1, 0, 0, 1], [1, 0, 0, 1],
            [0, 1, 0, 1], [0, 1, 0, 1], [0, 1, 0, 1],
            [0, 0, 1, 1], [0, 0, 1, 1], [0, 0, 1, 1],
        ], dtype=np.float32)

        self.axis_arrows.setMeshData(
            vertexes=verts, faces=faces, vertexColors=vertex_colors
        )

    def update_coordinate_system(self):
        dist = self.cameraParams()['distance']
        axis_length = dist * self.AXIS_VISUAL_RATIO / self.scene_scale
        neg_ext = -axis_length * 0.5
        pos_ext = axis_length
        fade_start = neg_ext * 0.5

        # --- 坐标轴线（带负半轴渐隐） ---
        self.x_axis.setData(
            pos=np.array([[neg_ext, 0, 0], [fade_start, 0, 0], [pos_ext, 0, 0]], dtype=np.float32),
            color=np.array([[1, 0, 0, 0], [1, 0, 0, 1], [1, 0, 0, 1]], dtype=np.float32),
            mode='line_strip')
        self.y_axis.setData(
            pos=np.array([[0, neg_ext, 0], [0, fade_start, 0], [0, pos_ext, 0]], dtype=np.float32),
            color=np.array([[0, 1, 0, 0], [0, 1, 0, 1], [0, 1, 0, 1]], dtype=np.float32),
            mode='line_strip')
        self.z_axis.setData(
            pos=np.array([[0, 0, neg_ext], [0, 0, fade_start], [0, 0, pos_ext]], dtype=np.float32),
            color=np.array([[0, 0, 1, 0], [0, 0, 1, 1], [0, 0, 1, 1]], dtype=np.float32),
            mode='line_strip')

        # --- 坐标轴箭头 (Billboard 填充三角形) ---
        self._update_arrow_billboard(pos_ext, axis_length)

        # --- 坐标轴名称标签 ---
        label_offset = pos_ext + axis_length * 0.05 * 1.5
        if self.x_label is not None:
            self.x_label.setData(pos=np.array([label_offset, 0, 0], dtype=np.float32))
        if self.y_label is not None:
            self.y_label.setData(pos=np.array([0, label_offset, 0], dtype=np.float32))
        if self.z_label is not None:
            self.z_label.setData(pos=np.array([0, 0, label_offset], dtype=np.float32))

        # --- 网格与刻度计算（一级 = 5× 二级）---
        total_range = pos_ext - neg_ext
        minor_sp, major_sp, phase_t = self._compute_grid_spacings(total_range)
        tick_half = axis_length * self.TICK_LINE_LENGTH_RATIO

        # phase_t 在级别内随缩放连续变化（参见 _compute_grid_spacings 注释）。
        # fade = 1 → 副网格完全可见（即将升级为主网格）
        # fade = 0 → 副网格不可见（刚刚进入新级别，线太密）
        fade = max(0.0, min(1.0, 1.0 - phase_t))
        minor_alpha = int(self.GRID_MAJOR_ALPHA * fade ** 3.0)
        minor_width = self.GRID_MINOR_WIDTH_MIN + (self.GRID_MAJOR_WIDTH - self.GRID_MINOR_WIDTH_MIN) * fade ** 2.0

        # --- 网格（XOY 平面）---
        half_extent_raw = max(pos_ext * 4, 1e-3)
        half_extent = math.ceil(half_extent_raw / max(major_sp, 1e-15)) * major_sp
        grid_fade_radius = pos_ext * 1.2

        major_rgba = np.array([0.0, 1.0, 1.0, self.GRID_MAJOR_ALPHA / 255.0], dtype=np.float32)
        major_verts, major_colors = self._build_grid_lines(
            major_sp, half_extent, base_rgba=major_rgba, fade_radius=grid_fade_radius)
        if len(major_verts) >= 2:
            self.grid_major_item.setData(pos=major_verts, color=major_colors, mode='lines')
            self.grid_major_item.setVisible(True)
        else:
            self.grid_major_item.setVisible(False)

        if minor_alpha > 0:
            minor_rgba = np.array([0.0, 1.0, 1.0, minor_alpha / 255.0], dtype=np.float32)
            minor_verts, minor_colors = self._build_grid_lines(
                minor_sp, half_extent, skip_multiple=5, base_rgba=minor_rgba,
                fade_radius=grid_fade_radius)
            if len(minor_verts) >= 2:
                self.grid_minor_item.setData(pos=minor_verts, color=minor_colors, mode='lines',
                                             width=minor_width)
                self.grid_minor_item.setVisible(True)
            else:
                self.grid_minor_item.setVisible(False)
        else:
            self.grid_minor_item.setVisible(False)

        # --- 刻度线与标签（对齐一级网格）---
        tick_verts = []
        tick_colors = []
        label_idx = 0

        axis_defs = [
            (0, (1.0, 0.3, 0.3, 0.7), (1, 2)),  # X
            (1, (0.3, 1.0, 0.3, 0.7), (0, 2)),  # Y
            (2, (0.3, 0.3, 1.0, 0.7), (0, 1)),  # Z
        ]

        for main_ax, color, (perp_a, perp_b) in axis_defs:
            val = major_sp
            while val <= pos_ext + 1e-9:
                for sign_val in ([val, -val] if val > 1e-9 else [val]):
                    if sign_val < fade_start - 1e-9 or sign_val > pos_ext + 1e-9:
                        continue
                    p1 = np.zeros(3, dtype=np.float32)
                    p2 = np.zeros(3, dtype=np.float32)
                    p1[main_ax] = sign_val
                    p2[main_ax] = sign_val
                    p1[perp_a] = -tick_half
                    p2[perp_a] = tick_half
                    tick_verts.append(p1)
                    tick_verts.append(p2)
                    tick_colors.append(color)
                    tick_colors.append(color)

                    if label_idx < self.TICK_LABEL_POOL_SIZE:
                        lbl = self.tick_label_pool[label_idx]
                        lbl_pos = np.zeros(3, dtype=np.float32)
                        lbl_pos[main_ax] = sign_val
                        lbl_pos[perp_a] = tick_half * 2.5
                        v = sign_val
                        if abs(v) < 1e-9:
                            txt = '0'
                        elif abs(v) >= 1000 or (abs(v) < 0.01 and abs(v) > 0):
                            txt = f'{v:.2e}'
                        elif v == int(v):
                            txt = str(int(v))
                        else:
                            txt = f'{v:.2f}'.rstrip('0').rstrip('.')
                        lbl.setData(pos=lbl_pos.astype(float), text=txt)
                        lbl.setVisible(True)
                        label_idx += 1
                val += major_sp

        for i in range(label_idx, self.TICK_LABEL_POOL_SIZE):
            self.tick_label_pool[i].setVisible(False)

        if len(tick_verts) >= 2:
            self.tick_line_item.setData(
                pos=np.array(tick_verts, dtype=np.float32),
                color=np.array(tick_colors, dtype=np.float32),
                mode='lines')
            self.tick_line_item.setVisible(True)
        else:
            self.tick_line_item.setVisible(False)
        
    def clear_all(self):
        """Remove all tracked points, paths, trails and associated data."""
        for item in self.points.values():
            self.removeItem(item)
        for item in self.path_items.values():
            self.removeItem(item)
        for item in self.trail_items.values():
            if isinstance(item, dict):
                for layer in item.get('core', []):
                    self.removeItem(layer)
                for layer in item.get('glow', []):
                    self.removeItem(layer)
            else:
                self.removeItem(item)
        self.points.clear()
        self.point_histories.clear()
        self.point_speeds.clear()
        self.point_times.clear()
        self.point_colors.clear()
        self.path_items.clear()
        self.trail_items.clear()
        self.first_point_rendered = False
        self.update()

    def update_point(self, name, x, y, z, color=(1, 0, 0, 1), size=10):
        """更新或创建 3D 视图中的追踪点。"""
        current_pos = np.array([x, y, z], dtype=float)
        if not np.isfinite(current_pos).all():
            self._render_debug("POINT_INVALID_POSITION", f"收到非法坐标: {current_pos.tolist()}", name, "ERROR")
            return
        pos = np.array([current_pos], dtype=np.float32)
        
        # 首个点的自适应缩放
        if not self.first_point_rendered:
            dist = np.sqrt(x**2 + y**2 + z**2)
            cam_dist = self.cameraParams()['distance']

            if dist > cam_dist * 0.4:
                new_dist = dist * 2.5
                self.setCameraPosition(distance=new_dist, elevation=45, azimuth=45)
                self.scene_scale = self.initial_state['scene_scale']
                self._target_scene_scale = self.scene_scale
                self.update_coordinate_system()

            self.first_point_rendered = True
        
        # 颜色归一化：输入可能是 0-255 或 0-1 范围，统一转为 0-1
        color_arr = np.asarray(color, dtype=float).flatten()
        if color_arr.size == 0:
            self._render_debug("POINT_EMPTY_COLOR", "颜色为空，已回退到默认红色", name, "WARN")
            color_arr = np.array([1.0, 0.0, 0.0, 1.0], dtype=float)
        if color_arr.size == 1:
            color_arr = np.array([color_arr[0], 0.0, 0.0, 1.0], dtype=float)
        if color_arr.size == 2:
            color_arr = np.array([color_arr[0], color_arr[1], 0.0, 1.0], dtype=float)
        if color_arr.size == 3:
            color_arr = np.append(color_arr, 1.0)
        if np.nanmax(color_arr) > 1.0:
            color_arr = color_arr / 255.0
        color_arr = np.clip(color_arr[:4], 0.0, 1.0)
        color_tuple = tuple(float(c) for c in color_arr)
        self.point_colors[name] = color_tuple
            
        if name in self.points:
            # 更新已有点
            self.points[name].setData(pos=pos, color=color_tuple, size=size)
        else:
            # 创建新点（pxMode=True 表示大小单位为像素）
            sp = gl.GLScatterPlotItem(pos=pos, color=color_tuple, size=size, pxMode=True)
            sp.setGLOptions('translucent')
            self.addItem(sp)
            self.points[name] = sp
        
        current_time = time.perf_counter()
        if name not in self.point_histories:
            self.point_histories[name] = [current_pos]
            self.point_speeds[name] = [0.0]
            self.point_times[name] = [current_time]
            if self.full_path_mode or self.trail_mode:
                self._render_debug("POINT_HISTORY_INIT", "已初始化轨迹历史，当前点数=1", name, verbose=True)
        else:
            last_pos = self.point_histories[name][-1]
            last_time = self.point_times[name][-1]
            dt = max(current_time - last_time, 1e-6)
            speed = float(np.linalg.norm(current_pos - last_pos) / dt)
            self.point_histories[name].append(current_pos)
            self.point_speeds[name].append(speed)
            self.point_times[name].append(current_time)
            if len(self.point_histories[name]) > self.max_history_length:
                excess = len(self.point_histories[name]) - self.max_history_length
                self.point_histories[name] = self.point_histories[name][excess:]
                self.point_speeds[name] = self.point_speeds[name][excess:]
                self.point_times[name] = self.point_times[name][excess:]
            if self.full_path_mode or self.trail_mode:
                history_len = len(self.point_histories[name])
                self._render_debug("POINT_HISTORY_APPEND", f"追加轨迹点，当前点数={history_len}，速度={speed:.6f}", name, verbose=True)
        
        if self.full_path_mode:
            self.refresh_full_path(name, color_tuple)
        if self.trail_mode:
            self.refresh_trail(name)

    def set_full_path_mode(self, enabled):
        target_mode = bool(enabled)
        if target_mode == self.full_path_mode:
            return
        self.full_path_mode = target_mode
        self._render_debug("MODE_FULL_PATH", f"全路径模式={'开启' if self.full_path_mode else '关闭'}，当前点数量={len(self.points)}")
        if self.full_path_mode:
            for name in self.points.keys():
                path_color = self.point_colors.get(name, (1, 1, 1, 1))
                self.refresh_full_path(name, path_color)
        else:
            for item in self.path_items.values():
                item.setVisible(False)
        self.update()

    def set_trail_mode(self, enabled):
        target_mode = bool(enabled)
        if target_mode == self.trail_mode:
            pass
            
        self.trail_mode = target_mode
        # 强制发送日志
        self.log_message.emit(f"[Viewer3D][INFO][GLOBAL][MODE_TRAIL] 速度尾迹模式={'开启' if self.trail_mode else '关闭'}，当前点数量={len(self.points)}")
        
        if self.trail_mode:
            for name in self.points.keys():
                self.refresh_trail(name)
        else:
            for item in self.trail_items.values():
                self._hide_trail_item(item)
        self.update()

    def set_trail_length(self, length):
        self.trail_length = max(10, int(length))
        # 强制发送日志
        self.log_message.emit(f"[Viewer3D][INFO][GLOBAL][TRAIL_LENGTH_UPDATE] 尾迹长度已更新为 {self.trail_length}")
        
        if self.trail_mode:
            for name in self.points.keys():
                self.refresh_trail(name)
            self.update()

    def refresh_full_path(self, name, color):
        try:
            history = self.point_histories.get(name, [])
            if len(history) < 2:
                self._render_debug("PATH_SKIPPED_NOT_ENOUGH_POINTS", f"轨迹点不足，history_len={len(history)}", name, "WARN")
                if name in self.path_items:
                    self.path_items[name].setVisible(False)
                return
            if name not in self.path_items:
                path_item = gl.GLLinePlotItem(mode='lines', width=2.0, antialias=True)
                path_item.setGLOptions('translucent')
                self.addItem(path_item)
                self.path_items[name] = path_item
                self._render_debug("PATH_ITEM_CREATED", "已创建全路径绘制对象", name)
            rgba = np.array(color, dtype=np.float32)
            if rgba.shape[0] == 3:
                rgba = np.append(rgba, 1.0)
            rgba[3] = 0.8
            path_pos = np.array(history, dtype=np.float32)
            segment_pos, _, reason = self._build_line_segments(path_pos)
            if segment_pos is None:
                self._render_debug("PATH_SEGMENT_BUILD_FAILED", reason or "未知原因", name, "ERROR")
                self.path_items[name].setVisible(False)
                return
            segment_color = np.tile(rgba, (len(segment_pos), 1))
            self.path_items[name].setData(pos=segment_pos, color=segment_color, mode='lines', width=2.0)
            self.path_items[name].setVisible(True)
            self._render_debug("PATH_RENDER_OK", f"全路径渲染成功，segment_count={len(segment_pos)}", name)
            self.update()
        except Exception as exc:
            self._render_debug("PATH_RENDER_EXCEPTION", f"{type(exc).__name__}: {exc}", name, "ERROR")
            if name in self.path_items:
                self.path_items[name].setVisible(False)

    def refresh_trail(self, name):
        try:
            history = self.point_histories.get(name, [])
            speeds = self.point_speeds.get(name, [])
            if len(history) < 2:
                self._render_debug("TRAIL_SKIPPED_NOT_ENOUGH_POINTS", f"轨迹点不足，history_len={len(history)}", name, "WARN")
                if name in self.trail_items:
                    self._hide_trail_item(self.trail_items[name])
                return
            start_idx = max(0, len(history) - self.trail_length)
            trail_pos = np.array(history[start_idx:], dtype=np.float32)
            trail_speeds = np.array(speeds[start_idx:], dtype=np.float32)
            finite_mask = np.isfinite(trail_pos).all(axis=1) & np.isfinite(trail_speeds)
            filtered_out = int(len(trail_pos) - int(np.sum(finite_mask)))
            if filtered_out > 0:
                self._render_debug("TRAIL_FILTERED_NONFINITE", f"已过滤非法样本={filtered_out}", name, "WARN")
            trail_pos = trail_pos[finite_mask]
            trail_speeds = trail_speeds[finite_mask]
            if len(trail_pos) < 2:
                self._render_debug("TRAIL_SKIPPED_AFTER_FILTER", f"过滤后点不足，valid_len={len(trail_pos)}", name, "WARN")
                if name in self.trail_items:
                    self._hide_trail_item(self.trail_items[name])
                return
            norm, speed_low, speed_high = self._normalize_speed_for_trail(trail_speeds)
            if speed_high - speed_low < 1e-6:
                self._render_debug("TRAIL_SPEED_FLAT", f"速度近似恒定，low={speed_low:.6f}, high={speed_high:.6f}", name, "WARN")
            ages = np.linspace(0.0, 1.0, len(trail_pos), dtype=np.float32)
            colors = np.zeros((len(trail_pos), 4), dtype=np.float32)
            colors[:, :3] = self._speed_to_comet_rgb(norm)
            colors[:, 3] = np.linspace(0.12, 0.95, len(trail_pos), dtype=np.float32)
            segment_pos, segment_color, reason = self._build_line_segments(trail_pos, colors)
            if segment_pos is None or segment_color is None:
                self._render_debug("TRAIL_SEGMENT_BUILD_FAILED", reason or "未知原因", name, "ERROR")
                if name in self.trail_items:
                    self._hide_trail_item(self.trail_items[name])
                return
            trail_item = self.trail_items.get(name)
            if isinstance(trail_item, dict):
                self._hide_trail_item(trail_item)
                trail_item = None
            if trail_item is None:
                trail_item = gl.GLLinePlotItem(mode='lines', width=4.0, antialias=True)
                trail_item.setGLOptions('translucent')
                self.addItem(trail_item)
                self.trail_items[name] = trail_item
                self._render_debug("TRAIL_ITEM_CREATED", "已创建连续尾迹绘制对象", name)
            speed_head = float(np.mean(norm[-min(5, len(norm)):])) if len(norm) > 0 else 0.0
            width_ratio = 0.25 + 0.75 * speed_head
            trail_width = self.trail_width_min + (self.trail_width_max - self.trail_width_min) * width_ratio
            trail_item.setData(pos=segment_pos, color=segment_color, mode='lines', width=trail_width)
            trail_item.setVisible(True)
            self._render_debug(
                "TRAIL_RENDER_OK",
                f"连续尾迹渲染成功，segment_count={len(segment_pos)}，trail_len={len(trail_pos)}，width={trail_width:.2f}，speed_low={speed_low:.4f}，speed_high={speed_high:.4f}",
                name
            )
            self.update()
        except Exception as exc:
            self._render_debug("TRAIL_RENDER_EXCEPTION", f"{type(exc).__name__}: {exc}", name, "ERROR")
            if name in self.trail_items:
                self._hide_trail_item(self.trail_items[name])
            
    def mousePressEvent(self, ev):
        """处理鼠标按下事件。"""
        self.setFocus()  # 确保组件获得焦点以接收键盘事件
        self.mousePos = ev.pos()
        
        # 中键触发复位动画
        if ev.button() == Qt.MouseButton.MiddleButton:
            # 启动长按计时器（1 秒）
            self.is_long_press = False
            self.long_press_timer.start(1000)
            ev.accept()
            return
            
        # 左键或右键：接受事件
        if ev.button() == Qt.MouseButton.LeftButton or ev.button() == Qt.MouseButton.RightButton:
            # 用户交互时停止动画
            if self.animation_timer.isActive():
                self.animation_timer.stop()
            ev.accept()
        else:
            super().mousePressEvent(ev)

    def mouseReleaseEvent(self, ev):
        """处理鼠标释放事件。"""
        if ev.button() == Qt.MouseButton.MiddleButton:
            # 计时器仍在运行说明是短按
            if self.long_press_timer.isActive():
                self.long_press_timer.stop()
                if not self.is_long_press:
                    # 短按：部分复位（保持缩放比例）
                    self.start_reset_animation(full_reset=False)
            
            # 重置标志
            self.is_long_press = False
            ev.accept()
        else:
            super().mouseReleaseEvent(ev)

    def on_long_press_timeout(self):
        """中键按住超过 1 秒时触发完全复位。"""
        self.is_long_press = True
        # 完全复位（缩放比例也恢复默认）
        self.start_reset_animation(full_reset=True)

    def keyPressEvent(self, ev):
        """处理键盘按键事件。"""
        if ev.key() == Qt.Key_R:
            self.auto_fit_view()
            ev.accept()
        else:
            super().keyPressEvent(ev)

    def auto_fit_view(self):
        """
        自动调整视角以适配所有追踪点。
        相机距离设为最远点的合适倍数，坐标轴、刻度和网格通过
        update_coordinate_system() 在每个动画帧中自动跟随更新。
        """
        if not self.points:
            return

        max_dist = 0
        for item in self.points.values():
            pos_data = item.pos
            if pos_data is None or len(pos_data) == 0:
                continue
            dists = np.linalg.norm(pos_data, axis=1)
            if len(dists) > 0:
                current_max = np.max(dists)
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

    def mouseMoveEvent(self, ev):
        """处理鼠标移动事件，实现旋转和平移。"""
        diff = ev.pos() - self.mousePos
        self.mousePos = ev.pos()

        if ev.buttons() == Qt.MouseButton.LeftButton:
            # 旋转（轨道）
            self.orbit(diff.x(), diff.y())
            self._update_arrow_billboard()
            self.camera_changed.emit()
            
        elif ev.buttons() == Qt.MouseButton.RightButton:
            dist = self.cameraParams()['distance']
            scale = dist / self.scene_scale * 0.001
            
            self.pan_offset.setX(self.pan_offset.x() + diff.x() * scale)
            self.pan_offset.setY(self.pan_offset.y() - diff.y() * scale)  # Y 方向反转
            
            self.update()
            
        else:
            super().mouseMoveEvent(ev)

    def viewMatrix(self):
        """
        重写视图矩阵以加入自定义平移偏移和场景缩放。
        旋转始终围绕原点 (0,0,0)，滚轮缩放通过 scene_scale 缩放世界坐标系。
        """
        m = QMatrix4x4()
        
        m.translate(self.pan_offset.x(), self.pan_offset.y(), 0)
        m.translate(0, 0, -self.opts['distance'])
        m.rotate(self.opts['elevation']-90, 1, 0, 0)
        m.rotate(self.opts['azimuth'], 0, 0, 1)
        
        # 场景缩放：缩放整个世界坐标系，而非移动相机
        s = self.scene_scale
        m.scale(s, s, s)
        
        center = self.opts['center']
        m.translate(-center.x(), -center.y(), -center.z())
        
        return m

    def wheelEvent(self, ev):
        """处理滚轮缩放：缩放场景而非移动相机（平滑过渡）。"""
        if self.animation_timer.isActive():
            self.animation_timer.stop()

        delta = ev.angleDelta().y()
        factor = 1.1 if delta > 0 else (1.0 / 1.1)

        self._target_scene_scale *= factor
        if not self._zoom_anim_timer.isActive():
            self._zoom_anim_timer.start()
        ev.accept()

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

        new_dist = self.start_state['distance'] + (self.target_state['distance'] - self.start_state['distance']) * ease
        new_elev = self.start_state['elevation'] + (self.target_state['elevation'] - self.start_state['elevation']) * ease
        new_azim = self.start_state['azimuth'] + (self.target_state['azimuth'] - self.start_state['azimuth']) * ease

        new_pan_x = self.start_state['pan_x'] + (self.target_state['pan_x'] - self.start_state['pan_x']) * ease
        new_pan_y = self.start_state['pan_y'] + (self.target_state['pan_y'] - self.start_state['pan_y']) * ease
        self.pan_offset.setX(new_pan_x)
        self.pan_offset.setY(new_pan_y)

        start_scale = self.start_state.get('scene_scale', 1.0)
        target_scale = self.target_state.get('scene_scale', 1.0)
        self.scene_scale = start_scale + (target_scale - start_scale) * ease
        self._target_scene_scale = self.scene_scale

        self.setCameraPosition(
            distance=new_dist,
            elevation=new_elev,
            azimuth=new_azim
        )
        self.update_coordinate_system()
        self.camera_changed.emit()
        self.update()
