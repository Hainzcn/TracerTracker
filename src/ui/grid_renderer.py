"""
3D 场景网格、坐标轴与刻度渲染器。

负责管理 XOY 平面网格（双层主副线）、三轴坐标轴（含箭头与标签）
以及刻度线和刻度标签的所有 GL 对象。
"""

import math

import numpy as np
import pyqtgraph.opengl as gl
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from OpenGL.GL import (
    GL_DEPTH_TEST, GL_BLEND, GL_ALPHA_TEST, GL_CULL_FACE,
    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
)

_LOG5 = math.log(5.0)


class GridRenderer:
    """
    管理 3D 场景中的网格、坐标轴和刻度对象。
    由 Viewer3D 持有，通过 ``update()`` 在相机参数变化时刷新。
    """

    AXIS_VISUAL_RATIO = 0.28
    TICK_LABEL_POOL_SIZE = 60
    TICK_LINE_LENGTH_RATIO = 0.02

    GRID_MAJOR_WIDTH = 1.6
    GRID_MINOR_WIDTH_MIN = 0.5
    GRID_MAJOR_ALPHA = 40
    AXIS_LABEL_FADE_POWER = 6.0
    ORTHO_SWITCH_BAND = 0.16
    SUBMERSION_DESAT_MAX = 0.72
    SUBMERSION_DIM_MAX = 0.45
    SUBMERSION_DEPTH_RANGE = 0.85

    GRID_PLANES = {
        'xoy': {'axes': (0, 1), 'normal_axis': 2},
        'xoz': {'axes': (0, 2), 'normal_axis': 1},
        'yoz': {'axes': (1, 2), 'normal_axis': 0},
    }

    def __init__(self, viewer):
        self._viewer = viewer

        _grid_gl_opts = {
            GL_DEPTH_TEST: False,
            GL_BLEND: True,
            GL_ALPHA_TEST: False,
            GL_CULL_FACE: False,
            'glBlendFunc': (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
        }

        self.grid_major_items = {}
        self.grid_minor_items = {}
        for plane in self.GRID_PLANES:
            major_item = gl.GLLinePlotItem(
                mode='lines', width=self.GRID_MAJOR_WIDTH, antialias=True,
            )
            major_item.setGLOptions(_grid_gl_opts)
            major_item.setDepthValue(-1000)
            viewer.addItem(major_item)
            self.grid_major_items[plane] = major_item

            minor_item = gl.GLLinePlotItem(
                mode='lines', width=self.GRID_MINOR_WIDTH_MIN, antialias=True,
            )
            minor_item.setGLOptions(_grid_gl_opts)
            minor_item.setDepthValue(-1000)
            viewer.addItem(minor_item)
            self.grid_minor_items[plane] = minor_item

        # 保留默认 XOY 引用，避免外部代码若有直接访问时失效。
        self.grid_major_item = self.grid_major_items['xoy']
        self.grid_minor_item = self.grid_minor_items['xoy']

        self._setup_axes()

    # ------------------------------------------------------------------
    # Axes setup
    # ------------------------------------------------------------------

    def _create_axis_label(self, text, pos, color):
        try:
            label = gl.GLTextItem(
                pos=np.asarray(pos, dtype=float), text=text, color=color,
            )
            label.setData(alignment=Qt.AlignmentFlag.AlignCenter)
            self._viewer.addItem(label)
            return label
        except Exception:
            return None

    def _setup_axes(self):
        """创建轴线、箭头、标签和刻度结构。"""
        self.axes_width = 3
        self.axis_label_base_colors = {
            'x': QColor(255, 0, 0, 255),
            'y': QColor(0, 255, 0, 255),
            'z': QColor(0, 0, 255, 255),
        }
        self.tick_label_base_color = QColor(180, 180, 180, 200)

        self.x_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)
        self.y_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)
        self.z_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)

        self._viewer.addItem(self.x_axis)
        self._viewer.addItem(self.y_axis)
        self._viewer.addItem(self.z_axis)

        self.axis_arrows = gl.GLMeshItem(
            smooth=True, computeNormals=False, glOptions='translucent',
        )
        self._viewer.addItem(self.axis_arrows)

        self.x_label = self._create_axis_label(
            'X', [20.0, 0.0, 0.0], self.axis_label_base_colors['x'],
        )
        self.y_label = self._create_axis_label(
            'Y', [0.0, 20.0, 0.0], self.axis_label_base_colors['y'],
        )
        self.z_label = self._create_axis_label(
            'Z', [0.0, 0.0, 20.0], self.axis_label_base_colors['z'],
        )

        self.tick_line_item = gl.GLLinePlotItem(
            width=1.5, antialias=True, mode='lines',
        )
        self.tick_line_item.setGLOptions('translucent')
        self._viewer.addItem(self.tick_line_item)

        self.tick_label_pool = []
        for _ in range(self.TICK_LABEL_POOL_SIZE):
            lbl = gl.GLTextItem(
                pos=np.zeros(3, dtype=float), text='',
                color=self.tick_label_base_color,
            )
            lbl.setVisible(False)
            self._viewer.addItem(lbl)
            self.tick_label_pool.append(lbl)

    # ------------------------------------------------------------------
    # Static helpers
    # ------------------------------------------------------------------

    @staticmethod
    def compute_grid_spacings(view_range, target_minor_count=15):
        """
        使用纯x5层级序列计算两级网格间距。
        返回（次要间距、主要间距、相位_t）。
        """
        if view_range <= 0:
            return 1.0, 5.0, 0.5
        ideal = view_range / target_minor_count
        log5_val = math.log(max(ideal, 1e-15)) / _LOG5
        level = math.floor(log5_val)
        minor = 5.0 ** level
        major = 5.0 * minor
        phase = log5_val - level
        return minor, major, max(0.0, min(1.0, phase))

    @staticmethod
    def build_grid_lines(plane, spacing, half_extent, skip_multiple=0,
                         base_rgba=None, fade_radius=None):
        """
        生成指定坐标平面上具有边缘渐变效果的线对顶点。
        返回（位置、颜色）numpy数组。
        """
        empty_pos = np.zeros((0, 3), dtype=np.float32)
        empty_col = np.zeros((0, 4), dtype=np.float32)
        plane_cfg = GridRenderer.GRID_PLANES.get(plane)
        if (spacing <= 0 or half_extent <= 0 or base_rgba is None
                or plane_cfg is None):
            return empty_pos, empty_col

        n = int(half_extent / spacing + 0.5)
        verts = []
        colors = []
        axis_u, axis_v = plane_cfg['axes']

        do_fade = fade_radius is not None and fade_radius < half_extent
        inv_fade_range = (
            1.0 / max(half_extent - fade_radius, 1e-9)
            if do_fade else 1.0
        )

        def _plane_point(u, v):
            point = [0.0, 0.0, 0.0]
            point[axis_u] = u
            point[axis_v] = v
            return point

        for i in range(-n, n + 1):
            if skip_multiple > 0 and i % skip_multiple == 0:
                continue
            coord = i * spacing
            d = abs(coord)

            if do_fade and d > fade_radius:
                alpha_mult = max(
                    0.0,
                    1.0 - ((d - fade_radius) * inv_fade_range) ** 2,
                )
                if alpha_mult < 1e-4:
                    continue
            else:
                alpha_mult = 1.0

            c_max = base_rgba.copy()
            c_max[3] *= alpha_mult

            if do_fade:
                c_zero = c_max.copy()
                c_zero[3] = 0.0

                verts.extend([
                    _plane_point(-half_extent, coord),
                    _plane_point(-fade_radius, coord),
                ])
                colors.extend([c_zero, c_max])
                verts.extend([
                    _plane_point(-fade_radius, coord),
                    _plane_point(fade_radius, coord),
                ])
                colors.extend([c_max, c_max])
                verts.extend([
                    _plane_point(fade_radius, coord),
                    _plane_point(half_extent, coord),
                ])
                colors.extend([c_max, c_zero])

                verts.extend([
                    _plane_point(coord, -half_extent),
                    _plane_point(coord, -fade_radius),
                ])
                colors.extend([c_zero, c_max])
                verts.extend([
                    _plane_point(coord, -fade_radius),
                    _plane_point(coord, fade_radius),
                ])
                colors.extend([c_max, c_max])
                verts.extend([
                    _plane_point(coord, fade_radius),
                    _plane_point(coord, half_extent),
                ])
                colors.extend([c_max, c_zero])
            else:
                verts.extend([
                    _plane_point(-half_extent, coord),
                    _plane_point(half_extent, coord),
                ])
                colors.extend([c_max, c_max])
                verts.extend([
                    _plane_point(coord, -half_extent),
                    _plane_point(coord, half_extent),
                ])
                colors.extend([c_max, c_max])

        if not verts:
            return empty_pos, empty_col
        return (
            np.array(verts, dtype=np.float32),
            np.array(colors, dtype=np.float32),
        )

    @staticmethod
    def _camera_direction(camera_params):
        elev = np.radians(camera_params['elevation'])
        azim = np.radians(camera_params['azimuth'])
        cam_dir = np.array([
            np.cos(elev) * np.sin(azim),
            np.cos(elev) * np.cos(azim),
            -np.sin(elev),
        ], dtype=np.float32)
        cam_len = np.linalg.norm(cam_dir)
        if cam_len > 1e-6:
            cam_dir /= cam_len
        else:
            cam_dir = np.array([0, 1, 0], dtype=np.float32)
        return cam_dir

    @staticmethod
    def _apply_submersion_to_rgba(rgba, depth_factor):
        depth_factor = max(0.0, min(1.0, float(depth_factor)))
        if depth_factor <= 1e-6:
            return np.array(rgba, dtype=np.float32)

        rgb = np.array(rgba[:3], dtype=np.float32)
        alpha = float(rgba[3])
        gray = float(np.dot(
            rgb,
            np.array([0.299, 0.587, 0.114], dtype=np.float32),
        ))
        desat = depth_factor * GridRenderer.SUBMERSION_DESAT_MAX
        dim = 1.0 - depth_factor * GridRenderer.SUBMERSION_DIM_MAX
        styled_rgb = (rgb * (1.0 - desat) + gray * desat) * dim
        styled_rgb = np.clip(styled_rgb, 0.0, 1.0)
        return np.array(
            [styled_rgb[0], styled_rgb[1], styled_rgb[2], alpha],
            dtype=np.float32,
        )

    @classmethod
    def _apply_visibility_to_color(cls, base_color, visibility,
                                   depth_factor=0.0):
        color = QColor(base_color)
        visibility = max(0.0, min(1.0, float(visibility)))
        rgba = np.array([
            color.redF(), color.greenF(), color.blueF(), color.alphaF(),
        ], dtype=np.float32)
        styled = cls._apply_submersion_to_rgba(rgba, depth_factor)
        return QColor.fromRgbF(
            float(styled[0]),
            float(styled[1]),
            float(styled[2]),
            max(0.0, min(1.0, color.alphaF() * visibility)),
        )

    @staticmethod
    def _plane_for_axis(axis_key):
        return {
            'x': 'yoz',
            'y': 'xoz',
            'z': 'xoy',
        }[axis_key]

    def _analyze_camera_axes(self, cam_dir):
        axis_components = {
            'x': abs(float(cam_dir[0])),
            'y': abs(float(cam_dir[1])),
            'z': abs(float(cam_dir[2])),
        }
        ordered_axes = sorted(
            axis_components,
            key=axis_components.get,
            reverse=True,
        )
        return axis_components, ordered_axes

    def _compute_target_plane_weights(self, cam_dir):
        axis_components, ordered_axes = self._analyze_camera_axes(cam_dir)
        primary_axis = ordered_axes[0]
        secondary_axis = ordered_axes[1]

        primary_strength = axis_components[primary_axis]
        secondary_strength = axis_components[secondary_axis]
        diff = primary_strength - secondary_strength

        primary_plane = self._plane_for_axis(primary_axis)
        secondary_plane = self._plane_for_axis(secondary_axis)
        weights = {plane: 0.0 for plane in self.GRID_PLANES}

        if diff >= self.ORTHO_SWITCH_BAND:
            weights[primary_plane] = 1.0
            return weights

        blend_t = max(0.0, min(1.0, diff / max(self.ORTHO_SWITCH_BAND, 1e-9)))
        primary_weight = 0.5 + 0.5 * blend_t
        weights[primary_plane] = primary_weight
        weights[secondary_plane] = 1.0 - primary_weight
        return weights

    @staticmethod
    def _dominant_plane(plane_weights):
        return max(
            plane_weights.items(),
            key=lambda item: item[1],
        )[0]

    def _compute_plane_weights(self, cam_dir):
        ortho_mix = max(
            0.0, min(1.0, float(getattr(self._viewer, '_ortho_blend', 0.0))),
        )
        default_weights = {'xoy': 1.0, 'xoz': 0.0, 'yoz': 0.0}
        if ortho_mix <= 1e-4:
            return default_weights

        target_weights = self._compute_target_plane_weights(cam_dir)

        return {
            plane: (
                default_weights[plane] * (1.0 - ortho_mix)
                + target_weights[plane] * ortho_mix
            )
            for plane in self.GRID_PLANES
        }

    def _compute_axis_label_visibility(self, cam_dir):
        axis_components, _ = self._analyze_camera_axes(cam_dir)
        return {
            axis: max(
                0.0,
                min(
                    1.0,
                    1.0 - axis_components[axis] ** self.AXIS_LABEL_FADE_POWER,
                ),
            )
            for axis in axis_components
        }

    def _compute_submersion_factor(self, point, dominant_plane, plane_weights,
                                   cam_dir, axis_length):
        plane_weight = plane_weights.get(dominant_plane, 0.0)
        if plane_weight <= 1e-4:
            return 0.0

        normal_axis = self.GRID_PLANES[dominant_plane]['normal_axis']
        cam_component = float(cam_dir[normal_axis])
        if abs(cam_component) <= 1e-6:
            return 0.0

        signed_depth = float(point[normal_axis]) * cam_component
        if signed_depth <= 0.0:
            return 0.0

        normalized = min(
            1.0,
            signed_depth / max(axis_length * self.SUBMERSION_DEPTH_RANGE, 1e-6),
        )
        return normalized * plane_weight

    def _set_text_item_visibility(self, item, pos, base_color, visibility,
                                  text=None, depth_factor=0.0):
        if item is None:
            return
        color = self._apply_visibility_to_color(
            base_color,
            visibility,
            depth_factor=depth_factor,
        )
        item.setData(pos=pos.astype(float), color=color, **(
            {'text': text} if text is not None else {}
        ))
        item.setVisible(color.alpha() > 0)

    # ------------------------------------------------------------------
    # Arrow billboard
    # ------------------------------------------------------------------

    def update_arrow_billboard(self, camera_params=None, pos_ext=None,
                               axis_length=None, scene_scale=None,
                               plane_weights=None):
        """重新计算 Billboard 效果箭头三角形，使其始终面向相机。"""
        if camera_params is None:
            camera_params = self._viewer.cameraParams()

        if pos_ext is None or axis_length is None:
            sc = scene_scale if scene_scale is not None else 1.0
            dist = camera_params['distance']
            axis_length = dist * self.AXIS_VISUAL_RATIO / sc
            pos_ext = axis_length

        arrow_len = axis_length * 0.05
        arrow_width = arrow_len * 0.4

        cam_dir = self._camera_direction(camera_params)
        if plane_weights is None:
            plane_weights = self._compute_plane_weights(cam_dir)
        dominant_plane = self._dominant_plane(plane_weights)

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
            tip_x,  base_x - w_x, base_x + w_x,
            tip_y,  base_y - w_y, base_y + w_y,
            tip_z,  base_z - w_z, base_z + w_z,
        ], dtype=np.float32)

        faces = np.array([
            [0, 1, 2],
            [3, 4, 5],
            [6, 7, 8],
        ], dtype=np.uint32)

        x_depth = self._compute_submersion_factor(
            tip_x, dominant_plane, plane_weights, cam_dir, axis_length,
        )
        y_depth = self._compute_submersion_factor(
            tip_y, dominant_plane, plane_weights, cam_dir, axis_length,
        )
        z_depth = self._compute_submersion_factor(
            tip_z, dominant_plane, plane_weights, cam_dir, axis_length,
        )
        vertex_colors = np.array([
            self._apply_submersion_to_rgba([1, 0, 0, 1], x_depth),
            self._apply_submersion_to_rgba([1, 0, 0, 1], x_depth),
            self._apply_submersion_to_rgba([1, 0, 0, 1], x_depth),
            self._apply_submersion_to_rgba([0, 1, 0, 1], y_depth),
            self._apply_submersion_to_rgba([0, 1, 0, 1], y_depth),
            self._apply_submersion_to_rgba([0, 1, 0, 1], y_depth),
            self._apply_submersion_to_rgba([0, 0, 1, 1], z_depth),
            self._apply_submersion_to_rgba([0, 0, 1, 1], z_depth),
            self._apply_submersion_to_rgba([0, 0, 1, 1], z_depth),
        ], dtype=np.float32)

        self.axis_arrows.setMeshData(
            vertexes=verts, faces=faces, vertexColors=vertex_colors,
        )

    # ------------------------------------------------------------------
    # Main update
    # ------------------------------------------------------------------

    def update(self, distance, scene_scale):
        """根据当前相机状态刷新轴线、网格和刻度。"""
        axis_length = distance * self.AXIS_VISUAL_RATIO / scene_scale
        neg_ext = -axis_length * 0.5
        pos_ext = axis_length
        fade_start = neg_ext * 0.5
        cam_params = self._viewer.cameraParams()
        cam_dir = self._camera_direction(cam_params)
        plane_weights = self._compute_plane_weights(cam_dir)
        dominant_plane = self._dominant_plane(plane_weights)
        axis_label_visibility = self._compute_axis_label_visibility(cam_dir)

        # --- Axis lines (with negative-half fade) ---
        x_axis_pos = np.array([
            [neg_ext, 0, 0], [fade_start, 0, 0], [pos_ext, 0, 0],
        ], dtype=np.float32)
        self.x_axis.setData(
            pos=x_axis_pos,
            color=np.array([
                self._apply_submersion_to_rgba(
                    [1, 0, 0, 0],
                    self._compute_submersion_factor(
                        x_axis_pos[0], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
                self._apply_submersion_to_rgba(
                    [1, 0, 0, 1],
                    self._compute_submersion_factor(
                        x_axis_pos[1], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
                self._apply_submersion_to_rgba(
                    [1, 0, 0, 1],
                    self._compute_submersion_factor(
                        x_axis_pos[2], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
            ], dtype=np.float32),
            mode='line_strip',
        )
        y_axis_pos = np.array([
            [0, neg_ext, 0], [0, fade_start, 0], [0, pos_ext, 0],
        ], dtype=np.float32)
        self.y_axis.setData(
            pos=y_axis_pos,
            color=np.array([
                self._apply_submersion_to_rgba(
                    [0, 1, 0, 0],
                    self._compute_submersion_factor(
                        y_axis_pos[0], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
                self._apply_submersion_to_rgba(
                    [0, 1, 0, 1],
                    self._compute_submersion_factor(
                        y_axis_pos[1], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
                self._apply_submersion_to_rgba(
                    [0, 1, 0, 1],
                    self._compute_submersion_factor(
                        y_axis_pos[2], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
            ], dtype=np.float32),
            mode='line_strip',
        )
        z_axis_pos = np.array([
            [0, 0, neg_ext], [0, 0, fade_start], [0, 0, pos_ext],
        ], dtype=np.float32)
        self.z_axis.setData(
            pos=z_axis_pos,
            color=np.array([
                self._apply_submersion_to_rgba(
                    [0, 0, 1, 0],
                    self._compute_submersion_factor(
                        z_axis_pos[0], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
                self._apply_submersion_to_rgba(
                    [0, 0, 1, 1],
                    self._compute_submersion_factor(
                        z_axis_pos[1], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
                self._apply_submersion_to_rgba(
                    [0, 0, 1, 1],
                    self._compute_submersion_factor(
                        z_axis_pos[2], dominant_plane, plane_weights, cam_dir,
                        axis_length,
                    ),
                ),
            ], dtype=np.float32),
            mode='line_strip',
        )

        # --- Axis arrows (billboard) ---
        self.update_arrow_billboard(
            cam_params, pos_ext, axis_length, scene_scale,
            plane_weights=plane_weights,
        )

        # --- Axis name labels ---
        label_offset = pos_ext + axis_length * 0.05 * 1.5
        if self.x_label is not None:
            x_label_pos = np.array([label_offset, 0, 0], dtype=np.float32)
            self._set_text_item_visibility(
                self.x_label,
                x_label_pos,
                self.axis_label_base_colors['x'],
                axis_label_visibility['x'],
                depth_factor=self._compute_submersion_factor(
                    x_label_pos, dominant_plane, plane_weights, cam_dir,
                    axis_length,
                ),
            )
        if self.y_label is not None:
            y_label_pos = np.array([0, label_offset, 0], dtype=np.float32)
            self._set_text_item_visibility(
                self.y_label,
                y_label_pos,
                self.axis_label_base_colors['y'],
                axis_label_visibility['y'],
                depth_factor=self._compute_submersion_factor(
                    y_label_pos, dominant_plane, plane_weights, cam_dir,
                    axis_length,
                ),
            )
        if self.z_label is not None:
            z_label_pos = np.array([0, 0, label_offset], dtype=np.float32)
            self._set_text_item_visibility(
                self.z_label,
                z_label_pos,
                self.axis_label_base_colors['z'],
                axis_label_visibility['z'],
                depth_factor=self._compute_submersion_factor(
                    z_label_pos, dominant_plane, plane_weights, cam_dir,
                    axis_length,
                ),
            )

        # --- Grid & tick spacing ---
        total_range = pos_ext - neg_ext
        minor_sp, major_sp, phase_t = self.compute_grid_spacings(total_range)
        tick_half = axis_length * self.TICK_LINE_LENGTH_RATIO

        fade = max(0.0, min(1.0, 1.0 - phase_t))
        minor_alpha = int(self.GRID_MAJOR_ALPHA * fade ** 3.0)
        minor_width = (
            self.GRID_MINOR_WIDTH_MIN
            + (self.GRID_MAJOR_WIDTH - self.GRID_MINOR_WIDTH_MIN) * fade ** 2.0
        )

        # --- Grid (XOY plane) ---
        half_extent_raw = max(pos_ext * 4, 1e-3)
        half_extent = (
            math.ceil(half_extent_raw / max(major_sp, 1e-15)) * major_sp
        )
        grid_fade_radius = pos_ext * 1.2

        for plane in self.GRID_PLANES:
            plane_weight = plane_weights[plane]
            major_item = self.grid_major_items[plane]
            minor_item = self.grid_minor_items[plane]

            if plane_weight <= 1e-4:
                major_item.setVisible(False)
                minor_item.setVisible(False)
                continue

            major_rgba = np.array(
                [
                    0.0, 1.0, 1.0,
                    (self.GRID_MAJOR_ALPHA / 255.0) * plane_weight,
                ],
                dtype=np.float32,
            )
            major_verts, major_colors = self.build_grid_lines(
                plane, major_sp, half_extent, base_rgba=major_rgba,
                fade_radius=grid_fade_radius,
            )
            if len(major_verts) >= 2:
                major_item.setData(
                    pos=major_verts, color=major_colors, mode='lines',
                )
                major_item.setVisible(True)
            else:
                major_item.setVisible(False)

            if minor_alpha > 0:
                minor_rgba = np.array(
                    [0.0, 1.0, 1.0, (minor_alpha / 255.0) * plane_weight],
                    dtype=np.float32,
                )
                minor_verts, minor_colors = self.build_grid_lines(
                    plane, minor_sp, half_extent, skip_multiple=5,
                    base_rgba=minor_rgba, fade_radius=grid_fade_radius,
                )
                if len(minor_verts) >= 2:
                    minor_item.setData(
                        pos=minor_verts, color=minor_colors, mode='lines',
                        width=minor_width,
                    )
                    minor_item.setVisible(True)
                else:
                    minor_item.setVisible(False)
            else:
                minor_item.setVisible(False)

        # --- Tick lines & labels (aligned to major grid) ---
        tick_verts = []
        tick_colors = []
        label_idx = 0

        axis_defs = [
            ('x', 0, (1.0, 0.3, 0.3, 0.7), (1, 2)),
            ('y', 1, (0.3, 1.0, 0.3, 0.7), (0, 2)),
            ('z', 2, (0.3, 0.3, 1.0, 0.7), (0, 1)),
        ]

        for axis_key, main_ax, color, (perp_a, perp_b) in axis_defs:
            tick_visibility = axis_label_visibility[axis_key]
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
                    tick_depth = self._compute_submersion_factor(
                        (p1 + p2) * 0.5,
                        dominant_plane,
                        plane_weights,
                        cam_dir,
                        axis_length,
                    )
                    tick_verts.append(p1)
                    tick_verts.append(p2)
                    tick_colors.append(
                        self._apply_submersion_to_rgba(color, tick_depth),
                    )
                    tick_colors.append(
                        self._apply_submersion_to_rgba(color, tick_depth),
                    )

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
                        self._set_text_item_visibility(
                            lbl,
                            lbl_pos,
                            self.tick_label_base_color,
                            tick_visibility,
                            text=txt,
                            depth_factor=self._compute_submersion_factor(
                                lbl_pos, dominant_plane, plane_weights, cam_dir,
                                axis_length,
                            ),
                        )
                        label_idx += 1
                val += major_sp

        for i in range(label_idx, self.TICK_LABEL_POOL_SIZE):
            self.tick_label_pool[i].setVisible(False)

        if len(tick_verts) >= 2:
            self.tick_line_item.setData(
                pos=np.array(tick_verts, dtype=np.float32),
                color=np.array(tick_colors, dtype=np.float32),
                mode='lines',
            )
            self.tick_line_item.setVisible(True)
        else:
            self.tick_line_item.setVisible(False)
