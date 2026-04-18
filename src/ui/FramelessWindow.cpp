#include "FramelessWindow.h"
#include "Styles.h"
#include "WindowIcons.h"

#include <QShowEvent>
#include <QEvent>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <QApplication>
#include <QCursor>
#include <QIcon>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace {
#ifdef Q_OS_WIN
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmBorderColor = 34;
constexpr DWORD kDwmCornerDoNotRound = 1;
constexpr DWORD kDwmCornerRound = 2;
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;
#endif
}

// ============================================================
// FramelessWindow.cpp — 自定义无边框窗口实现
// ============================================================

FramelessWindow::FramelessWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint);

    // ── 根布局 ──
    auto* root = new QWidget(this);
    root->setObjectName("centralWidget");
    setCentralWidget(root);
    m_rootLayout = new QVBoxLayout(root);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    // ── 顶栏 ──
    m_toolBar = new QWidget(this);
    m_toolBar->setObjectName("topBar");
    m_toolBar->setFixedHeight(TOOLBAR_HEIGHT);
    m_toolBar->setStyleSheet(Styles::TOP_BAR_STYLE());

    m_toolBarLayout = new QHBoxLayout(m_toolBar);
    m_toolBarLayout->setContentsMargins(10, 0, 0, 0);
    m_toolBarLayout->setSpacing(8);

    // 左侧 stretch 使窗口控制按钮贴右
    m_toolBarLayout->addStretch();

    // ── 窗口控制按钮 ──
    m_winBtnContainer = new QWidget(m_toolBar);
    auto* winLayout = new QHBoxLayout(m_winBtnContainer);
    winLayout->setContentsMargins(0, 0, 0, 0);
    winLayout->setSpacing(0);

    m_minimizeBtn = new QPushButton(m_winBtnContainer);
    m_minimizeBtn->setStyleSheet(Styles::STYLE_WIN_BTN());
    m_minimizeBtn->setFixedSize(BTN_WIDTH, BTN_HEIGHT);
    m_minimizeBtn->setIcon(WindowIcons::makeMinimizeIcon());
    m_minimizeBtn->setIconSize(QSize(16, 16));
    connect(m_minimizeBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
    winLayout->addWidget(m_minimizeBtn);

    m_maximizeBtn = new QPushButton(m_winBtnContainer);
    m_maximizeBtn->setStyleSheet(Styles::STYLE_WIN_BTN());
    m_maximizeBtn->setFixedSize(BTN_WIDTH, BTN_HEIGHT);
    m_maximizeBtn->setIcon(WindowIcons::makeMaximizeIcon());
    m_maximizeBtn->setIconSize(QSize(16, 16));
    connect(m_maximizeBtn, &QPushButton::clicked, this, &FramelessWindow::toggleMaximizeRestore);
    winLayout->addWidget(m_maximizeBtn);

    m_closeBtn = new QPushButton(m_winBtnContainer);
    m_closeBtn->setStyleSheet(Styles::STYLE_WIN_CLOSE_BTN());
    m_closeBtn->setFixedSize(BTN_WIDTH, BTN_HEIGHT);
    m_closeBtn->setIcon(WindowIcons::makeCloseIcon());
    m_closeBtn->setIconSize(QSize(16, 16));
    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);
    winLayout->addWidget(m_closeBtn);

    m_toolBarLayout->addWidget(m_winBtnContainer);

    // 顶栏底边线与窗口按钮同高时，子控件绘制会盖住 QSS 的 border-bottom；
    // 使用 1px 子控件置于最上层且不接收鼠标，保证整条分隔线连续（含悬停高亮）。
    m_topBarBottomLine = new QWidget(m_toolBar);
    m_topBarBottomLine->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_topBarBottomLine->setStyleSheet(
        QStringLiteral("background-color: %1; border: none;").arg(Styles::COLOR_BORDER_DARK));
    m_toolBar->installEventFilter(this);

    m_rootLayout->addWidget(m_toolBar);

    // ── 内容区域 ──
    m_contentLayout = new QVBoxLayout;
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    m_rootLayout->addLayout(m_contentLayout, 1);
}

QRect FramelessWindow::winButtonsRect() const {
    if (!m_winBtnContainer) return QRect();
    QPoint topLeft = m_winBtnContainer->mapTo(this, QPoint(0, 0));
    return QRect(topLeft, m_winBtnContainer->size());
}

void FramelessWindow::updateMaximizeButton() {
    const bool maximized =
#ifdef Q_OS_WIN
        isWindowActuallyMaximized(reinterpret_cast<HWND>(winId()));
#else
        isMaximized();
#endif
    m_maximizeBtn->setIcon(maximized ? WindowIcons::makeRestoreIcon() : WindowIcons::makeMaximizeIcon());
}

void FramelessWindow::updateWindowFrameState() {
    updateMaximizeButton();

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;

    if (isWindowActuallyMaximized(hwnd)) {
        DWORD cornerPref = kDwmCornerDoNotRound;
        DwmSetWindowAttribute(hwnd, kDwmWindowCornerPreference, &cornerPref, sizeof(cornerPref));
        DwmSetWindowAttribute(hwnd, kDwmBorderColor, &kDwmColorNone, sizeof(kDwmColorNone));
    } else {
        DWORD cornerPref = kDwmCornerRound;
        COLORREF borderColor = RGB(0x33, 0x33, 0x33);
        DwmSetWindowAttribute(hwnd, kDwmWindowCornerPreference, &cornerPref, sizeof(cornerPref));
        DwmSetWindowAttribute(hwnd, kDwmBorderColor, &borderColor, sizeof(borderColor));
    }
#endif
}

void FramelessWindow::toggleMaximizeRestore() {
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        ShowWindow(hwnd, isWindowActuallyMaximized(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        updateWindowFrameState();
        return;
    }
#endif

    isMaximized() ? showNormal() : showMaximized();
    updateWindowFrameState();
}

// ── 事件 ─────────────────────────────────────────────────────

void FramelessWindow::updateTopBarBottomLineGeometry() {
    if (!m_toolBar || !m_topBarBottomLine)
        return;
    const int w = std::max(1, m_toolBar->width());
    const int h = m_toolBar->height();
    m_topBarBottomLine->setGeometry(0, h - 1, w, 1);
    m_topBarBottomLine->raise();
}

bool FramelessWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_toolBar) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
            updateTopBarBottomLineGeometry();
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void FramelessWindow::showEvent(QShowEvent* ev) {
    QMainWindow::showEvent(ev);
    updateTopBarBottomLineGeometry();
#ifdef Q_OS_WIN
    if (!m_nativeBorderSetup) {
        m_nativeBorderSetup = true;
        HWND hwnd = reinterpret_cast<HWND>(winId());

        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        style |= WS_THICKFRAME | WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
        SetWindowLongW(hwnd, GWL_STYLE, style);

        COLORREF borderColor = RGB(0x33, 0x33, 0x33);
        DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/,
                              &borderColor, sizeof(borderColor));

        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    updateWindowFrameState();
#endif
}

void FramelessWindow::changeEvent(QEvent* ev) {
    QMainWindow::changeEvent(ev);
    if (ev->type() == QEvent::WindowStateChange)
        updateWindowFrameState();
}

#ifdef Q_OS_WIN

// 顶栏内需要接收鼠标交互的控件：对其若判为标题栏(HTCAPTION)，系统不会把点击交给 Qt。
static bool widgetBlocksCaptionDrag(const QWidget* w) {
    for (const QWidget* x = w; x; x = x->parentWidget()) {
        if (qobject_cast<const QAbstractButton*>(x))
            return true;
        if (qobject_cast<const QComboBox*>(x))
            return true;
        if (qobject_cast<const QAbstractSpinBox*>(x))
            return true;
        if (qobject_cast<const QLineEdit*>(x))
            return true;
        if (qobject_cast<const QSlider*>(x))
            return true;
    }
    return false;
}

bool FramelessWindow::isWindowActuallyMaximized(HWND hwnd) const {
    return hwnd && IsZoomed(hwnd);
}

bool FramelessWindow::isInTitleBarDragArea(const QPoint& screenPos) const {
    const QPoint localPos = mapFromGlobal(screenPos);
    const int tbH = m_toolBar ? m_toolBar->height() : TOOLBAR_HEIGHT;
    if (localPos.y() < 0 || localPos.y() >= tbH)
        return false;

    if (localPos.x() < 0 || localPos.x() >= width())
        return false;

    QWidget* child = childAt(localPos);
    // childAt 命中的是最深子控件：顶栏左侧为 ToolBar/标签/下拉框等，不是 m_toolBar 自身。
    // 须允许 m_toolBar 子树内的命中，仅靠 winButtonsRect 排除右上角系统按钮区。
    if (child && child != centralWidget()) {
        if (child != m_toolBar && (!m_toolBar || !m_toolBar->isAncestorOf(child)))
            return false;
    }

    const QRect btnRect = winButtonsRect();
    if (btnRect.isValid() && localPos.x() >= btnRect.left())
        return false;

    if (child && widgetBlocksCaptionDrag(child))
        return false;

    return true;
}

void FramelessWindow::beginRestoreDrag(HWND hwnd) {
    if (!hwnd)
        return;

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp))
        return;

    RECT maximizedRect{};
    if (!GetWindowRect(hwnd, &maximizedRect))
        return;

    int normalWidth = int(wp.rcNormalPosition.right - wp.rcNormalPosition.left);
    if (normalWidth <= 0) {
        normalWidth = width();
    }

    const int maximizedWidth = std::max(1, int(maximizedRect.right - maximizedRect.left));
    double widthRatio = double(m_maximizedDragStart.x() - maximizedRect.left) / double(maximizedWidth);
    if (widthRatio < 0.0)
        widthRatio = 0.0;
    else if (widthRatio > 1.0)
        widthRatio = 1.0;

    const int restoredX = m_maximizedDragStart.x() - int(normalWidth * widthRatio);
    const int dragOffsetY = int(m_maximizedDragStart.y() - maximizedRect.top);
    const int titleBarOffset = std::max(0, std::min(dragOffsetY,
                                                    m_toolBar ? m_toolBar->height() : TOOLBAR_HEIGHT));
    const int restoredY = m_maximizedDragStart.y() - titleBarOffset;

    ShowWindow(hwnd, SW_RESTORE);
    SetWindowPos(hwnd, nullptr, restoredX, restoredY, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    updateWindowFrameState();

    ReleaseCapture();
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
}

bool FramelessWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    if (eventType != "windows_generic_MSG")
        return QMainWindow::nativeEvent(eventType, message, result);

    auto* msg = static_cast<MSG*>(message);

    if (msg->message == WM_NCCALCSIZE) {
        if (msg->wParam == TRUE) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
            if (isWindowActuallyMaximized(msg->hwnd)) {
                HMONITOR mon = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(mon, &mi);
                params->rgrc[0] = mi.rcWork;
            }
            *result = 0;
            return true;
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    if (msg->message == WM_NCACTIVATE) {
        *result = TRUE;
        return true;
    }

    if (msg->message == 0x00AE /*WM_NCUAHDRAWCAPTION*/
        || msg->message == 0x00AF /*WM_NCUAHDRAWFRAME*/) {
        *result = 0;
        return true;
    }

    if (msg->message == WM_NCLBUTTONDOWN) {
        const QPoint screenPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        if (msg->wParam == HTCAPTION && isWindowActuallyMaximized(msg->hwnd) && isInTitleBarDragArea(screenPos)) {
            m_pendingMaximizedDrag = true;
            m_maximizedDragStart = screenPos;
            *result = 0;
            return true;
        }
    }

    if (msg->message == WM_NCLBUTTONDBLCLK) {
        const QPoint screenPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        if (msg->wParam == HTCAPTION && isInTitleBarDragArea(screenPos)) {
            m_pendingMaximizedDrag = false;
            toggleMaximizeRestore();
            *result = 0;
            return true;
        }
    }

    if (msg->message == WM_NCLBUTTONUP || msg->message == WM_LBUTTONUP || msg->message == WM_CAPTURECHANGED) {
        m_pendingMaximizedDrag = false;
    }

    if ((msg->message == WM_MOUSEMOVE || msg->message == WM_NCMOUSEMOVE) && m_pendingMaximizedDrag) {
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
            m_pendingMaximizedDrag = false;
        } else {
            const QPoint screenPos = QCursor::pos();
            const int dragDistance = QApplication::startDragDistance();
            if ((screenPos - m_maximizedDragStart).manhattanLength() >= dragDistance) {
                m_pendingMaximizedDrag = false;
                beginRestoreDrag(msg->hwnd);
                *result = 0;
                return true;
            }
        }
    }

    if (msg->message == WM_NCHITTEST) {
        RECT winRect;
        GetWindowRect(msg->hwnd, &winRect);
        int x = GET_X_LPARAM(msg->lParam) - winRect.left;
        int y = GET_Y_LPARAM(msg->lParam) - winRect.top;
        int w = winRect.right - winRect.left;
        int h = winRect.bottom - winRect.top;

        const bool maximized = isWindowActuallyMaximized(msg->hwnd);

        if (!maximized) {
            if (x < BORDER_WIDTH && y < BORDER_WIDTH)                   { *result = HTTOPLEFT;     return true; }
            if (x >= w - BORDER_WIDTH && y < BORDER_WIDTH)              { *result = HTTOPRIGHT;    return true; }
            if (x < BORDER_WIDTH && y >= h - BORDER_WIDTH)              { *result = HTBOTTOMLEFT;  return true; }
            if (x >= w - BORDER_WIDTH && y >= h - BORDER_WIDTH)         { *result = HTBOTTOMRIGHT; return true; }
            if (x < BORDER_WIDTH)                                        { *result = HTLEFT;        return true; }
            if (x >= w - BORDER_WIDTH)                                   { *result = HTRIGHT;       return true; }
            if (y < BORDER_WIDTH)                                        { *result = HTTOP;         return true; }
            if (y >= h - BORDER_WIDTH)                                   { *result = HTBOTTOM;      return true; }
        }

        const QPoint screenPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        if (isInTitleBarDragArea(screenPos)) {
            *result = HTCAPTION;
            return true;
        }

        *result = HTCLIENT;
        return true;
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
