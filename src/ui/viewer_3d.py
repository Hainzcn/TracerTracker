from PySide6.QtCore import Qt, QPoint, QTimer, QTime
from PySide6.QtGui import QVector3D, QColor, QMatrix4x4
import pyqtgraph.opengl as gl
import numpy as np
import json
import os
import time

class Config:
    def __init__(self, config_path='config.json'):
        self.config = {}
        if os.path.exists(config_path):
            with open(config_path, 'r') as f:
                self.config = json.load(f)

    def get(self, key, default=None):
        """Get a configuration value."""
        return self.config.get(key, default)
    
    def get_points_config(self):
        """Get the points configuration list."""
        return self.config.get('points', [])

class Viewer3D(gl.GLViewWidget):
    """
    A custom 3D viewer widget based on pyqtgraph.opengl.GLViewWidget.
    Implements specific mouse interactions:
    - Left Click & Drag: Rotate (Orbit)
    - Right Click & Drag: Pan (Move)
    - Middle Click: Reset View (Animated)
    - Scroll Wheel: Zoom
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        
        # Initial camera settings
        self.initial_state = {
            'distance': 40,
            'elevation': 30,
            'azimuth': -45,
            'center': QVector3D(0, 0, 0)
        }
        
        # Set up the camera and background
        self.setCameraPosition(
            distance=self.initial_state['distance'],
            elevation=self.initial_state['elevation'],
            azimuth=self.initial_state['azimuth']
        )
        self.setBackgroundColor('#121212')  # Dark high-end background
        
        # Add a grid
        self.grid = gl.GLGridItem()
        self.grid.setSize(x=20, y=20, z=20)
        self.grid.setSpacing(x=1, y=1, z=1)
        # Custom grid color for high-end look (cyan/blueish tint, very subtle)
        # RGBA: (0, 255, 255, 50)
        self.grid.setColor((0, 255, 255, 50)) 
        self.addItem(self.grid)
        
        # Add custom thickened and extended axes
        self.add_custom_axes()
        
        # Tracked points
        self.points = {} # Dict to store point items: {name: GLScatterPlotItem}
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
        
        # State for adaptive scaling
        self.first_point_rendered = False
        
        # Interaction state
        self.mousePos = QPoint()
        
        # Custom camera pan offset (screen space translation)
        self.pan_offset = QVector3D(0, 0, 0)
        
        # Axes size state
        self.current_axes_size = 20
        self.target_axes_size = 20
        
        # Enable keyboard focus
        self.setFocusPolicy(Qt.StrongFocus)
        
        # Long press state for middle click
        self.long_press_timer = QTimer(self)
        self.long_press_timer.setSingleShot(True)
        self.long_press_timer.timeout.connect(self.on_long_press_timeout)
        self.is_long_press = False
        
        # Animation state
        self.animation_timer = QTimer(self)
        self.animation_timer.timeout.connect(self.update_animation)
        self.animation_start_time = None
        self.animation_duration = 800  # ms
        self.start_state = {}
        self.target_state = {}
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
        print(f"[Viewer3D][{level}]{point_text}[{code}] {detail}")

    def _create_axis_label(self, text, pos, color):
        try:
            label = gl.GLTextItem(pos=np.asarray(pos, dtype=float), text=text, color=color)
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
        
    def add_custom_axes(self):
        """
        Add custom thickened axes that extend beyond the origin.
        """
        # Store initial parameters for later updates
        self.axes_width = 3
        
        # Create empty items, will be populated by update_axes_size
        self.x_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)
        self.y_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)
        self.z_axis = gl.GLLinePlotItem(width=self.axes_width, antialias=True)
        
        self.addItem(self.x_axis)
        self.addItem(self.y_axis)
        self.addItem(self.z_axis)
        
        # Add Text Labels
        label_color = QColor(240, 240, 240, 255)
        self.x_label = self._create_axis_label('X', [20.0, 0.0, 0.0], label_color)
        self.y_label = self._create_axis_label('Y', [0.0, 20.0, 0.0], label_color)
        self.z_label = self._create_axis_label('Z', [0.0, 0.0, 20.0], label_color)
        
        # Initial size
        self.update_axes_size(20)

    def update_axes_size(self, size):
        """
        Update the size of the axes and grid to match the scale.
        """
        # Ensure minimum size
        size = max(size, 20)
        self.current_axes_size = size
        
        # Negative extension (origin back-shoot)
        neg_ext = -size * 0.5
        pos_ext = size
        
        # X Axis (Red)
        pos_x = np.array([[neg_ext, 0, 0], [pos_ext, 0, 0]], dtype=np.float32)
        self.x_axis.setData(pos=pos_x, color=(1, 0, 0, 1))
        
        # Y Axis (Green)
        pos_y = np.array([[0, neg_ext, 0], [0, pos_ext, 0]], dtype=np.float32)
        self.y_axis.setData(pos=pos_y, color=(0, 1, 0, 1))
        
        # Z Axis (Blue)
        pos_z = np.array([[0, 0, neg_ext], [0, 0, pos_ext]], dtype=np.float32)
        self.z_axis.setData(pos=pos_z, color=(0, 0, 1, 1))
        
        # Update Labels Position
        # Offset slightly from the end
        label_offset = size * 1.05 
        if self.x_label is not None:
            self.x_label.setData(pos=np.array([label_offset, 0, 0], dtype=np.float32))
        if self.y_label is not None:
            self.y_label.setData(pos=np.array([0, label_offset, 0], dtype=np.float32))
        if self.z_label is not None:
            self.z_label.setData(pos=np.array([0, 0, label_offset], dtype=np.float32))
        
        # Update grid size
        if hasattr(self, 'grid'):
            # Adjust spacing to avoid too many lines
            # Target ~20 lines per dimension
            spacing = max(1, size / 20)
            self.grid.setSize(x=size*2, y=size*2, z=size*2) # Grid covers neg and pos
            self.grid.setSpacing(x=spacing, y=spacing, z=spacing)
        
    def update_point(self, name, x, y, z, color=(1, 0, 0, 1), size=10):
        """
        Update or create a tracked point in the 3D view.
        """
        current_pos = np.array([x, y, z], dtype=float)
        if not np.isfinite(current_pos).all():
            self._render_debug("POINT_INVALID_POSITION", f"收到非法坐标: {current_pos.tolist()}", name, "ERROR")
            return
        pos = np.array([current_pos], dtype=np.float32)
        
        # Adaptive scaling for the first point
        if not self.first_point_rendered:
            dist = np.sqrt(x**2 + y**2 + z**2)
            cam_dist = self.cameraParams()['distance']
            
            # If point is significantly far (e.g. outside current view comfort zone), adapt view
            # Standard comfort zone for distance=40 is around 20 units radius
            if dist > cam_dist * 0.4:
                # Scale out to fit point comfortably
                # Factor 2.5 gives good context with origin
                new_dist = dist * 2.5
                
                # Apply adaptive scaling and rotation (reset to optimal view)
                self.setCameraPosition(distance=new_dist, elevation=30, azimuth=45)
                
            self.first_point_rendered = True
        
        # Convert color from list [r, g, b, a] (0-255) to tuple (0-1) if needed
        # Or if passed as (0-1), use as is.
        # Assuming input is tuple/list of floats 0-1 or 0-255.
        # GLLinePlotItem uses 0-1. GLScatterPlotItem uses 0-1.
        
        # Normalize color if values > 1
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
            # Update existing point
            self.points[name].setData(pos=pos, color=color_tuple, size=size)
        else:
            # Create new point
            # pxMode=True means size is in pixels, False means world units
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
            return
        self.trail_mode = target_mode
        self._render_debug("MODE_TRAIL", f"速度尾迹模式={'开启' if self.trail_mode else '关闭'}，当前点数量={len(self.points)}")
        if self.trail_mode:
            for name in self.points.keys():
                self.refresh_trail(name)
        else:
            for item in self.trail_items.values():
                self._hide_trail_item(item)
        self.update()

    def set_trail_length(self, length):
        self.trail_length = max(10, int(length))
        self._render_debug("TRAIL_LENGTH_UPDATE", f"尾迹长度已更新为 {self.trail_length}")
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
        """
        Handle mouse press events.
        """
        self.setFocus() # Ensure widget has focus for key events
        self.mousePos = ev.pos()
        
        # Middle button for reset animation
        if ev.button() == Qt.MouseButton.MiddleButton:
            # Start timer for long press (1 second)
            self.is_long_press = False
            self.long_press_timer.start(1000)
            ev.accept()
            return
            
        # Accept the event if it's left or right button
        if ev.button() == Qt.MouseButton.LeftButton or ev.button() == Qt.MouseButton.RightButton:
            # Stop animation if user interacts
            if self.animation_timer.isActive():
                self.animation_timer.stop()
            ev.accept()
        else:
            super().mousePressEvent(ev)

    def mouseReleaseEvent(self, ev):
        """
        Handle mouse release events.
        """
        if ev.button() == Qt.MouseButton.MiddleButton:
            # If timer is still active, it's a short press
            if self.long_press_timer.isActive():
                self.long_press_timer.stop()
                if not self.is_long_press:
                    # Short press: Partial Reset (Keep zoom/scale)
                    self.start_reset_animation(full_reset=False)
            
            # Reset flag
            self.is_long_press = False
            ev.accept()
        else:
            super().mouseReleaseEvent(ev)

    def on_long_press_timeout(self):
        """
        Called when middle button is held for 1 second.
        Triggers full reset.
        """
        self.is_long_press = True
        # Full Reset (Reset zoom/scale to defaults)
        self.start_reset_animation(full_reset=True)

    def keyPressEvent(self, ev):
        """
        Handle key press events.
        """
        if ev.key() == Qt.Key_R:
            self.auto_fit_view()
            ev.accept()
        else:
            super().keyPressEvent(ev)

    def auto_fit_view(self):
        """
        Automatically adjust the view to fit all points elegantly.
        - Calculates bounding box of all points
        - Centers the view on origin (resets pan)
        - Adjusts zoom distance to encompass all points
        """
        if not self.points:
            return

        # Find maximum distance from origin
        max_dist = 0
        for item in self.points.values():
            # item.pos is a numpy array (N, 3)
            pos_data = item.pos
            if pos_data is None or len(pos_data) == 0:
                continue
                
            # Calculate distance for each point (though usually 1 per item here)
            # We use linalg.norm on the pos array
            # If pos_data has multiple points, we want max of norms
            dists = np.linalg.norm(pos_data, axis=1)
            if len(dists) > 0:
                current_max = np.max(dists)
                if current_max > max_dist:
                    max_dist = current_max
        
        # Determine target distance
        # Default if no points or points at origin
        if max_dist < 1.0:
            target_dist = self.initial_state['distance']
        else:
            # Factor 2.5 provides a comfortable margin
            target_dist = max_dist * 2.5
            
        # Update axes and grid size to match the scale
        # Use animation by setting target
        self.target_axes_size = target_dist / 2
            
        # Get current camera state
        current_cam = self.cameraParams()
        
        # Setup animation
        self.start_state = {
            'distance': current_cam['distance'],
            'elevation': current_cam['elevation'],
            'azimuth': current_cam['azimuth'],
            'pan_x': self.pan_offset.x(),
            'pan_y': self.pan_offset.y(),
            'axes_size': self.current_axes_size
        }
        
        self.target_state = {
            'distance': target_dist,
            'elevation': current_cam['elevation'], # Maintain current rotation
            'azimuth': current_cam['azimuth'],     # Maintain current rotation
            'pan_x': 0,                            # Reset pan to center origin
            'pan_y': 0,
            'axes_size': self.target_axes_size
        }
        
        self.animation_start_time = QTime.currentTime()
        self.animation_timer.start(16) # ~60 FPS

    def mouseMoveEvent(self, ev):
        """
        Handle mouse move events for rotation and panning.
        """
        diff = ev.pos() - self.mousePos
        self.mousePos = ev.pos()

        if ev.buttons() == Qt.MouseButton.LeftButton:
            # Rotate (Orbit)
            self.orbit(diff.x(), diff.y())
            
        elif ev.buttons() == Qt.MouseButton.RightButton:
            # Custom Pan (Move Camera in Screen Space)
            # We want: Drag Down -> Content Moves Up
            # In our viewMatrix logic, we apply translation(pan_x, pan_y, 0) first.
            # Y is typically Up in OpenGL.
            # Mouse diff.y() > 0 means drag down.
            # If we want content to move up, we need pan_y to increase.
            # So pan_y += diff.y() * scale
            
            dist = self.cameraParams()['distance']
            # Scale factor - adjust based on distance for consistent feel
            scale = dist * 0.001
            
            self.pan_offset.setX(self.pan_offset.x() + diff.x() * scale)
            self.pan_offset.setY(self.pan_offset.y() - diff.y() * scale) # Inverted Y drag direction: Drag Down -> Content Moves Up
            
            self.update()
            
        else:
            super().mouseMoveEvent(ev)

    def viewMatrix(self):
        """
        Override viewMatrix to include custom pan offset.
        This ensures rotation always happens around the center (0,0,0) 
        while allowing the view to be shifted (panned).
        """
        m = QMatrix4x4()
        
        # Apply custom screen-space pan (translation)
        # This shifts the viewport
        m.translate(self.pan_offset.x(), self.pan_offset.y(), 0)
        
        # Standard GLViewWidget view matrix logic
        # Translate by distance (camera zoom)
        m.translate(0, 0, -self.opts['distance'])
        
        # Rotate around center
        m.rotate(self.opts['elevation']-90, 1, 0, 0)
        m.rotate(self.opts['azimuth'], 0, 0, 1)
        
        # Translate to center (which we keep as 0,0,0 for rotation pivot)
        center = self.opts['center']
        m.translate(-center.x(), -center.y(), -center.z())
        
        return m

    def wheelEvent(self, ev):
        """
        Handle scroll wheel for zooming.
        """
        # Stop animation if user interacts
        if self.animation_timer.isActive():
            self.animation_timer.stop()
            
        # Calculate scroll amount
        delta = ev.angleDelta().y()
        
        # Zoom factor
        if delta > 0:
            factor = 0.9
        else:
            factor = 1.1
            
        # Apply zoom by changing camera distance
        cam_params = self.cameraParams()
        dist = cam_params['distance']
        self.setCameraPosition(distance=dist * factor)
        
        ev.accept()

    def start_reset_animation(self, full_reset=True):
        """
        Start the camera reset animation.
        
        Args:
            full_reset (bool): If True, resets zoom and axes scale to defaults.
                               If False, keeps current zoom and axes scale.
        """
        current_params = self.cameraParams()
        
        if full_reset:
            # Reset axes size to initial default
            self.target_axes_size = 20
            target_distance = self.initial_state['distance']
        else:
            # Keep current axes size and distance
            self.target_axes_size = self.current_axes_size
            target_distance = current_params['distance']
        
        # Calculate shortest path for azimuth
        current_azim = current_params['azimuth']
        target_azim = self.initial_state['azimuth']
        
        diff = target_azim - current_azim
        # Normalize diff to [-180, 180] to find shortest path
        diff = (diff + 180) % 360 - 180
        
        # The actual target value for interpolation might be outside 0-360 
        # to ensure smoothness from current value
        effective_target_azim = current_azim + diff
        
        # Capture start state
        self.start_state = {
            'distance': current_params['distance'],
            'elevation': current_params['elevation'],
            'azimuth': current_params['azimuth'],
            'pan_x': self.pan_offset.x(),
            'pan_y': self.pan_offset.y(),
            'axes_size': self.current_axes_size
        }
        
        # Set target state
        self.target_state = self.initial_state.copy()
        self.target_state['distance'] = target_distance # Use determined target distance
        self.target_state['azimuth'] = effective_target_azim # Use the calculated shortest path target
        self.target_state['pan_x'] = 0
        self.target_state['pan_y'] = 0
        self.target_state['axes_size'] = self.target_axes_size
        
        # Start animation timer
        self.animation_start_time = QTime.currentTime()
        self.animation_timer.start(16) # ~60 FPS

    def update_animation(self):
        """
        Update camera position for animation frame.
        """
        if not self.animation_start_time:
            return
            
        elapsed = self.animation_start_time.msecsTo(QTime.currentTime())
        progress = elapsed / self.animation_duration
        
        if progress >= 1.0:
            progress = 1.0
            self.animation_timer.stop()
            
        # Ease-out cubic function for smooth "elegant" feel
        # t = progress - 1
        # ease = t * t * t + 1
        # Or simple ease-out-quad
        ease = 1 - (1 - progress) * (1 - progress)
        
        # Interpolate values
        new_dist = self.start_state['distance'] + (self.target_state['distance'] - self.start_state['distance']) * ease
        new_elev = self.start_state['elevation'] + (self.target_state['elevation'] - self.start_state['elevation']) * ease
        new_azim = self.start_state['azimuth'] + (self.target_state['azimuth'] - self.start_state['azimuth']) * ease
        
        # Interpolate pan offset
        new_pan_x = self.start_state['pan_x'] + (self.target_state['pan_x'] - self.start_state['pan_x']) * ease
        new_pan_y = self.start_state['pan_y'] + (self.target_state['pan_y'] - self.start_state['pan_y']) * ease
        self.pan_offset.setX(new_pan_x)
        self.pan_offset.setY(new_pan_y)
        
        # Interpolate axes size
        if 'axes_size' in self.start_state and 'axes_size' in self.target_state:
            new_axes_size = self.start_state['axes_size'] + (self.target_state['axes_size'] - self.start_state['axes_size']) * ease
            self.update_axes_size(new_axes_size)
        
        # Update standard camera params
        self.setCameraPosition(
            distance=new_dist,
            elevation=new_elev,
            azimuth=new_azim
            # Center remains (0,0,0) so we don't need to interpolate it
        )
        self.update() # Ensure redraw for pan offset update
