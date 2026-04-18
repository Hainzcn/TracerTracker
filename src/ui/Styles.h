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

// ── 下拉框样式（可剥离复用）──────────────────────────────────

// 下拉框核心 QSS —— 传入 prefix 可将规则作用域化；空 prefix 作用于全局
// 用法示例：
//   STYLE_COMBO_QSS("")                            → 全局 QComboBox
//   STYLE_COMBO_QSS("QWidget#configPanel")         → 限定于面板内部
inline QString STYLE_COMBO_QSS(const QString& prefix = QString()) {
    QString css = R"(
@ QComboBox {
    color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background: #2b2d30;
    border: 1px solid #4d4d4d; border-radius: 6px;
    padding: 1px 20px 1px 8px; min-height: 14px;
}
@ QComboBox:hover { border-color: #666666; background: #333538; }
@ QComboBox:focus { border-color: #0e639c; background: #333538; }
@ QComboBox:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
@ QComboBox::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 20px; border: none; background: transparent;
}
@ QComboBox::down-arrow {
    image: url(:/svg/icons/ChevronDown.svg);
    width: 14px; height: 14px;
    margin-right: 6px;
}
@ QComboBox::down-arrow:on {
    image: url(:/svg/icons/ChevronUp.svg);
}
@ QComboBox QAbstractItemView {
    color: #e0e0e0; background-color: #2b2d30;
    selection-background-color: #2c3e50;
    font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    border: 1px solid #4d4d4d; border-radius: 6px;
    outline: none; margin: 0px; padding: 4px;
}
@ QComboBox QAbstractItemView::item {
    min-height: 24px; max-height: 24px;
    padding: 0px 8px; border-radius: 4px;
}
@ QComboBox QAbstractItemView::item:hover { background-color: #3a3d41; }
@ QComboBox QAbstractItemView::item:selected { background-color: #2c3e50; }
)";
    const QString scope = prefix.isEmpty() ? QString() : (prefix + " ");
    return css.replace("@ ", scope);
}

// 全局默认下拉框样式（保持现有 min-width: 120px 行为）
inline QString STYLE_COMBO() {
    return STYLE_COMBO_QSS() + "\nQComboBox { min-width: 120px; }\n";
}

// ── 数字输入框样式（可剥离复用，与下拉框等高同色）──────────────

// SpinBox 核心 QSS —— 同样支持 prefix 作用域化
inline QString STYLE_SPINBOX_QSS(const QString& prefix = QString()) {
    QString css = R"(
@ QSpinBox, @ QDoubleSpinBox {
    color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background: #2b2d30; border: 1px solid #4d4d4d;
    border-radius: 6px; padding: 1px 8px; min-height: 14px;
}
@ QSpinBox:hover, @ QDoubleSpinBox:hover { border-color: #666666; background: #333538; }
@ QSpinBox:focus, @ QDoubleSpinBox:focus { border-color: #0e639c; background: #333538; }
@ QSpinBox:disabled, @ QDoubleSpinBox:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
@ QSpinBox::up-button, @ QSpinBox::down-button,
@ QDoubleSpinBox::up-button, @ QDoubleSpinBox::down-button {
    width: 0; height: 0; border: none; background: transparent;
}
)";
    const QString scope = prefix.isEmpty() ? QString() : (prefix + " ");
    return css.replace("@ ", scope);
}

// 全局默认数值输入框样式
inline QString STYLE_SPINBOX() {
    return STYLE_SPINBOX_QSS();
}

// ── 按钮样式 ─────────────────────────────────────────────────

// 默认（空闲）状态按钮
inline QString STYLE_BTN_IDLE() {
    return R"(
QPushButton {
    color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background: #333333; border: 1px solid #4d4d4d;
    border-radius: 6px; padding: 2px 10px;
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
    border-radius: 6px; padding: 2px 10px;
}
QPushButton:hover { background-color: #388e3c; }
QPushButton:pressed { background-color: #1b5e20; }
)";
}

// 窗口控制按钮（最小化/最大化）
inline QString STYLE_WIN_BTN() {
    return R"(
QPushButton {
    color: #cccccc; background: #252526;
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
    color: #cccccc; background: #252526;
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

// ── 侧边栏样式 ─────────────────────────────────────────────────

inline QString SIDE_BAR_STYLE() {
    return R"(
QWidget#sideBar {
    background-color: #252526;
    border-right: 1px solid #333333;
}
)";
}

inline QString SIDE_BAR_BTN_STYLE() {
    return R"(
QPushButton {
    background-color: transparent;
    color: #cccccc;
    border: none;
    border-radius: 4px;
    font-size: 12px;
    font-family: 'Microsoft YaHei', sans-serif;
    padding: 8px 0px;
}
QPushButton:hover {
    background-color: #333333;
    color: #ffffff;
}
QPushButton:pressed {
    background-color: #1e1e1e;
}
QPushButton:checked {
    background-color: #3f3f46;
    color: #ffffff;
}
)";
}

// ── 顶栏/底栏样式 ─────────────────────────────────────────────

// 顶部工具栏背景（底边分隔线由 FramelessWindow 顶层的 1px 子控件绘制，避免被子控件盖住）
inline QString TOP_BAR_STYLE() {
    return "#topBar { background-color: #252526; }";
}

// 状态栏背景
// 使用 #statusBar 选择器限定边框只绘制在容器自身
inline QString STATUS_BAR_STYLE() {
    return R"(
#statusBar {
    background-color: #252526;
    border-top: 1px solid #333333;
}
#statusBar > * {
    background: transparent;
}
)";
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

// ── 协议配置侧滑面板样式 ────────────────────────────────────

inline QString CONFIG_PANEL_STYLE() {
    return R"(
QWidget#configPanel {
    background-color: #252526;
    border-right: 1px solid #333333;
}

/* ── 标签 ── */
QWidget#configPanel QLabel {
    color: #cccccc;
    font-size: 12px;
    font-family: 'Microsoft YaHei', sans-serif;
    border: none;
    background: transparent;
}
QWidget#configPanel QLabel#sectionTitle {
    color: #e0e0e0;
    font-size: 13px;
    font-weight: bold;
    padding-top: 2px;
    padding-bottom: 2px;
}
QWidget#configPanel QLabel#subLabel {
    color: #b8b8b8;
    font-size: 12px;
    padding-top: 2px;
    padding-bottom: 2px;
}
QWidget#configPanel QLabel#groupLabel {
    color: #c8c8c8;
    font-size: 12px;
    font-weight: bold;
    letter-spacing: 1px;
    padding-top: 6px;
    padding-bottom: 2px;
}
QWidget#configPanel QLabel#descLabel {
    color: #888888;
    font-size: 11px;
}
QWidget#configPanel QLabel#fieldListLabel {
    color: #9fa0a0;
    font-size: 11px;
    font-family: Consolas, monospace;
    background: #1e1e1e;
    border: 1px solid #333333;
    border-radius: 4px;
    padding: 6px 8px;
}
QWidget#configPanel QLabel#multSign {
    color: #777777;
    font-size: 11px;
    padding: 0px 2px;
}

/* ── 按钮 ── */
QWidget#configPanel QPushButton {
    color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background: #333333; border: 1px solid #4d4d4d;
    border-radius: 4px; padding: 4px 14px;
    min-height: 20px;
}
QWidget#configPanel QPushButton:hover { background-color: #404040; border-color: #666666; }
QWidget#configPanel QPushButton:pressed { background-color: #2a2a2a; border-color: #4d4d4d; }
QWidget#configPanel QPushButton#applyBtn {
    color: #ffffff; background-color: #2e7d32; border: 1px solid #4caf50;
}
QWidget#configPanel QPushButton#applyBtn:hover { background-color: #388e3c; }
QWidget#configPanel QPushButton#applyBtn:pressed { background-color: #1b5e20; }
QWidget#configPanel QPushButton#closeBtn {
    background: transparent; border: none;
    border-radius: 4px; padding: 0px;
    min-width: 0px; min-height: 0px;
}
QWidget#configPanel QPushButton#closeBtn:hover { background: #E81123; }
QWidget#configPanel QPushButton#closeBtn:pressed { background: #c50f1f; }

/* ── 滚动区与滚动条 ── */
QWidget#configPanel QScrollArea {
    background: transparent; border: none;
}
QWidget#configPanel QScrollBar:vertical {
    border: none; background: transparent; width: 8px; margin: 0;
}
QWidget#configPanel QScrollBar::handle:vertical {
    background: #3f3f3f; min-height: 24px; border-radius: 4px;
}
QWidget#configPanel QScrollBar::handle:vertical:hover { background: #555555; }
QWidget#configPanel QScrollBar::add-line:vertical,
QWidget#configPanel QScrollBar::sub-line:vertical { height: 0; border: none; background: transparent; }
QWidget#configPanel QScrollBar::add-page:vertical,
QWidget#configPanel QScrollBar::sub-page:vertical { background: transparent; }

/* ── 分隔线 ── */
QFrame#configSep { background-color: #333333; min-height: 1px; max-height: 1px; border: none; }
)"
    // 复用顶栏剥离出的下拉框/数值框样式，scope 到面板内部
    + STYLE_COMBO_QSS("QWidget#configPanel")
    + STYLE_SPINBOX_QSS("QWidget#configPanel")
    // 面板专属覆盖：用相同的 min/max-height 锁死内容区，确保下拉框与
    // 数值框在面板内严格等高（消除 QStyle 对二者 frame margin 的细微差异）
    + R"(
QWidget#configPanel QComboBox,
QWidget#configPanel QDoubleSpinBox,
QWidget#configPanel QSpinBox {
    min-height: 20px; max-height: 20px;
    padding-top: 1px; padding-bottom: 1px;
}
QWidget#configPanel QComboBox::drop-down { height: 16px; }
)";
}

} // namespace Styles
