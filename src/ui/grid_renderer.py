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
    """管理 3D 场景中的网格、坐标轴和刻度对象。

    由 Viewer3D 持有，通过 ``update()`` 在相机参数变化时刷新。
    """

    AXIS_VISUAL_RATIO = 0.28
    TICK_LABEL_POOL_SIZE = 60
    TICK_LINE_LENGTH_RATIO = 0.02

    GRID_MAJOR_WIDTH = 1.6
    GRID_MINOR_WIDTH_MIN = 0.5
    GRID_MAJOR_ALPHA = 40

    def __init__(self, viewer):
        self._viewer = viewer

        _grid_gl_opts = {
            GL_DEPTH_TEST: False,
            GL_BLEND: True,
            GL_ALPHA_TEST: False,
            GL_CULL_FACE: False,
            'glBlendFunc': (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
        }

        self.grid_major_item = gl.GLLinePlotItem(
            mode='lines', width=self.GRID_MAJOR_WIDTH, antialias=True,
        )
        self.grid_major_item.setGLOptions(_grid_gl_opts)
        self.grid_major_item.setDepthValue(-1000)
        viewer.addItem(self.grid_major_item)

        self.grid_minor_item = gl.GLLinePlotItem(
            mode='lines', width=self.GRID_MINOR_WIDTH_MIN, antialias=True,
        )
        self.grid_minor_item.setGLOptions(_grid_gl_opts)
        self.grid_minor_item.setDepthValue(-1000)
        viewer.addItem(self.grid_minor_item)

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
        """Create axis lines, arrows, labels and tick infrastructure."""
        self.axes_width = 3

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
            'X', [20.0, 0.0, 0.0], QColor(255, 0, 0, 255),
        )
        self.y_label = self._create_axis_label(
            'Y', [0.0, 20.0, 0.0], QColor(0, 255, 0, 255),
        )
        self.z_label = self._create_axis_label(
            'Z', [0.0, 0.0, 20.0], QColor(0, 0, 255, 255),
        )

        self.tick_line_item = gl.GLLinePlotItem(
            width=1.5, antialias=True, mode='lines',
        )
        self.tick_line_item.setGLOptions('translucent')
        self._viewer.addItem(self.tick_line_item)

        self.tick_label_pool = []
        tick_label_color = QColor(180, 180, 180, 200)
        for _ in range(self.TICK_LABEL_POOL_SIZE):
            lbl = gl.GLTextItem(
                pos=np.zeros(3, dtype=float), text='', color=tick_label_color,
            )
            lbl.setVisible(False)
            self._viewer.addItem(lbl)
            self.tick_label_pool.append(lbl)

    # ------------------------------------------------------------------
    # Static helpers
    # ------------------------------------------------------------------

    @staticmethod
    def compute_grid_spacings(view_range, target_minor_count=15):
        """Compute two-level grid spacings using a pure x5 level sequence.

        Returns (minor_spacing, major_spacing, phase_t).
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
    def build_grid_lines(spacing, half_extent, skip_multiple=0,
                         base_rgba=None, fade_radius=None):
        """Generate line-pair vertices with per-line edge fade on XOY (z=0).

        Returns (positions, colors) numpy arrays.
        """
        empty_pos = np.zeros((0, 3), dtype=np.float32)
        empty_col = np.zeros((0, 4), dtype=np.float32)
        if spacing <= 0 or half_extent <= 0 or base_rgba is None:
            return empty_pos, empty_col

        n = int(half_extent / spacing + 0.5)
        verts = []
        colors = []

        do_fade = fade_radius is not None and fade_radius < half_extent
        inv_fade_range = (
            1.0 / max(half_extent - fade_radius, 1e-9)
            if do_fade else 1.0
        )

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
                    [-half_extent, coord, 0.0], [-fade_radius, coord, 0.0],
                ])
                colors.extend([c_zero, c_max])
                verts.extend([
                    [-fade_radius, coord, 0.0], [fade_radius, coord, 0.0],
                ])
                colors.extend([c_max, c_max])
                verts.extend([
                    [fade_radius, coord, 0.0], [half_extent, coord, 0.0],
                ])
                colors.extend([c_max, c_zero])

                verts.extend([
                    [coord, -half_extent, 0.0], [coord, -fade_radius, 0.0],
                ])
                colors.extend([c_zero, c_max])
                verts.extend([
                    [coord, -fade_radius, 0.0], [coord, fade_radius, 0.0],
                ])
                colors.extend([c_max, c_max])
                verts.extend([
                    [coord, fade_radius, 0.0], [coord, half_extent, 0.0],
                ])
                colors.extend([c_max, c_zero])
            else:
                verts.extend([
                    [-half_extent, coord, 0.0], [half_extent, coord, 0.0],
                ])
                colors.extend([c_max, c_max])
                verts.extend([
                    [coord, -half_extent, 0.0], [coord, half_extent, 0.0],
                ])
                colors.extend([c_max, c_max])

        if not verts:
            return empty_pos, empty_col
        return (
            np.array(verts, dtype=np.float32),
            np.array(colors, dtype=np.float32),
        )

    # ------------------------------------------------------------------
    # Arrow billboard
    # ------------------------------------------------------------------

    def update_arrow_billboard(self, camera_params=None, pos_ext=None,
                               axis_length=None, scene_scale=None):
        """Recalculate billboard arrow triangles so they always face the camera."""
        if camera_params is None:
            camera_params = self._viewer.cameraParams()

        if pos_ext is None or axis_length is None:
            sc = scene_scale if scene_scale is not None else 1.0
            dist = camera_params['distance']
            axis_length = dist * self.AXIS_VISUAL_RATIO / sc
            pos_ext = axis_length

        arrow_len = axis_length * 0.05
        arrow_width = arrow_len * 0.4

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

        vertex_colors = np.array([
            [1, 0, 0, 1], [1, 0, 0, 1], [1, 0, 0, 1],
            [0, 1, 0, 1], [0, 1, 0, 1], [0, 1, 0, 1],
            [0, 0, 1, 1], [0, 0, 1, 1], [0, 0, 1, 1],
        ], dtype=np.float32)

        self.axis_arrows.setMeshData(
            vertexes=verts, faces=faces, vertexColors=vertex_colors,
        )

    # ------------------------------------------------------------------
    # Main update
    # ------------------------------------------------------------------

    def update(self, distance, scene_scale):
        """Refresh axes, grid, and ticks based on current camera state."""
        axis_length = distance * self.AXIS_VISUAL_RATIO / scene_scale
        neg_ext = -axis_length * 0.5
        pos_ext = axis_length
        fade_start = neg_ext * 0.5

        # --- Axis lines (with negative-half fade) ---
        self.x_axis.setData(
            pos=np.array([
                [neg_ext, 0, 0], [fade_start, 0, 0], [pos_ext, 0, 0],
            ], dtype=np.float32),
            color=np.array([
                [1, 0, 0, 0], [1, 0, 0, 1], [1, 0, 0, 1],
            ], dtype=np.float32),
            mode='line_strip',
        )
        self.y_axis.setData(
            pos=np.array([
                [0, neg_ext, 0], [0, fade_start, 0], [0, pos_ext, 0],
            ], dtype=np.float32),
            color=np.array([
                [0, 1, 0, 0], [0, 1, 0, 1], [0, 1, 0, 1],
            ], dtype=np.float32),
            mode='line_strip',
        )
        self.z_axis.setData(
            pos=np.array([
                [0, 0, neg_ext], [0, 0, fade_start], [0, 0, pos_ext],
            ], dtype=np.float32),
            color=np.array([
                [0, 0, 1, 0], [0, 0, 1, 1], [0, 0, 1, 1],
            ], dtype=np.float32),
            mode='line_strip',
        )

        # --- Axis arrows (billboard) ---
        cam_params = self._viewer.cameraParams()
        self.update_arrow_billboard(
            cam_params, pos_ext, axis_length, scene_scale,
        )

        # --- Axis name labels ---
        label_offset = pos_ext + axis_length * 0.05 * 1.5
        if self.x_label is not None:
            self.x_label.setData(
                pos=np.array([label_offset, 0, 0], dtype=np.float32),
            )
        if self.y_label is not None:
            self.y_label.setData(
                pos=np.array([0, label_offset, 0], dtype=np.float32),
            )
        if self.z_label is not None:
            self.z_label.setData(
                pos=np.array([0, 0, label_offset], dtype=np.float32),
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

        major_rgba = np.array(
            [0.0, 1.0, 1.0, self.GRID_MAJOR_ALPHA / 255.0], dtype=np.float32,
        )
        major_verts, major_colors = self.build_grid_lines(
            major_sp, half_extent, base_rgba=major_rgba,
            fade_radius=grid_fade_radius,
        )
        if len(major_verts) >= 2:
            self.grid_major_item.setData(
                pos=major_verts, color=major_colors, mode='lines',
            )
            self.grid_major_item.setVisible(True)
        else:
            self.grid_major_item.setVisible(False)

        if minor_alpha > 0:
            minor_rgba = np.array(
                [0.0, 1.0, 1.0, minor_alpha / 255.0], dtype=np.float32,
            )
            minor_verts, minor_colors = self.build_grid_lines(
                minor_sp, half_extent, skip_multiple=5, base_rgba=minor_rgba,
                fade_radius=grid_fade_radius,
            )
            if len(minor_verts) >= 2:
                self.grid_minor_item.setData(
                    pos=minor_verts, color=minor_colors, mode='lines',
                    width=minor_width,
                )
                self.grid_minor_item.setVisible(True)
            else:
                self.grid_minor_item.setVisible(False)
        else:
            self.grid_minor_item.setVisible(False)

        # --- Tick lines & labels (aligned to major grid) ---
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
                mode='lines',
            )
            self.tick_line_item.setVisible(True)
        else:
            self.tick_line_item.setVisible(False)
