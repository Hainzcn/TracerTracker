import math
from PySide6.QtWidgets import QWidget
from PySide6.QtCore import Qt, Signal, QPointF
from PySide6.QtGui import QPainter, QColor, QFont, QPen, QBrush


class ViewOrientationGizmo(QWidget):
    """
    Blender 风格的视角方向 Gizmo，显示在 3D 视图左上角。
    点击轴端点可快速切换到对应的标准视图。
    """

    view_selected = Signal(float, float)

    _SIZE = 90
    _AXIS_LENGTH = 0.65
    _BASE_RADIUS = 7
    _CENTER_RADIUS = 0
    _BG_RADIUS_RATIO = 0.85

    _AXES = [
        {
            'dir': (1, 0, 0),
            'color': QColor(240, 60, 80),
            'label': 'X',
            'preset_pos': (0, -90),
            'preset_neg': (0, 90),
        },
        {
            'dir': (0, 1, 0),
            'color': QColor(140, 200, 60),
            'label': 'Y',
            'preset_pos': (0, 180),
            'preset_neg': (0, 0),
        },
        {
            'dir': (0, 0, 1),
            'color': QColor(60, 140, 240),
            'label': 'Z',
            'preset_pos': (90, None),
            'preset_neg': (-90, None),
        },
    ]

    def __init__(self, viewer, parent=None):
        super().__init__(parent)
        self.viewer = viewer
        self.setFixedSize(self._SIZE, self._SIZE)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setCursor(Qt.ArrowCursor)

        self._elevation = 30.0
        self._azimuth = -135.0

        self._label_font = QFont("Arial", 10, QFont.Bold)
        self._hovered_key = None
        self._hovering_bg = False
        self._pressed_preset = None
        self._last_mouse_pos = None
        self.setMouseTracking(True)

    def update_orientation(self):
        params = self.viewer.cameraParams()
        self._elevation = params['elevation']
        self._azimuth = params['azimuth']
        self.update()

    def _project(self, dx, dy, dz):
        """Project a unit-length 3D direction to 2D gizmo coordinates.

        Returns (screen_x, screen_y, depth, scale).
        Applies the same rotation chain as Viewer3D.viewMatrix():
          R_z(azimuth)  then  R_x(elevation - 90)
        """
        e = math.radians(self._elevation)
        a = math.radians(self._azimuth)

        cos_a, sin_a = math.cos(a), math.sin(a)
        sin_e, cos_e = math.sin(e), math.cos(e)

        x1 = dx * cos_a - dy * sin_a
        y1 = dx * sin_a + dy * cos_a
        z1 = dz

        x2 = x1
        y2 = y1 * sin_e + z1 * cos_e
        z2 = -y1 * cos_e + z1 * sin_e

        half = self._SIZE / 2.0
        r = half * self._AXIS_LENGTH
        
        # True perspective scale
        # Assume camera is at distance 4.0 units away
        # z2 ranges from -1 to 1. Positive z2 is closer to camera.
        camera_dist = 6.0
        scale = camera_dist / (camera_dist - z2)
        
        return half + x2 * r * scale, half - y2 * r * scale, z2, scale

    def _build_endpoints(self):
        """Return a list of drawable endpoint dicts, sorted back-to-front."""
        endpoints = []

        for axis in self._AXES:
            dx, dy, dz = axis['dir']
            px, py, pd, scale_pos = self._project(dx, dy, dz)
            
            endpoints.append({
                'x': px, 'y': py, 'depth': pd,
                'color': axis['color'],
                'label': axis['label'],
                'radius': self._BASE_RADIUS * scale_pos,
                'positive': True,
                'key': f"+{axis['label'].lower()}",
                'preset': axis['preset_pos'],
                'line_from': (self._SIZE / 2, self._SIZE / 2),
                'scale': scale_pos,
            })
            nx, ny, nd, scale_neg = self._project(-dx, -dy, -dz)
            
            endpoints.append({
                'x': nx, 'y': ny, 'depth': nd,
                'color': axis['color'],
                'label': '',
                'radius': self._BASE_RADIUS * scale_neg,
                'positive': False,
                'key': f"-{axis['label'].lower()}",
                'preset': axis['preset_neg'],
                'line_from': (self._SIZE / 2, self._SIZE / 2),
                'scale': scale_neg,
            })
        for ep in endpoints:
            color = QColor(ep['color'])
            depth = ep['depth']
            if depth < 0:
                h, s, v, a = color.getHsv()
                factor = max(0.3, 1.0 + depth * 0.3)
                color.setHsv(h, int(s * factor), int(v * factor), a)
            ep['shaded_color'] = color

        endpoints.sort(key=lambda ep: ep['depth'])
        return endpoints

    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        cx = self._SIZE / 2.0
        cy = self._SIZE / 2.0
        
        # Draw circular background if hovering
        if self._hovering_bg:
            painter.setPen(Qt.NoPen)
            painter.setBrush(QBrush(QColor(128, 128, 128, 60)))
            bg_radius = cx * self._BG_RADIUS_RATIO
            painter.drawEllipse(QPointF(cx, cy), bg_radius, bg_radius)

        endpoints = self._build_endpoints()

        # Draw lines and spheres in a single back-to-front pass
        for ep in endpoints:
            color = QColor(ep['shaded_color'])

            is_hovered = (ep['key'] == self._hovered_key)
            if is_hovered:
                color = color.lighter(130)

            # Draw the connecting line first (only for positive axes, as requested)
            if ep['positive']:
                pen = QPen(color, 2.5 * ep['scale'])
                pen.setCapStyle(Qt.RoundCap)
                painter.setPen(pen)
                painter.drawLine(QPointF(cx, cy), QPointF(ep['x'], ep['y']))

            r = ep['radius']
            
            if ep['positive']:
                # Solid circle for positive axes
                painter.setPen(Qt.NoPen)
                painter.setBrush(QBrush(color))
                painter.drawEllipse(QPointF(ep['x'], ep['y']), r, r)

                if ep['label']:
                    # Scale font size slightly based on perspective
                    font = QFont(self._label_font)
                    base_size = font.pointSizeF()
                    if base_size < 0:
                        base_size = font.pointSize()
                        
                    font.setPointSizeF(max(1.0, base_size * ep['scale']))
                    painter.setFont(font)
                    
                    if is_hovered:
                        painter.setPen(QColor(255, 255, 255)) # White text when hovered
                    else:
                        painter.setPen(QColor(20, 20, 20)) # Dark text like Blender
                        
                    text_rect = painter.fontMetrics().boundingRect(ep['label'])
                    painter.drawText(
                        QPointF(ep['x'] - text_rect.width() / 2.0,
                                ep['y'] + text_rect.height() / 2.0 - 2),
                        ep['label'],
                    )
            else:
                # Outlined circle for negative axes
                pen = QPen(color, 2.0 * ep['scale'])
                painter.setPen(pen)
                if is_hovered:
                    painter.setBrush(QBrush(color.lighter(130)))
                else:
                    painter.setBrush(QBrush(QColor(40, 40, 40, 200))) # Dark semi-transparent inside
                painter.drawEllipse(QPointF(ep['x'], ep['y']), r, r)

        painter.end()

    def _hit_test(self, pos):
        """Return the endpoint key at *pos*, or None."""
        endpoints = self._build_endpoints()
        for ep in reversed(endpoints):
            dx = pos.x() - ep['x']
            dy = pos.y() - ep['y']
            # Use dynamic radius for hit testing to match perspective size
            hit_radius = ep['radius'] + 2
            if dx * dx + dy * dy <= hit_radius ** 2:
                return ep['key'], ep['preset']
        return None, None

    def mousePressEvent(self, ev):
        if ev.button() == Qt.LeftButton:
            pos = ev.position()
            cx = self._SIZE / 2.0
            cy = self._SIZE / 2.0
            dx = pos.x() - cx
            dy = pos.y() - cy
            
            # Check if click is inside the circular area
            bg_radius = (self._SIZE / 2.0) * self._BG_RADIUS_RATIO
            if dx * dx + dy * dy <= bg_radius ** 2:
                key, preset = self._hit_test(pos)
                self._pressed_preset = preset
                self._last_mouse_pos = pos
                ev.accept()
                return
        ev.ignore()

    def mouseMoveEvent(self, ev):
        pos = ev.position()
        cx = self._SIZE / 2.0
        cy = self._SIZE / 2.0
        dx = pos.x() - cx
        dy = pos.y() - cy
        
        bg_radius = (self._SIZE / 2.0) * self._BG_RADIUS_RATIO
        is_inside = (dx * dx + dy * dy <= bg_radius ** 2)
        needs_update = False
        
        if is_inside != self._hovering_bg:
            self._hovering_bg = is_inside
            needs_update = True

        if ev.buttons() == Qt.LeftButton and self._last_mouse_pos is not None:
            # Drag to rotate
            diff = pos - self._last_mouse_pos
            self.viewer.orbit(diff.x(), diff.y())
            self.viewer._update_arrow_billboard()
            self.viewer.camera_changed.emit()
            self.viewer.update()
            
            self._last_mouse_pos = pos
            self.update_orientation()
            
            # If dragging, we might have moved off the initial preset
            # We could clear it here if drag distance is significant, 
            # but standard behavior often just cancels click if dragged.
            # For simplicity, we'll let release event handle it (if dragged, release might still be inside, but we can clear preset to avoid snapping after drag)
            if diff.manhattanLength() > 2:
                self._pressed_preset = None
                
            ev.accept()
            return
            
        # Hover logic
        key, _ = self._hit_test(pos)
        if key != self._hovered_key:
            self._hovered_key = key
            self.setCursor(Qt.PointingHandCursor if key else Qt.ArrowCursor)
            needs_update = True
            
        if needs_update:
            self.update()
            
        ev.ignore()

    def leaveEvent(self, _ev):
        needs_update = False
        if self._hovered_key is not None:
            self._hovered_key = None
            self.setCursor(Qt.ArrowCursor)
            needs_update = True
            
        if self._hovering_bg:
            self._hovering_bg = False
            needs_update = True
            
        if needs_update:
            self.update()

    def mouseReleaseEvent(self, ev):
        if ev.button() == Qt.LeftButton and self._last_mouse_pos is not None:
            pos = ev.position()
            cx = self._SIZE / 2.0
            cy = self._SIZE / 2.0
            dx = pos.x() - cx
            dy = pos.y() - cy
            
            bg_radius = (self._SIZE / 2.0) * self._BG_RADIUS_RATIO
            is_inside = (dx * dx + dy * dy <= bg_radius ** 2)
            
            if is_inside and self._pressed_preset is not None:
                elev, azim = self._pressed_preset
                if azim is None:
                    azim = self._azimuth
                
                # Check if we are already in this view (or close enough)
                def is_close(a, b, tol=1.0):
                    diff = (a - b) % 360
                    if diff > 180: diff -= 360
                    if diff < -180: diff += 360
                    return abs(diff) < tol

                if is_close(self._elevation, elev) and is_close(self._azimuth, azim):
                    # We are already at the target view, so flip to the opposite
                    opposite_preset = None
                    for axis in self._AXES:
                        if axis['preset_pos'] == self._pressed_preset:
                            opposite_preset = axis['preset_neg']
                            break
                        elif axis['preset_neg'] == self._pressed_preset:
                            opposite_preset = axis['preset_pos']
                            break
                    
                    if opposite_preset:
                        elev, target_azim = opposite_preset
                        if target_azim is not None:
                            azim = target_azim

                self.view_selected.emit(float(elev), float(azim))
                
            self._pressed_preset = None
            self._last_mouse_pos = None
            ev.accept()
            return
            
        ev.ignore()

    def wheelEvent(self, ev):
        ev.ignore()
