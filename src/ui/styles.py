"""
TracerTracker 全局 QSS 样式常量。

所有 UI 组件的样式定义集中于此，便于统一维护主题。
"""

STYLE_LABEL = (
    "color: #cccccc; font-size: 12px;"
    " font-family: 'Microsoft YaHei', sans-serif; border: none;"
)

STYLE_COMBO = """
    QComboBox {
        color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
        background: #333333;
        border: 1px solid #4d4d4d; border-radius: 4px;
        padding: 2px 20px 2px 8px; min-width: 120px;
    }
    QComboBox:hover { border-color: #666666; background: #3a3a3a; }
    QComboBox:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
    QComboBox::drop-down {
        subcontrol-origin: padding;
        subcontrol-position: center right;
        width: 20px; border: none;
        background: transparent;
    }
    QComboBox::down-arrow {
        image: none;
        width: 0px; 
        height: 0px;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 5px solid #999999;
        margin-right: 6px;
        margin-top: 2px;
    }
    QComboBox::down-arrow:on {
        border-top: none;
        border-bottom: 5px solid #999999;
        margin-top: -2px;
    }
    QComboBox QAbstractItemView {
        color: #e0e0e0; background-color: #333333;
        selection-background-color: #094771;
        font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
        border: 1px solid #4d4d4d;
        border-top: none;
        border-bottom-left-radius: 4px;
        border-bottom-right-radius: 4px;
        border-top-left-radius: 0px;
        border-top-right-radius: 0px;
        outline: none;
        margin: 0px;
        padding: 0px;
    }
    QComboBox QAbstractItemView::item {
        min-height: 20px;
        padding: 0px 8px;
    }
    QComboBox QAbstractItemView::item:hover {
        background-color: #404040;
    }
    QComboBox QAbstractItemView::item:selected {
        background-color: #094771;
    }
    QComboBox QAbstractItemView::item:selected:hover {
        background-color: #094771;
    }
"""

STYLE_SPINBOX = """
    QSpinBox {
        color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
        background: #333333;
        border: 1px solid #4d4d4d; border-radius: 4px;
        padding: 2px 6px;
    }
    QSpinBox:hover { border-color: #666666; background: #3a3a3a; }
    QSpinBox:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
    QSpinBox::up-button, QSpinBox::down-button {
        width: 0; height: 0; border: none;
    }
"""

STYLE_BTN_IDLE = """
    QPushButton {
        color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
        background: #333333;
        border: 1px solid #4d4d4d; border-radius: 4px;
        padding: 2px 10px;
    }
    QPushButton:hover { background-color: #404040; border-color: #666666; }
    QPushButton:pressed { background-color: #2a2a2a; border-color: #4d4d4d; }
    QPushButton:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
"""

STYLE_BTN_ACTIVE = """
    QPushButton {
        color: #ffffff; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
        background-color: #2e7d32;
        border: 1px solid #4caf50; border-radius: 4px;
        padding: 2px 10px;
    }
    QPushButton:hover { background-color: #388e3c; }
    QPushButton:pressed { background-color: #1b5e20; }
"""

STYLE_CHECKBOX = """
    QCheckBox {
        color: #cccccc; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif; spacing: 6px;
    }
    QCheckBox:hover { color: #e0e0e0; }
    QCheckBox:disabled { color: #666666; }
    QCheckBox::indicator {
        width: 14px; height: 14px;
        border: 1px solid #666666; border-radius: 3px;
        background: #333333;
    }
    QCheckBox::indicator:hover { border-color: #888888; }
    QCheckBox::indicator:checked {
        background: #4caf50; border-color: #4caf50;
    }
    QCheckBox::indicator:disabled {
        border-color: #4d4d4d; background: #2a2a2a;
    }
    QCheckBox::indicator:checked:disabled {
        background: #555555; border-color: #555555;
    }
"""

STATUS_LABEL_STYLE = (
    "color: #999999; font-size: 12px;"
    " font-family: 'Microsoft YaHei', sans-serif; border: none;"
)

STATUS_LABEL_ACTIVE_STYLE = (
    "color: #e0e0e0; font-size: 12px;"
    " font-family: 'Microsoft YaHei', sans-serif; border: none;"
)

CONSOLE_STYLE = """
    QTextEdit, QPlainTextEdit {
        background-color: #1e1e1e;
        color: #d4d4d4;
        border: 1px solid #333333;
        font-family: 'Consolas', 'JetBrains Mono', monospace;
        font-size: 12px;
        padding: 4px;
    }
"""

STYLE_FOLD_BTN_LEFT = """
    QPushButton {
        color: transparent;
        background-color: transparent;
        border: none;
        border-radius: 0px;
        font-weight: bold;
        font-family: 'Microsoft YaHei', sans-serif;
    }
    QPushButton:hover {
        color: #e0e0e0;
        background-color: rgba(90, 90, 90, 180);
    }
"""

STYLE_FOLD_BTN_RIGHT = """
    QPushButton {
        color: transparent;
        background-color: transparent;
        border: none;
        border-radius: 0px;
        font-weight: bold;
        font-family: 'Microsoft YaHei', sans-serif;
    }
    QPushButton:hover {
        color: #e0e0e0;
        background-color: rgba(90, 90, 90, 180);
    }
"""

MAIN_WINDOW_STYLE = """
    QMainWindow {
        background-color: #1e1e1e;
        font-family: 'Microsoft YaHei', sans-serif;
    }
    QSplitter::handle {
        background-color: #333333;
    }
    QSplitter::handle:horizontal {
        width: 2px;
    }
    QSplitter::handle:vertical {
        height: 2px;
    }
    QScrollBar:vertical {
        border: none;
        background: #1e1e1e;
        width: 10px;
        margin: 0px 0px 0px 0px;
    }
    QScrollBar::handle:vertical {
        background: #424242;
        min-height: 20px;
        border-radius: 5px;
    }
    QScrollBar::handle:vertical:hover {
        background: #4f4f4f;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
        height: 0px;
    }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
        background: none;
    }
"""

STATUS_BAR_STYLE = "background-color: #252526; border-top: 1px solid #333333;"

TOP_BAR_STYLE = "background-color: #252526; border-bottom: 1px solid #333333;"
