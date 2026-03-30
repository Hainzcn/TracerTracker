"""
调试控制台组件：原始数据日志与调试信息显示。
"""

import time

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QSplitter, QPlainTextEdit, QPushButton, QSizePolicy,
)
from PySide6.QtCore import (
    Qt, QPropertyAnimation, QEasingCurve, QParallelAnimationGroup,
    QVariantAnimation, Signal, Property, QPointF, QTimer
)
from PySide6.QtGui import (
    QColor, QPainter, QPolygonF, QSyntaxHighlighter, QTextCharFormat,
    QFont,
)

from src.ui.styles import CONSOLE_STYLE, STYLE_FOLD_BTN_LEFT, STYLE_FOLD_BTN_RIGHT


class RotatingButton(QPushButton):
    """支持翻转动画的折叠按钮，通过矢量三角形实现方向切换。"""

    ANIM_DURATION = 180

    def __init__(self, base_char, parent=None):
        super().__init__("", parent)
        self._default_flip_scale = -1.0 if base_char == "◀" else 1.0
        self._flip_scale = self._default_flip_scale

        self._flip_anim = QPropertyAnimation(self, b"flipScale")
        self._flip_anim.setDuration(self.ANIM_DURATION)
        self._flip_anim.setEasingCurve(QEasingCurve.InOutCubic)

    def _get_flip_scale(self):
        return self._flip_scale

    def _set_flip_scale(self, value):
        self._flip_scale = value
        self.update()

    flipScale = Property(float, _get_flip_scale, _set_flip_scale)

    def animate_flip(self, collapsed):
        target = -self._default_flip_scale if collapsed else self._default_flip_scale
        self._flip_anim.stop()
        self._flip_anim.setStartValue(float(self._flip_scale))
        self._flip_anim.setEndValue(float(target))
        self._flip_anim.start()

    def reset_flip(self, collapsed=False):
        self._flip_anim.stop()
        self._flip_scale = -self._default_flip_scale if collapsed else self._default_flip_scale
        self.update()

    def enterEvent(self, event):
        super().enterEvent(event)
        self.update()

    def leaveEvent(self, event):
        super().leaveEvent(event)
        self.update()

    def paintEvent(self, event):
        super().paintEvent(event)
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        if not self.isEnabled():
            color = QColor("#555555")
        elif not self.underMouse():
            color = QColor(224, 224, 224, 0)
        else:
            color = QColor("#e0e0e0")
        triangle_width = min(10.0, max(6.0, self.width() * 0.24))
        triangle_height = triangle_width * 1.35
        left_x = -triangle_width / 3.0
        tip_x = triangle_width * 2.0 / 3.0
        half_h = triangle_height / 2.0
        triangle = QPolygonF([
            QPointF(left_x, -half_h),
            QPointF(left_x, half_h),
            QPointF(tip_x, 0.0),
        ])

        painter.setPen(Qt.NoPen)
        painter.setBrush(color)
        painter.translate(self.width() / 2, self.height() / 2)
        painter.scale(self._flip_scale, 1.0)
        painter.drawPolygon(triangle)
        painter.end()


class ConsoleHighlighter(QSyntaxHighlighter):
    """为纯文本控制台提供轻量级行高亮。"""

    def __init__(self, document, mode):
        super().__init__(document)
        self._mode = mode
        self._default_format = QTextCharFormat()
        self._default_format.setForeground(QColor("#d4d4d4"))
        self._timestamp_format = QTextCharFormat()
        self._timestamp_format.setForeground(QColor("#888888"))
        self._udp_format = QTextCharFormat()
        self._udp_format.setForeground(QColor("#4dabf7"))
        self._udp_format.setFontWeight(QFont.Bold)
        self._serial_format = QTextCharFormat()
        self._serial_format.setForeground(QColor("#69db7c"))
        self._serial_format.setFontWeight(QFont.Bold)
        self._warn_format = QTextCharFormat()
        self._warn_format.setForeground(QColor("#fcc419"))
        self._error_format = QTextCharFormat()
        self._error_format.setForeground(QColor("#ff6b6b"))

    def highlightBlock(self, text):
        self.setFormat(0, len(text), self._default_format)
        if self._mode == "raw":
            if len(text) >= 10 and text.startswith("[") and text[9] == "]":
                self.setFormat(0, 10, self._timestamp_format)
            if "[UDP]" in text:
                start = text.index("[UDP]")
                self.setFormat(start, 5, self._udp_format)
            elif "[SERIAL]" in text:
                start = text.index("[SERIAL]")
                self.setFormat(start, 8, self._serial_format)
            return

        lowered = text.lower()
        if "stationary detected" in lowered:
            self.setFormat(0, len(text), self._warn_format)
        elif "error" in lowered or "failed" in lowered:
            self.setFormat(0, len(text), self._error_format)


class DebugConsole(QWidget):
    """可切换显示的调试控制台，包含原始数据和调试日志两个面板。"""

    all_collapsed = Signal()

    EXPANDED_HEIGHT = 200
    ANIM_DURATION = 180
    DRAG_COLLAPSE_THRESHOLD = 50

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(0)
        self.setMaximumHeight(0)
        
        self.main_layout = QVBoxLayout(self)
        self.main_layout.setContentsMargins(0, 0, 0, 0)
        self.main_layout.setSpacing(0)
        
        self.splitter = QSplitter(Qt.Horizontal, self)
        self.main_layout.addWidget(self.splitter)

        self._anim_min = QPropertyAnimation(self, b"minimumHeight")
        self._anim_max = QPropertyAnimation(self, b"maximumHeight")
        self._anim_group = QParallelAnimationGroup()
        for anim in (self._anim_min, self._anim_max):
            anim.setDuration(self.ANIM_DURATION)
            anim.setEasingCurve(QEasingCurve.OutCubic)
            self._anim_group.addAnimation(anim)
        self._anim_group.finished.connect(self._on_anim_finished)
        self._target_visible = False
        self._render_suspend_count = 0
        self._pending_raw_logs = []
        self._pending_debug_logs = []
        self._interaction_suspend_count = 0
        self._log_flush_timer = QTimer(self)
        self._log_flush_timer.setSingleShot(True)
        self._log_flush_timer.setInterval(50)
        self._log_flush_timer.timeout.connect(self._flush_pending_logs)
        self._resize_restore_timer = QTimer(self)
        self._resize_restore_timer.setSingleShot(True)
        self._resize_restore_timer.setInterval(120)
        self._resize_restore_timer.timeout.connect(self._finish_interactive_resize)
        self._interactive_resize_active = False

        self.raw_data_console = QPlainTextEdit()
        self.raw_data_console.setReadOnly(True)
        self.raw_data_console.setUndoRedoEnabled(False)
        self.raw_data_console.setCenterOnScroll(False)
        self.raw_data_console.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.raw_data_console.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.raw_data_console.document().setMaximumBlockCount(500)
        self.raw_data_console.setPlaceholderText("原始数据日志...")
        self.raw_data_console.setStyleSheet(CONSOLE_STYLE)
        self.raw_data_highlighter = ConsoleHighlighter(
            self.raw_data_console.document(), "raw",
        )

        self.debug_info_console = QPlainTextEdit()
        self.debug_info_console.setReadOnly(True)
        self.debug_info_console.setUndoRedoEnabled(False)
        self.debug_info_console.setCenterOnScroll(False)
        self.debug_info_console.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.debug_info_console.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.debug_info_console.document().setMaximumBlockCount(500)
        self.debug_info_console.setPlaceholderText("调试信息与姿态处理日志...")
        self.debug_info_console.setStyleSheet(CONSOLE_STYLE)
        self.debug_info_highlighter = ConsoleHighlighter(
            self.debug_info_console.document(), "debug",
        )

        self.left_wrapper = QWidget()
        left_layout = QVBoxLayout(self.left_wrapper)
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.setSpacing(0)
        left_layout.addWidget(self.raw_data_console)

        self.right_wrapper = QWidget()
        right_layout = QVBoxLayout(self.right_wrapper)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(0)
        right_layout.addWidget(self.debug_info_console)

        self.splitter.addWidget(self.left_wrapper)
        self.splitter.addWidget(self.right_wrapper)
        self.splitter.setSizes([640, 640])

        self.btn_fold_left = RotatingButton("◀", self)
        self.btn_fold_left.setStyleSheet(STYLE_FOLD_BTN_LEFT)
        self.btn_fold_left.setProperty("collapsed", False)
        self.btn_fold_left.clicked.connect(self.toggle_left_panel)

        self.btn_fold_right = RotatingButton("▶", self)
        self.btn_fold_right.setStyleSheet(STYLE_FOLD_BTN_RIGHT)
        self.btn_fold_right.setProperty("collapsed", False)
        self.btn_fold_right.clicked.connect(self.toggle_right_panel)

        self.btn_fold_left.raise_()
        self.btn_fold_right.raise_()

        self.left_wrapper.setMinimumWidth(0)
        self.right_wrapper.setMinimumWidth(0)
        self.left_wrapper.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Expanding)
        self.right_wrapper.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Expanding)
        self.raw_data_console.setMinimumSize(0, 0)
        self.debug_info_console.setMinimumSize(0, 0)
        self.raw_data_console.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Expanding)
        self.debug_info_console.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Expanding)
        self.hide()

        # 折叠状态跟踪
        self.left_collapsed = False
        self.right_collapsed = False
        self.saved_sizes = [640, 640]

        self.splitter.setChildrenCollapsible(True)
        self.splitter.setCollapsible(0, True)
        self.splitter.setCollapsible(1, True)
        self.splitter.setHandleWidth(2)

        self._splitter_anim = QVariantAnimation(self)
        self._splitter_anim.setDuration(180)
        self._splitter_anim.setEasingCurve(QEasingCurve.OutCubic)
        self._splitter_anim.valueChanged.connect(self._on_splitter_anim_step)
        self._splitter_anim.finished.connect(self._on_splitter_anim_finished)
        self.splitter.splitterMoved.connect(self._on_splitter_moved)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.btn_fold_left.setGeometry(0, 0, 30, self.height())
        self.btn_fold_right.setGeometry(self.width() - 30, 0, 30, self.height())
        self.btn_fold_left.raise_()
        self.btn_fold_right.raise_()

    # ── Public API ───────────────────────────────────────────────────

    def toggle_visibility(self, state):
        """切换调试控制台的可见性（带滑动动画）。"""
        self._target_visible = bool(state)
        self._anim_group.stop()
        if self._target_visible:
            if not self.isVisible():
                self.setMinimumHeight(0)
                self.setMaximumHeight(0)
                self.show()
                current_h = 0
            else:
                current_h = self.height()
            self._restore_panel_layout()
            target_h = self.EXPANDED_HEIGHT
        else:
            current_h = self.height()
            target_h = 0
        self._suspend_interaction()
        self._suspend_console_rendering()
        for anim in (self._anim_min, self._anim_max):
            anim.setStartValue(current_h)
            anim.setEndValue(target_h)
        self._anim_group.start()

    def _on_anim_finished(self):
        """动画结束后处理隐藏与清空。"""
        if not self._target_visible:
            self.hide()
            self._pending_raw_logs.clear()
            self._pending_debug_logs.clear()
            self._log_flush_timer.stop()
            self.raw_data_console.clear()
            self.debug_info_console.clear()
        self._resume_console_rendering()
        self._resume_interaction()

    def _on_splitter_anim_step(self, value):
        if not hasattr(self, '_anim_start_sizes') or not hasattr(self, '_anim_end_sizes'):
            return
        s1 = int(self._anim_start_sizes[0] + (self._anim_end_sizes[0] - self._anim_start_sizes[0]) * value)
        s2 = int(self._anim_start_sizes[1] + (self._anim_end_sizes[1] - self._anim_start_sizes[1]) * value)
        self.splitter.setSizes([s1, s2])

    def _on_splitter_anim_finished(self):
        if hasattr(self, '_anim_end_sizes'):
            self.splitter.setSizes(self._anim_end_sizes)
        if self.left_collapsed:
            self.left_wrapper.hide()
        if self.right_collapsed:
            self.right_wrapper.hide()
        self._sync_splitter_handle()
        self._resume_console_rendering()
        self._resume_interaction()
        self._check_all_collapsed()

    def _start_splitter_anim(self, start_sizes, end_sizes):
        self._anim_start_sizes = start_sizes
        self._anim_end_sizes = end_sizes
        self._suspend_interaction()
        self._suspend_console_rendering()
        self._splitter_anim.setStartValue(0.0)
        self._splitter_anim.setEndValue(1.0)
        self._splitter_anim.start()

    def _sync_splitter_handle(self):
        self.splitter.setHandleWidth(0 if (self.left_collapsed or self.right_collapsed) else 2)

    def _set_button_collapsed_state(self, button, collapsed):
        button.reset_flip(collapsed)
        button.setProperty("collapsed", collapsed)
        button.style().unpolish(button)
        button.style().polish(button)

    def _restore_panel_layout(self):
        self._set_button_collapsed_state(self.btn_fold_left, self.left_collapsed)
        self._set_button_collapsed_state(self.btn_fold_right, self.right_collapsed)

        if self.left_collapsed:
            self.left_wrapper.hide()
            self.raw_data_console.setLineWrapMode(QPlainTextEdit.NoWrap)
        else:
            self.left_wrapper.show()
            self.raw_data_console.setLineWrapMode(QPlainTextEdit.WidgetWidth)

        if self.right_collapsed:
            self.right_wrapper.hide()
            self.debug_info_console.setLineWrapMode(QPlainTextEdit.NoWrap)
        else:
            self.right_wrapper.show()
            self.debug_info_console.setLineWrapMode(QPlainTextEdit.WidgetWidth)

        total_size = max(sum(self.saved_sizes), self.splitter.width(), 1)
        if self.left_collapsed and not self.right_collapsed:
            self.splitter.setSizes([0, total_size])
        elif self.right_collapsed and not self.left_collapsed:
            self.splitter.setSizes([total_size, 0])
        elif not self.left_collapsed and not self.right_collapsed:
            if sum(self.saved_sizes) > 0:
                self.splitter.setSizes(self.saved_sizes)
            else:
                self.splitter.setSizes([640, 640])
        self._sync_splitter_handle()

    def _suspend_interaction(self):
        self._interaction_suspend_count += 1
        if self._interaction_suspend_count > 1:
            return
        for widget in (self.splitter, self.btn_fold_left, self.btn_fold_right):
            widget.setAttribute(Qt.WA_TransparentForMouseEvents, True)

    def _resume_interaction(self):
        if self._interaction_suspend_count == 0:
            return
        self._interaction_suspend_count -= 1
        if self._interaction_suspend_count > 0:
            return
        for widget in (self.splitter, self.btn_fold_left, self.btn_fold_right):
            widget.setAttribute(Qt.WA_TransparentForMouseEvents, False)

    def _suspend_console_rendering(self):
        self._render_suspend_count += 1
        if self._render_suspend_count > 1:
            return
        for console in (self.raw_data_console, self.debug_info_console):
            console.setLineWrapMode(QPlainTextEdit.NoWrap)
            console.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)

    def _resume_console_rendering(self):
        if self._render_suspend_count == 0:
            return
        self._render_suspend_count -= 1
        if self._render_suspend_count > 0:
            return
        self.raw_data_console.setLineWrapMode(
            QPlainTextEdit.NoWrap if self.left_collapsed else QPlainTextEdit.WidgetWidth
        )
        self.debug_info_console.setLineWrapMode(
            QPlainTextEdit.NoWrap if self.right_collapsed else QPlainTextEdit.WidgetWidth
        )
        for console in (self.raw_data_console, self.debug_info_console):
            console.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
            console.viewport().update()
        self._flush_pending_logs()

    def _on_splitter_moved(self, _pos, _index):
        if self._splitter_anim.state() == QVariantAnimation.Running:
            return
        current_sizes = self.splitter.sizes()
        if not self.left_collapsed and not self.right_collapsed:
            collapse_threshold = max(
                self.DRAG_COLLAPSE_THRESHOLD,
                self.btn_fold_left.width(),
                self.btn_fold_right.width(),
            )
            if current_sizes[0] <= collapse_threshold:
                self._collapse_panel_from_drag("left")
                return
            if current_sizes[1] <= collapse_threshold:
                self._collapse_panel_from_drag("right")
                return
            self.saved_sizes = current_sizes[:]
        if not self._interactive_resize_active:
            self._interactive_resize_active = True
            self._suspend_console_rendering()
        self._resize_restore_timer.start()

    def _finish_interactive_resize(self):
        if not self._interactive_resize_active:
            return
        if self._splitter_anim.state() == QVariantAnimation.Running:
            self._resize_restore_timer.start()
            return
        self._interactive_resize_active = False
        self._resume_console_rendering()

    def _collapse_panel_from_drag(self, side):
        self._resize_restore_timer.stop()
        if self._interactive_resize_active:
            self._interactive_resize_active = False
            self._resume_console_rendering()

        current_sizes = self.splitter.sizes()
        total_size = current_sizes[0] + current_sizes[1]
        if side == "left":
            self.left_collapsed = True
            self.raw_data_console.setLineWrapMode(QPlainTextEdit.NoWrap)
            self.btn_fold_left.animate_flip(True)
            self.btn_fold_left.setProperty("collapsed", True)
            self.btn_fold_left.style().unpolish(self.btn_fold_left)
            self.btn_fold_left.style().polish(self.btn_fold_left)
            self._start_splitter_anim(current_sizes, [0, total_size])
        else:
            self.right_collapsed = True
            self.debug_info_console.setLineWrapMode(QPlainTextEdit.NoWrap)
            self.btn_fold_right.animate_flip(True)
            self.btn_fold_right.setProperty("collapsed", True)
            self.btn_fold_right.style().unpolish(self.btn_fold_right)
            self.btn_fold_right.style().polish(self.btn_fold_right)
            self._start_splitter_anim(current_sizes, [total_size, 0])

    def _schedule_log_flush(self):
        if not self._log_flush_timer.isActive():
            self._log_flush_timer.start()

    def _flush_pending_logs(self):
        if not self._target_visible:
            self._pending_raw_logs.clear()
            self._pending_debug_logs.clear()
            return
        if self._render_suspend_count > 0:
            if self._pending_raw_logs or self._pending_debug_logs:
                self._schedule_log_flush()
            return
        if self._pending_raw_logs and self.isVisible() and not self.left_collapsed:
            self.raw_data_console.appendPlainText("\n".join(self._pending_raw_logs))
        self._pending_raw_logs.clear()
        if self._pending_debug_logs and self.isVisible() and not self.right_collapsed:
            self.debug_info_console.appendPlainText("\n".join(self._pending_debug_logs))
        self._pending_debug_logs.clear()

    def _collapse_console_when_last_panel_closes(self, closing_left):
        if closing_left:
            self.left_collapsed = True
            self.raw_data_console.setLineWrapMode(QPlainTextEdit.NoWrap)
            self.btn_fold_left.reset_flip(True)
            self.btn_fold_left.setProperty("collapsed", True)
            self.btn_fold_left.style().unpolish(self.btn_fold_left)
            self.btn_fold_left.style().polish(self.btn_fold_left)
        else:
            self.right_collapsed = True
            self.debug_info_console.setLineWrapMode(QPlainTextEdit.NoWrap)
            self.btn_fold_right.reset_flip(True)
            self.btn_fold_right.setProperty("collapsed", True)
            self.btn_fold_right.style().unpolish(self.btn_fold_right)
            self.btn_fold_right.style().polish(self.btn_fold_right)
        self._sync_splitter_handle()
        self._check_all_collapsed()

    def toggle_left_panel(self):
        if self._splitter_anim.state() == QVariantAnimation.Running:
            return

        current_sizes = self.splitter.sizes()

        if not self.left_collapsed:
            if self.right_collapsed:
                self._collapse_console_when_last_panel_closes(closing_left=True)
                return
            self.saved_sizes[0] = current_sizes[0]
            self.left_collapsed = True
            self.btn_fold_left.animate_flip(True)
            self.btn_fold_left.setProperty("collapsed", True)
            self.btn_fold_left.style().unpolish(self.btn_fold_left)
            self.btn_fold_left.style().polish(self.btn_fold_left)
            self.raw_data_console.setLineWrapMode(QPlainTextEdit.NoWrap)
            self._start_splitter_anim(current_sizes, [0, current_sizes[0] + current_sizes[1]])
        else:
            self.left_collapsed = False
            self._sync_splitter_handle()
            self.btn_fold_left.animate_flip(False)
            self.btn_fold_left.setProperty("collapsed", False)
            self.btn_fold_left.style().unpolish(self.btn_fold_left)
            self.btn_fold_left.style().polish(self.btn_fold_left)
            self.left_wrapper.show()
            self.raw_data_console.setLineWrapMode(QPlainTextEdit.WidgetWidth)
            target_right = current_sizes[1] - self.saved_sizes[0]
            self._start_splitter_anim(current_sizes, [self.saved_sizes[0], max(0, target_right)])

    def toggle_right_panel(self):
        if self._splitter_anim.state() == QVariantAnimation.Running:
            return

        current_sizes = self.splitter.sizes()

        if not self.right_collapsed:
            if self.left_collapsed:
                self._collapse_console_when_last_panel_closes(closing_left=False)
                return
            self.saved_sizes[1] = current_sizes[1]
            self.right_collapsed = True
            self.btn_fold_right.animate_flip(True)
            self.btn_fold_right.setProperty("collapsed", True)
            self.btn_fold_right.style().unpolish(self.btn_fold_right)
            self.btn_fold_right.style().polish(self.btn_fold_right)
            self.debug_info_console.setLineWrapMode(QPlainTextEdit.NoWrap)
            self._start_splitter_anim(current_sizes, [current_sizes[0] + current_sizes[1], 0])
        else:
            self.right_collapsed = False
            self._sync_splitter_handle()
            self.btn_fold_right.animate_flip(False)
            self.btn_fold_right.setProperty("collapsed", False)
            self.btn_fold_right.style().unpolish(self.btn_fold_right)
            self.btn_fold_right.style().polish(self.btn_fold_right)
            self.right_wrapper.show()
            self.debug_info_console.setLineWrapMode(QPlainTextEdit.WidgetWidth)
            target_left = current_sizes[0] - self.saved_sizes[1]
            self._start_splitter_anim(current_sizes, [max(0, target_left), self.saved_sizes[1]])

    def _check_all_collapsed(self):
        if self.left_collapsed and self.right_collapsed:
            self.all_collapsed.emit()

    def on_raw_data_received(self, source, raw_text):
        """处理原始数据日志。"""
        if not self.isVisible() or not self._target_visible or self.left_collapsed:
            return

        timestamp = time.strftime("%H:%M:%S", time.localtime(time.time()))
        source_color = "#4dabf7" if source == "udp" else "#69db7c"

        line_text = f"[{timestamp}] [{source.upper()}] {raw_text}"
        self._pending_raw_logs.append(line_text)
        self._schedule_log_flush()

    def on_pose_log(self, message):
        """处理来自 PoseProcessor 的日志消息。"""
        if not self.isVisible() or not self._target_visible or self.right_collapsed:
            return
        self._pending_debug_logs.append(message)
        self._schedule_log_flush()
