#pragma once
#include <QString>

// ============================================================
// Styles.h — TracerTracker 全局 QSS 样式常量
// 深色主题，统一维护所有 UI 组件样式
// ============================================================

namespace Styles {

// ── 颜色常量 ─────────────────────────────────────────────────

// 窗口主背景色
constexpr const char* COLOR_BG_WINDOW   = "#1e1e1e";
// 顶栏/底栏背景色
constexpr const char* COLOR_BG_BAR      = "#252526";
// 控件背景色
constexpr const char* COLOR_BG_CONTROL  = "#333333";
// 主边框色
constexpr const char* COLOR_BORDER      = "#4d4d4d";
// 暗边框色
constexpr const char* COLOR_BORDER_DARK = "#333333";
// 主文字色
constexpr const char* COLOR_TEXT        = "#cccccc";
// 亮文字色
constexpr const char* COLOR_TEXT_BRIGHT = "#e0e0e0";
// 激活色（绿色）
constexpr const char* COLOR_ACTIVE      = "#4caf50";
// 激活暗色
constexpr const char* COLOR_ACTIVE_DARK = "#2e7d32";
// 关闭按钮悬停红色
constexpr const char* COLOR_CLOSE_HOVER = "#E81123";
// 3D 场景背景色
constexpr const char* COLOR_3D_BG       = "#121212";

// ── 通用字体 ──────────────────────────────────────────────────
// 所有 UI 文本统一使用微软雅黑或等宽字体
constexpr const char* FONT_UI   = "'Microsoft YaHei', sans-serif";
constexpr const char* FONT_MONO = "Consolas, monospace";

// ── 标签样式 ─────────────────────────────────────────────────

// 普通标签样式（灰色文字）
inline QString STYLE_LABEL() {
    return "color: #cccccc; font-size: 12px;"
           " font-family: 'Microsoft YaHei', sans-serif; border: none;";
}

// ── 下拉框样式 ────────────────────────────────────────────────

// 串口选择下拉框样式
inline QString STYLE_COMBO() {
    return R"(
QComboBox {
    color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background: #333333;
    border: 1px solid #4d4d4d; border-radius: 4px;
    padding: 2px 20px 2px 8px; min-width: 120px;
}
QComboBox:hover { border-color: #666666; background: #3a3a3a; }
QComboBox:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
QComboBox::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 20px; border: none; background: transparent;
}
QComboBox::down-arrow {
    image: none; width: 0px; height: 0px;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #999999;
    margin-right: 6px; margin-top: 2px;
}
QComboBox::down-arrow:on {
    border-top: none; border-bottom: 5px solid #999999; margin-top: -2px;
}
QComboBox QAbstractItemView {
    color: #e0e0e0; background-color: #333333;
    selection-background-color: #094771;
    font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    border: 1px solid #4d4d4d; border-top: none;
    border-bottom-left-radius: 4px; border-bottom-right-radius: 4px;
    outline: none; margin: 0px; padding: 0px;
}
QComboBox QAbstractItemView::item {
    min-height: 20px; padding: 0px 8px;
}
QComboBox QAbstractItemView::item:hover { background-color: #404040; }
QComboBox QAbstractItemView::item:selected { background-color: #094771; }
)";
}

// ── 数字输入框样式 ────────────────────────────────────────────

// QSpinBox 样式（无上下箭头）
inline QString STYLE_SPINBOX() {
    return R"(
QSpinBox {
    color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background: #333333; border: 1px solid #4d4d4d;
    border-radius: 4px; padding: 2px 6px;
}
QSpinBox:hover { border-color: #666666; background: #3a3a3a; }
QSpinBox:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; height: 0; border: none; }
)";
}

// ── 按钮样式 ─────────────────────────────────────────────────

// 默认（空闲）状态按钮
inline QString STYLE_BTN_IDLE() {
    return R"(
QPushButton {
    color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background: #333333; border: 1px solid #4d4d4d;
    border-radius: 4px; padding: 2px 10px;
}
QPushButton:hover { background-color: #404040; border-color: #666666; }
QPushButton:pressed { background-color: #2a2a2a; border-color: #4d4d4d; }
QPushButton:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
)";
}

// 激活（运行中）状态按钮（绿色）
inline QString STYLE_BTN_ACTIVE() {
    return R"(
QPushButton {
    color: #ffffff; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background-color: #2e7d32; border: 1px solid #4caf50;
    border-radius: 4px; padding: 2px 10px;
}
QPushButton:hover { background-color: #388e3c; }
QPushButton:pressed { background-color: #1b5e20; }
)";
}

// 窗口控制按钮（最小化/最大化）
inline QString STYLE_WIN_BTN() {
    return R"(
QPushButton {
    color: #cccccc; background: transparent;
    border: none; border-radius: 0px;
    font-size: 14px; padding: 0px;
    min-width: 46px; min-height: 32px; max-width: 46px; max-height: 32px;
}
QPushButton:hover { background-color: #333333; color: #ffffff; }
QPushButton:pressed { background-color: #2a2a2a; }
)";
}

// 关闭按钮（悬停时变红）
inline QString STYLE_WIN_CLOSE_BTN() {
    return R"(
QPushButton {
    color: #cccccc; background: transparent;
    border: none; border-radius: 0px;
    font-size: 14px; padding: 0px;
    min-width: 46px; min-height: 32px; max-width: 46px; max-height: 32px;
}
QPushButton:hover { background-color: #E81123; color: #ffffff; }
QPushButton:pressed { background-color: #c50f1f; color: #ffffff; }
)";
}

// ── 复选框样式 ────────────────────────────────────────────────

inline QString STYLE_CHECKBOX() {
    return R"(
QCheckBox {
    color: #cccccc; font-size: 12px;
    font-family: 'Microsoft YaHei', sans-serif; spacing: 6px;
}
QCheckBox:hover { color: #e0e0e0; }
QCheckBox:disabled { color: #666666; }
QCheckBox::indicator {
    width: 14px; height: 14px;
    border: 1px solid #666666; border-radius: 3px; background: #333333;
}
QCheckBox::indicator:hover { border-color: #888888; }
QCheckBox::indicator:checked { background: #4caf50; border-color: #4caf50; }
QCheckBox::indicator:disabled { border-color: #4d4d4d; background: #2a2a2a; }
QCheckBox::indicator:checked:disabled { background: #555555; border-color: #555555; }
)";
}

// ── 状态标签样式 ──────────────────────────────────────────────

// 空闲状态（灰色）
inline QString STATUS_LABEL_STYLE() {
    return "color: #999999; font-size: 12px;"
           " font-family: 'Microsoft YaHei', sans-serif; border: none;";
}

// 活跃状态（亮色）
inline QString STATUS_LABEL_ACTIVE_STYLE() {
    return "color: #e0e0e0; font-size: 12px;"
           " font-family: 'Microsoft YaHei', sans-serif; border: none;";
}

// ── 调试控制台文本框样式 ──────────────────────────────────────

inline QString CONSOLE_STYLE() {
    return R"(
QTextEdit, QPlainTextEdit {
    background-color: #1e1e1e; color: #d4d4d4;
    border: 1px solid #333333;
    font-family: 'Consolas', 'JetBrains Mono', monospace;
    font-size: 12px; padding: 4px;
}
)";
}

// ── 折叠按钮样式（透明背景） ──────────────────────────────────

inline QString STYLE_FOLD_BTN() {
    return R"(
QPushButton {
    color: transparent; background-color: transparent;
    border: none; border-radius: 0px;
    font-weight: bold; font-family: 'Microsoft YaHei', sans-serif;
}
QPushButton:hover {
    color: #e0e0e0; background-color: rgba(90, 90, 90, 180);
}
)";
}

// 左侧折叠按钮（贴靠左边缘）
inline QString STYLE_FOLD_BTN_LEFT()  { return STYLE_FOLD_BTN(); }
// 右侧折叠按钮（贴靠右边缘）
inline QString STYLE_FOLD_BTN_RIGHT() { return STYLE_FOLD_BTN(); }

// ── 主窗口整体样式 ────────────────────────────────────────────

inline QString MAIN_WINDOW_STYLE() {
    return R"(
QMainWindow, QWidget#centralWidget {
    background-color: #1e1e1e;
    font-family: 'Microsoft YaHei', sans-serif;
}
QSplitter::handle { background-color: #333333; }
QSplitter::handle:horizontal { width: 2px; }
QSplitter::handle:vertical   { height: 2px; }
QScrollBar:vertical {
    border: none; background: #1e1e1e; width: 10px; margin: 0;
}
QScrollBar::handle:vertical {
    background: #424242; min-height: 20px; border-radius: 5px;
}
QScrollBar::handle:vertical:hover { background: #4f4f4f; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
)";
}

// ── 顶栏/底栏样式 ─────────────────────────────────────────────

// 顶部工具栏背景
inline QString TOP_BAR_STYLE() {
    return "background-color: #252526; border-bottom: 1px solid #333333;";
}

// 状态栏背景
inline QString STATUS_BAR_STYLE() {
    return "background-color: #252526; border-top: 1px solid #333333;";
}

// ── 投影切换按钮样式 ──────────────────────────────────────────

inline QString STYLE_PROJECTION_BTN() {
    return R"(
QPushButton {
    color: #aaaaaa; background: transparent; border: none;
    border-radius: 0px; font-size: 11px;
    font-family: 'Microsoft YaHei', sans-serif; padding: 2px 8px;
}
QPushButton:hover { color: #cccccc; background: rgba(180,180,180,40); }
QPushButton:pressed { color: #dddddd; background: rgba(180,180,180,60); }
)";
}

} // namespace Styles
