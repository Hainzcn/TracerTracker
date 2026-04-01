import time
from collections import deque

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import QWidget

from src.ui.attitude_widget import AttitudeWidget


class SensorChartPanel(QWidget):
    # 每条曲线保留的历史采样点数量。
    HISTORY_LENGTH = 120
    # 趋势图刷新节流间隔，降低非关键图表的更新频率。
    UPDATE_INTERVAL_SEC = 0.2
    # 图表宽度与姿态立方体背景框保持一致。
    CHART_WIDTH = AttitudeWidget.cube_panel_width()
    # 单个分组图表高度，以及分组间垂直间距。
    SECTION_HEIGHT = 74
    SECTION_GAP = 8
    # 标题区域高度。
    HEADER_HEIGHT = 12
    # 分组边框的左右留白，以及图框内部上下留白。
    HORIZONTAL_PADDING = 6
    VERTICAL_PADDING = 6
    # 图框整体内缩，避免边框贴到控件边缘。
    FRAME_INSET = 1
    # 实际曲线绘制区域的额外安全边距，防止压边。
    CHART_INSET = 2
    # 曲线描边宽度。
    SERIES_PEN_WIDTH = 1

    _TITLE_COLOR = QColor(153, 153, 153)
    _GRID_COLOR = QColor(255, 255, 255, 24)
    _ZERO_LINE_COLOR = QColor(255, 255, 255, 46)
    _BORDER_COLOR = QColor(80, 80, 80, 150)
    _SECTION_BG = QColor(30, 30, 30, 150)

    _ACC_COLORS = (
        QColor("#ff6b6b"),
        QColor("#69db7c"),
        QColor("#4dabf7"),
    )
    _RPY_COLORS = (
        QColor("#ff6b6b"),
        QColor("#69db7c"),
        QColor("#4dabf7"),
    )
    _ALT_COLORS = (QColor("#fcc419"),)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setAttribute(Qt.WA_TransparentForMouseEvents)
        total_height = self.SECTION_HEIGHT * 3 + self.SECTION_GAP * 2
        self.setFixedSize(self.CHART_WIDTH, total_height)
        self.setVisible(False)

        self._acc_history = [deque(maxlen=self.HISTORY_LENGTH) for _ in range(3)]
        self._rpy_history = [deque(maxlen=self.HISTORY_LENGTH) for _ in range(3)]
        self._alt_history = [deque(maxlen=self.HISTORY_LENGTH)]
        self._ref_pressure = None
        self._pending_snapshot = None
        self._last_flush_time = 0.0

    def reset(self):
        for history_group in (
            self._acc_history,
            self._rpy_history,
            self._alt_history,
        ):
            for series in history_group:
                series.clear()
        self._ref_pressure = None
        self._pending_snapshot = None
        self._last_flush_time = 0.0
        self.update()

    def push_snapshot(self, acceleration=None, euler=None, pressure=None, altitude=None):
        self._pending_snapshot = {
            "acceleration": acceleration,
            "euler": euler,
            "pressure": pressure,
            "altitude": altitude,
        }
        now = time.monotonic()
        if now - self._last_flush_time < self.UPDATE_INTERVAL_SEC:
            return

        self._last_flush_time = now
        snapshot = self._pending_snapshot
        self._pending_snapshot = None

        if snapshot["acceleration"] is not None:
            self._append_values(self._acc_history, snapshot["acceleration"])
        if snapshot["euler"] is not None:
            self._append_values(self._rpy_history, snapshot["euler"])

        altitude_value = self._resolve_altitude(
            pressure=snapshot["pressure"],
            altitude=snapshot["altitude"],
        )
        if altitude_value is not None:
            self._append_values(self._alt_history, (altitude_value,))

        self.update()

    def _resolve_altitude(self, pressure=None, altitude=None):
        if altitude is not None and altitude != 0.0:
            return float(altitude)
        if pressure is not None and pressure > 0:
            if self._ref_pressure is None:
                self._ref_pressure = float(pressure)
            return 44330.0 * (
                1.0 - pow(float(pressure) / self._ref_pressure, 1.0 / 5.255)
            )
        return None

    def _append_values(self, history_group, values):
        for series, value in zip(history_group, values):
            series.append(float(value))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        sections = (
            ("ACC", self._acc_history, self._ACC_COLORS, True),
            ("RPY", self._rpy_history, self._RPY_COLORS, True),
            ("ALT", self._alt_history, self._ALT_COLORS, False),
        )

        top = 0
        for title, history, colors, centered in sections:
            rect = QRectF(0, top, self.width(), self.SECTION_HEIGHT)
            self._draw_section(
                painter,
                rect,
                title=title,
                history=history,
                colors=colors,
                centered=centered,
            )
            top += self.SECTION_HEIGHT + self.SECTION_GAP

        super().paintEvent(event)

    def _draw_section(self, painter, rect, title, history, colors, centered):
        title_rect = QRectF(
            rect.left(),
            rect.top(),
            rect.width(),
            self.HEADER_HEIGHT,
        )
        frame_rect = QRectF(
            rect.left() + self.HORIZONTAL_PADDING + self.FRAME_INSET,
            title_rect.bottom() + 3,
            rect.width() - 2 * self.HORIZONTAL_PADDING - 2 * self.FRAME_INSET,
            rect.height() - self.HEADER_HEIGHT - 3 - self.FRAME_INSET,
        )
        chart_rect = frame_rect.adjusted(
            self.CHART_INSET,
            self.VERTICAL_PADDING + self.CHART_INSET,
            -self.CHART_INSET,
            -(self.VERTICAL_PADDING + self.CHART_INSET),
        )

        painter.setPen(QPen(self._BORDER_COLOR, 1))
        painter.setBrush(self._SECTION_BG)
        painter.drawRect(frame_rect.adjusted(0.5, 0.5, -0.5, -0.5))

        self._draw_header(
            painter,
            title_rect,
            title=title,
        )
        self._draw_grid(painter, chart_rect, centered=centered)
        self._draw_series(
            painter,
            chart_rect,
            history=history,
            colors=colors,
            centered=centered,
        )

    def _draw_header(self, painter, rect, title):
        painter.save()
        painter.setFont(QFont("Consolas", 10))
        painter.setPen(self._TITLE_COLOR)
        painter.drawText(rect, Qt.AlignHCenter | Qt.AlignBottom, title)
        painter.restore()

    def _draw_grid(self, painter, rect, centered):
        painter.save()
        line_color = self._ZERO_LINE_COLOR if centered else self._GRID_COLOR
        y = rect.center().y()
        painter.setPen(QPen(line_color, 1))
        painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y))

        painter.restore()

    def _draw_series(self, painter, rect, history, colors, centered):
        series_data = [list(series) for series in history if series]
        if not series_data:
            return

        if centered:
            values = [value for series in series_data for value in series]
            max_abs = max((abs(value) for value in values), default=1.0)
            max_abs = max(max_abs, 1.0)

            def value_to_y(value):
                amplitude = rect.height() * 0.42
                return rect.center().y() - (value / max_abs) * amplitude
        else:
            values = [value for series in series_data for value in series]
            min_val = min(values)
            max_val = max(values)
            if max_val - min_val < 1e-6:
                min_val -= 0.5
                max_val += 0.5
            padding = max((max_val - min_val) * 0.08, 0.2)
            min_val -= padding
            max_val += padding

            def value_to_y(value):
                ratio = (value - min_val) / (max_val - min_val)
                return rect.bottom() - ratio * rect.height()

        for series, color in zip(history, colors):
            if not series:
                continue

            points = []
            values = list(series)
            for idx, value in enumerate(values):
                x = rect.left() + (rect.width() * idx / max(self.HISTORY_LENGTH - 1, 1))
                points.append(QPointF(x, value_to_y(value)))

            path = QPainterPath()
            path.moveTo(points[0])
            for point in points[1:]:
                path.lineTo(point)

            painter.setPen(QPen(color, self.SERIES_PEN_WIDTH))
            painter.setBrush(Qt.NoBrush)
            painter.drawPath(path)
