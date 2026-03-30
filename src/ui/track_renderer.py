"""
3D 场景点追踪与轨迹渲染器。

负责管理追踪点的创建/更新、全路径历史渲染和速度彩色尾迹渲染。
"""

import logging
import time

import numpy as np
import pyqtgraph.opengl as gl
from PySide6.QtCore import Signal

logger = logging.getLogger(__name__)


class TrackRenderer:
    """管理 3D 场景中的追踪点、路径和速度尾迹。

    由 Viewer3D 持有，接收外部的点更新请求并维护对应的 GL 对象。
    """

    def __init__(self, viewer):
        self._viewer = viewer

        self.points = {}
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
        self.max_history_length = 20000

        self.first_point_rendered = False

    # ------------------------------------------------------------------
    # Debug helper (delegates to viewer)
    # ------------------------------------------------------------------

    def _render_debug(self, code, detail, name=None, level="INFO",
                      verbose=False):
        if not self._viewer.render_debug_enabled:
            return
        if verbose and not self._viewer.render_debug_verbose_point_updates:
            return
        point_text = f"[{name}]" if name is not None else "[GLOBAL]"
        msg = f"[Viewer3D][{level}]{point_text}[{code}] {detail}"
        self._viewer.log_message.emit(msg)

    # ------------------------------------------------------------------
    # Static helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _build_line_segments(vertices, colors=None):
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
            return (
                None, None,
                f"颜色维度无效: pts_shape={pts.shape}, "
                f"colors_shape={getattr(cols, 'shape', None)}",
            )
        valid_mask = (
            np.isfinite(pts).all(axis=1) & np.isfinite(cols).all(axis=1)
        )
        pts = pts[valid_mask]
        cols = cols[valid_mask]
        if len(pts) < 2:
            return None, None, f"过滤后有效顶点不足: valid_points={len(pts)}"
        segment_pos = np.repeat(pts, 2, axis=0)[1:-1]
        segment_color = np.repeat(cols, 2, axis=0)[1:-1]
        return segment_pos, segment_color, None

    @staticmethod
    def _normalize_speed_for_trail(speeds):
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

    @staticmethod
    def _speed_to_comet_rgb(speed_norm):
        n = np.clip(speed_norm, 0.0, 1.0)
        r = 1.0 - 0.95 * n
        g = 0.1 + 0.9 * n
        b = 0.06 + 0.14 * (1.0 - np.abs(n - 0.5) * 2.0)
        rgb = np.stack([r, g, b], axis=1).astype(np.float32)
        return np.clip(rgb, 0.0, 1.0)

    @staticmethod
    def _downsample_path(history):
        """对路径历史进行分级下采样压缩。

        最新 2000 个点完整保留。其余以 2000 为一组，
        时间越久压缩程度越高（每组取等间距采样）。
        """
        n = len(history)
        if n <= 2000:
            return np.array(history, dtype=np.float32)

        recent = history[-2000:]
        older = history[:-2000]

        segments = []
        i = len(older)
        level = 1
        while i > 0:
            start = max(0, i - 2000)
            chunk = older[start:i]
            step = min(2 ** level, len(chunk))
            sampled = chunk[::step]
            segments.append(sampled)
            i = start
            level += 1

        segments.reverse()
        parts = segments + [recent]
        return np.array(
            [p for seg in parts for p in seg], dtype=np.float32,
        )

    @staticmethod
    def _hide_trail_item(item):
        if item is None:
            return
        if isinstance(item, dict):
            for layer_item in item.get('core', []):
                layer_item.setVisible(False)
            for layer_item in item.get('glow', []):
                layer_item.setVisible(False)
            return
        item.setVisible(False)

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def update_point(self, name, x, y, z, color=(1, 0, 0, 1), size=10):
        """更新或创建 3D 视图中的追踪点。"""
        current_pos = np.array([x, y, z], dtype=float)
        if not np.isfinite(current_pos).all():
            self._render_debug(
                "POINT_INVALID_POSITION",
                f"收到非法坐标: {current_pos.tolist()}", name, "ERROR",
            )
            return
        pos = np.array([current_pos], dtype=np.float32)

        # 首个点的自适应缩放
        if not self.first_point_rendered:
            dist = np.sqrt(x**2 + y**2 + z**2)
            cam_dist = self._viewer.cameraParams()['distance']

            if dist > cam_dist * 0.4:
                new_dist = dist * 2.5
                self._viewer.setCameraPosition(
                    distance=new_dist, elevation=45, azimuth=45,
                )
                self._viewer.scene_scale = self._viewer.initial_state['scene_scale']
                self._viewer._target_scene_scale = self._viewer.scene_scale
                self._viewer.update_coordinate_system()

            self.first_point_rendered = True

        # 颜色归一化：统一转为 0-1
        color_arr = np.asarray(color, dtype=float).flatten()
        if color_arr.size == 0:
            self._render_debug(
                "POINT_EMPTY_COLOR",
                "颜色为空，已回退到默认红色", name, "WARN",
            )
            color_arr = np.array([1.0, 0.0, 0.0, 1.0], dtype=float)
        if color_arr.size == 1:
            color_arr = np.array([color_arr[0], 0.0, 0.0, 1.0], dtype=float)
        if color_arr.size == 2:
            color_arr = np.array(
                [color_arr[0], color_arr[1], 0.0, 1.0], dtype=float,
            )
        if color_arr.size == 3:
            color_arr = np.append(color_arr, 1.0)
        if np.nanmax(color_arr) > 1.0:
            color_arr = color_arr / 255.0
        color_arr = np.clip(color_arr[:4], 0.0, 1.0)
        color_tuple = tuple(float(c) for c in color_arr)
        self.point_colors[name] = color_tuple

        if name in self.points:
            self.points[name].setData(pos=pos, color=color_tuple, size=size)
        else:
            sp = gl.GLScatterPlotItem(
                pos=pos, color=color_tuple, size=size, pxMode=True,
            )
            sp.setGLOptions('translucent')
            self._viewer.addItem(sp)
            self.points[name] = sp

        current_time = time.perf_counter()
        if name not in self.point_histories:
            self.point_histories[name] = [current_pos]
            self.point_speeds[name] = [0.0]
            self.point_times[name] = [current_time]
            if self.full_path_mode or self.trail_mode:
                self._render_debug(
                    "POINT_HISTORY_INIT",
                    "已初始化轨迹历史，当前点数=1", name, verbose=True,
                )
        else:
            last_pos = self.point_histories[name][-1]
            last_time = self.point_times[name][-1]
            dt = max(current_time - last_time, 1e-6)
            speed = float(np.linalg.norm(current_pos - last_pos) / dt)
            self.point_histories[name].append(current_pos)
            self.point_speeds[name].append(speed)
            self.point_times[name].append(current_time)
            if len(self.point_histories[name]) > self.max_history_length:
                origin = self.point_histories[name][0]
                excess = (
                    len(self.point_histories[name]) - self.max_history_length
                )
                self.point_histories[name] = (
                    [origin] + self.point_histories[name][excess + 1:]
                )
                self.point_speeds[name] = (
                    [0.0] + self.point_speeds[name][excess + 1:]
                )
                self.point_times[name] = (
                    [self.point_times[name][0]]
                    + self.point_times[name][excess + 1:]
                )
            if self.full_path_mode or self.trail_mode:
                history_len = len(self.point_histories[name])
                self._render_debug(
                    "POINT_HISTORY_APPEND",
                    f"追加轨迹点，当前点数={history_len}，速度={speed:.6f}",
                    name, verbose=True,
                )

        if self.full_path_mode:
            self.refresh_full_path(name, color_tuple)
        if self.trail_mode:
            self.refresh_trail(name)

    def clear_all(self):
        """Remove all tracked points, paths, trails and associated data."""
        for item in self.points.values():
            self._viewer.removeItem(item)
        for item in self.path_items.values():
            self._viewer.removeItem(item)
        for item in self.trail_items.values():
            if isinstance(item, dict):
                for layer in item.get('core', []):
                    self._viewer.removeItem(layer)
                for layer in item.get('glow', []):
                    self._viewer.removeItem(layer)
            else:
                self._viewer.removeItem(item)
        self.points.clear()
        self.point_histories.clear()
        self.point_speeds.clear()
        self.point_times.clear()
        self.point_colors.clear()
        self.path_items.clear()
        self.trail_items.clear()
        self.first_point_rendered = False
        self._viewer.update()

    def set_full_path_mode(self, enabled):
        target_mode = bool(enabled)
        if target_mode == self.full_path_mode:
            return
        self.full_path_mode = target_mode
        self._render_debug(
            "MODE_FULL_PATH",
            f"全路径模式={'开启' if self.full_path_mode else '关闭'}，"
            f"当前点数量={len(self.points)}",
        )
        if self.full_path_mode:
            for name in self.points.keys():
                path_color = self.point_colors.get(name, (1, 1, 1, 1))
                self.refresh_full_path(name, path_color)
        else:
            for item in self.path_items.values():
                item.setVisible(False)
        self._viewer.update()

    def set_trail_mode(self, enabled):
        target_mode = bool(enabled)
        self.trail_mode = target_mode
        self._viewer.log_message.emit(
            f"[Viewer3D][INFO][GLOBAL][MODE_TRAIL] "
            f"速度尾迹模式={'开启' if self.trail_mode else '关闭'}，"
            f"当前点数量={len(self.points)}",
        )
        if self.trail_mode:
            for name in self.points.keys():
                self.refresh_trail(name)
        else:
            for item in self.trail_items.values():
                self._hide_trail_item(item)
        self._viewer.update()

    def set_trail_length(self, length):
        self.trail_length = max(10, int(length))
        self._viewer.log_message.emit(
            f"[Viewer3D][INFO][GLOBAL][TRAIL_LENGTH_UPDATE] "
            f"尾迹长度已更新为 {self.trail_length}",
        )
        if self.trail_mode:
            for name in self.points.keys():
                self.refresh_trail(name)
            self._viewer.update()

    def refresh_full_path(self, name, color):
        try:
            history = self.point_histories.get(name, [])
            if len(history) < 2:
                if name in self.path_items:
                    self.path_items[name].setVisible(False)
                return
            if name not in self.path_items:
                path_item = gl.GLLinePlotItem(
                    mode='lines', width=2.0, antialias=True,
                )
                path_item.setGLOptions('translucent')
                self._viewer.addItem(path_item)
                self.path_items[name] = path_item
            rgba = np.array(color, dtype=np.float32)
            if rgba.shape[0] == 3:
                rgba = np.append(rgba, 1.0)
            rgba[3] = 0.8
            path_pos = self._downsample_path(history)
            segment_pos, _, reason = self._build_line_segments(path_pos)
            if segment_pos is None:
                self.path_items[name].setVisible(False)
                return
            segment_color = np.tile(rgba, (len(segment_pos), 1))
            self.path_items[name].setData(
                pos=segment_pos, color=segment_color, mode='lines', width=2.0,
            )
            self.path_items[name].setVisible(True)
            self._viewer.update()
        except Exception:
            if name in self.path_items:
                self.path_items[name].setVisible(False)

    def refresh_trail(self, name):
        try:
            history = self.point_histories.get(name, [])
            speeds = self.point_speeds.get(name, [])
            if len(history) < 2:
                self._render_debug(
                    "TRAIL_SKIPPED_NOT_ENOUGH_POINTS",
                    f"轨迹点不足，history_len={len(history)}", name, "WARN",
                )
                if name in self.trail_items:
                    self._hide_trail_item(self.trail_items[name])
                return
            start_idx = max(0, len(history) - self.trail_length)
            trail_pos = np.array(history[start_idx:], dtype=np.float32)
            trail_speeds = np.array(speeds[start_idx:], dtype=np.float32)
            finite_mask = (
                np.isfinite(trail_pos).all(axis=1) & np.isfinite(trail_speeds)
            )
            filtered_out = int(len(trail_pos) - int(np.sum(finite_mask)))
            if filtered_out > 0:
                self._render_debug(
                    "TRAIL_FILTERED_NONFINITE",
                    f"已过滤非法样本={filtered_out}", name, "WARN",
                )
            trail_pos = trail_pos[finite_mask]
            trail_speeds = trail_speeds[finite_mask]
            if len(trail_pos) < 2:
                self._render_debug(
                    "TRAIL_SKIPPED_AFTER_FILTER",
                    f"过滤后点不足，valid_len={len(trail_pos)}", name, "WARN",
                )
                if name in self.trail_items:
                    self._hide_trail_item(self.trail_items[name])
                return
            norm_speeds, speed_low, speed_high = (
                self._normalize_speed_for_trail(trail_speeds)
            )
            if speed_high - speed_low < 1e-6:
                self._render_debug(
                    "TRAIL_SPEED_FLAT",
                    f"速度近似恒定，low={speed_low:.6f}, "
                    f"high={speed_high:.6f}",
                    name, "WARN",
                )
            colors = np.zeros((len(trail_pos), 4), dtype=np.float32)
            colors[:, :3] = self._speed_to_comet_rgb(norm_speeds)
            colors[:, 3] = np.linspace(
                0.12, 0.95, len(trail_pos), dtype=np.float32,
            )
            segment_pos, segment_color, reason = self._build_line_segments(
                trail_pos, colors,
            )
            if segment_pos is None or segment_color is None:
                self._render_debug(
                    "TRAIL_SEGMENT_BUILD_FAILED",
                    reason or "未知原因", name, "ERROR",
                )
                if name in self.trail_items:
                    self._hide_trail_item(self.trail_items[name])
                return
            trail_item = self.trail_items.get(name)
            if isinstance(trail_item, dict):
                self._hide_trail_item(trail_item)
                trail_item = None
            if trail_item is None:
                trail_item = gl.GLLinePlotItem(
                    mode='lines', width=4.0, antialias=True,
                )
                trail_item.setGLOptions('translucent')
                self._viewer.addItem(trail_item)
                self.trail_items[name] = trail_item
                self._render_debug(
                    "TRAIL_ITEM_CREATED", "已创建连续尾迹绘制对象", name,
                )
            speed_head = (
                float(np.mean(norm_speeds[-min(5, len(norm_speeds)):]))
                if len(norm_speeds) > 0 else 0.0
            )
            width_ratio = 0.25 + 0.75 * speed_head
            trail_width = (
                self.trail_width_min
                + (self.trail_width_max - self.trail_width_min) * width_ratio
            )
            trail_item.setData(
                pos=segment_pos, color=segment_color, mode='lines',
                width=trail_width,
            )
            trail_item.setVisible(True)
            self._render_debug(
                "TRAIL_RENDER_OK",
                f"连续尾迹渲染成功，segment_count={len(segment_pos)}，"
                f"trail_len={len(trail_pos)}，width={trail_width:.2f}，"
                f"speed_low={speed_low:.4f}，speed_high={speed_high:.4f}",
                name,
            )
            self._viewer.update()
        except Exception as exc:
            self._render_debug(
                "TRAIL_RENDER_EXCEPTION",
                f"{type(exc).__name__}: {exc}", name, "ERROR",
            )
            if name in self.trail_items:
                self._hide_trail_item(self.trail_items[name])
