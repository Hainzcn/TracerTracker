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
        self.x_label = gl.GLTextItem(pos=np.array([20, 0, 0]), text='X', color=(1, 1, 1, 1))
        self.y_label = gl.GLTextItem(pos=np.array([0, 20, 0]), text='Y', color=(1, 1, 1, 1))
        self.z_label = gl.GLTextItem(pos=np.array([0, 0, 20]), text='Z', color=(1, 1, 1, 1))
        
        self.addItem(self.x_label)
        self.addItem(self.y_label)
        self.addItem(self.z_label)
        
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
        pos_x = np.array([[neg_ext, 0, 0], [pos_ext, 0, 0]])
        self.x_axis.setData(pos=pos_x, color=(1, 0, 0, 1))
        
        # Y Axis (Green)
        pos_y = np.array([[0, neg_ext, 0], [0, pos_ext, 0]])
        self.y_axis.setData(pos=pos_y, color=(0, 1, 0, 1))
        
        # Z Axis (Blue)
        pos_z = np.array([[0, 0, neg_ext], [0, 0, pos_ext]])
        self.z_axis.setData(pos=pos_z, color=(0, 0, 1, 1))
        
        # Update Labels Position
        # Offset slightly from the end
        label_offset = size * 1.05 
        self.x_label.setData(pos=np.array([label_offset, 0, 0]))
        self.y_label.setData(pos=np.array([0, label_offset, 0]))
        self.z_label.setData(pos=np.array([0, 0, label_offset]))
        
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
        pos = np.array([[x, y, z]])
        
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
        if any(c > 1.0 for c in color):
            color = tuple(c / 255.0 for c in color)
        self.point_colors[name] = tuple(color)
            
        if name in self.points:
            # Update existing point
            self.points[name].setData(pos=pos, color=color, size=size)
        else:
            # Create new point
            # pxMode=True means size is in pixels, False means world units
            sp = gl.GLScatterPlotItem(pos=pos, color=color, size=size, pxMode=True)
            sp.setGLOptions('translucent')
            self.addItem(sp)
            self.points[name] = sp
        
        current_time = time.perf_counter()
        current_pos = np.array([x, y, z], dtype=float)
        if name not in self.point_histories:
            self.point_histories[name] = [current_pos]
            self.point_speeds[name] = [0.0]
            self.point_times[name] = [current_time]
        else:
            last_pos = self.point_histories[name][-1]
            last_time = self.point_times[name][-1]
            dt = max(current_time - last_time, 1e-6)
            speed = float(np.linalg.norm(current_pos - last_pos) / dt)
            self.point_histories[name].append(current_pos)
            self.point_speeds[name].append(speed)
            self.point_times[name].append(current_time)
        
        if self.full_path_mode:
            self.refresh_full_path(name, color)
        if self.trail_mode:
            self.refresh_trail(name)

    def set_full_path_mode(self, enabled):
        self.full_path_mode = bool(enabled)
        if self.full_path_mode:
            for name in self.points.keys():
                path_color = self.point_colors.get(name, (1, 1, 1, 1))
                self.refresh_full_path(name, path_color)
        else:
            for item in self.path_items.values():
                item.setVisible(False)
        self.update()

    def set_trail_mode(self, enabled):
        self.trail_mode = bool(enabled)
        if self.trail_mode:
            for name in self.points.keys():
                self.refresh_trail(name)
        else:
            for item in self.trail_items.values():
                item.setVisible(False)
        self.update()

    def set_trail_length(self, length):
        self.trail_length = max(10, int(length))
        if self.trail_mode:
            for name in self.points.keys():
                self.refresh_trail(name)
            self.update()

    def refresh_full_path(self, name, color):
        history = self.point_histories.get(name, [])
        if len(history) < 2:
            return
        if name not in self.path_items:
            path_item = gl.GLLinePlotItem(mode='lines', width=2.0, antialias=True)
            path_item.setGLOptions('translucent')
            self.addItem(path_item)
            self.path_items[name] = path_item
        rgba = np.array(color, dtype=float)
        if rgba.shape[0] == 3:
            rgba = np.append(rgba, 1.0)
        rgba[3] = 0.8
        path_pos = np.array(history, dtype=float)
        segment_pos = np.repeat(path_pos, 2, axis=0)[1:-1]
        segment_color = np.tile(rgba, (len(segment_pos), 1))
        self.path_items[name].setData(pos=segment_pos, color=segment_color, mode='lines', width=2.0)
        self.path_items[name].setVisible(True)

    def refresh_trail(self, name):
        history = self.point_histories.get(name, [])
        speeds = self.point_speeds.get(name, [])
        if len(history) < 2:
            return
        if name not in self.trail_items:
            trail_item = gl.GLLinePlotItem(mode='lines', width=4.0, antialias=True)
            trail_item.setGLOptions('translucent')
            self.addItem(trail_item)
            self.trail_items[name] = trail_item
        start_idx = max(0, len(history) - self.trail_length)
        trail_pos = np.array(history[start_idx:], dtype=float)
        trail_speeds = np.array(speeds[start_idx:], dtype=float)
        if len(trail_pos) < 2:
            self.trail_items[name].setVisible(False)
            return
        speed_min = float(np.min(trail_speeds))
        speed_max = float(np.max(trail_speeds))
        denom = speed_max - speed_min
        if denom < 1e-6:
            norm = np.zeros_like(trail_speeds)
        else:
            norm = (trail_speeds - speed_min) / denom
        colors = np.zeros((len(trail_pos), 4), dtype=float)
        colors[:, 0] = norm
        colors[:, 1] = 1.0 - np.abs(norm - 0.5) * 1.5
        colors[:, 1] = np.clip(colors[:, 1], 0.0, 1.0)
        colors[:, 2] = 1.0 - norm
        colors[:, 3] = np.linspace(0.2, 1.0, len(trail_pos))
        segment_pos = np.repeat(trail_pos, 2, axis=0)[1:-1]
        segment_color = np.repeat(colors, 2, axis=0)[1:-1]
        self.trail_items[name].setData(pos=segment_pos, color=segment_color, mode='lines', width=4.0)
        self.trail_items[name].setVisible(True)
            
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
