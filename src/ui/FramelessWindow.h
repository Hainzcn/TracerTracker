#pragma once
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>

// ============================================================
// FramelessWindow — 自定义无边框窗口基类
//
// 隐藏 Windows 原生标题栏，提供：
//   - 顶栏右端最小化/最大化/关闭按钮（46×32px，Windows 原生尺寸）
//   - 标题栏区域拖动移动窗口
//   - 窗口边缘拖动调整大小
//   - 最大化时填充屏幕（无圆角、无描边）
//   - 正常状态 DWM 圆角 + 1px 描边
//
// 子类只需调用 setCentralContent(QWidget*) 设置中心内容，
// 并通过 toolBarLayout() 向顶栏左侧添加控件。
// ============================================================

class FramelessWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit FramelessWindow(QWidget* parent = nullptr);

    // 顶栏水平布局（左侧可用区域，右端已放置窗口控制按钮）
    QHBoxLayout* toolBarLayout() const { return m_toolBarLayout; }

    // 顶栏 QWidget（供 nativeEvent 命中检测使用）
    QWidget* toolBar() const { return m_toolBar; }

    // 窗口控制按钮区域（窗口坐标系）
    QRect winButtonsRect() const;

    // 中心内容区域的垂直布局（顶栏下方，用于添加 viewer / console / statusbar 等）
    QVBoxLayout* contentLayout() const { return m_contentLayout; }

protected:
    void showEvent(QShowEvent* ev)     override;
    void changeEvent(QEvent* ev)       override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void updateMaximizeButton();

    // 顶栏
    QWidget*     m_toolBar        = nullptr;
    QHBoxLayout* m_toolBarLayout  = nullptr;

    // 窗口控制按钮
    QWidget*     m_winBtnContainer = nullptr;
    QPushButton* m_minimizeBtn     = nullptr;
    QPushButton* m_maximizeBtn     = nullptr;
    QPushButton* m_closeBtn        = nullptr;

    // 内容区域
    QVBoxLayout* m_rootLayout    = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;

    bool m_nativeBorderSetup = false;

    static constexpr int TOOLBAR_HEIGHT = 32;
    static constexpr int BORDER_WIDTH   = 5;
    static constexpr int BTN_WIDTH      = 46;
    static constexpr int BTN_HEIGHT     = 32;
};
