#include "ui/common/SidePanel.h"
#include "ui/common/Styles.h"
#include "ui/common/WindowIcons.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>

// ============================================================
// SidePanel.cpp — 侧滑面板基类实现
// ============================================================

SidePanel::SidePanel(const QString& title, int width, QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidePanel");
    setFixedWidth(width);
    // 显式 resize 到父级当前高度，避免首次显示前父控件按 QWidget 默认
    // 640×480 计算滑入/滑出位移
    resize(width, parent ? parent->height() : 480);

    // 背景不透明渲染 + 阻止鼠标事件向父 Viewer3D 传递
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_NoMousePropagation, true);
    setAutoFillBackground(true);

    setStyleSheet(Styles::SIDE_PANEL_STYLE());
    buildSkeleton(title);
}

// ── 静态助手 ────────────────────────────────────────────────

QFrame* SidePanel::makeSeparator(QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setObjectName("configSep");
    f->setFrameShape(QFrame::NoFrame);
    return f;
}

QHBoxLayout* SidePanel::wrapIndent(QWidget* w, int leftIndent) {
    auto* h = new QHBoxLayout();
    h->setContentsMargins(leftIndent, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(w);
    return h;
}

// ── 骨架构建 ────────────────────────────────────────────────

void SidePanel::buildSkeleton(const QString& title)
{
    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(0, 0, 0, 0);
    m_outerLayout->setSpacing(0);

    // ── 内容承载层 ──
    // 把标题栏 / 分隔线 / 滚动区 / 动作栏统统放进 m_contentHost；这样
    // MainWindow 在做面板间切换时，可只对 m_contentHost 挂 QGraphicsOpacityEffect
    // 来做内容淡入淡出，而 SidePanel 自身（this）的深色背景始终保持不透明。
    // contentHost 自身 NoBackground，背景全部由 #sidePanel 的 QSS 提供。
    m_contentHost = new QWidget(this);
    m_contentHost->setObjectName("sidePanelContentHost");
    m_contentHost->setAutoFillBackground(false);
    // 显式给 contentHost 一个空 stylesheet 段，避免它命中 #sidePanel 的背景；
    // QSS 仍可以通过 #sidePanel xxx { ... } 的后代选择器作用到内部子控件上。
    m_contentHost->setStyleSheet("QWidget#sidePanelContentHost { background: transparent; }");

    m_bodyLayout = new QVBoxLayout(m_contentHost);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(0);

    m_outerLayout->addWidget(m_contentHost);

    // 标题栏
    auto* titleBar = new QHBoxLayout();
    titleBar->setContentsMargins(14, 10, 8, 10);
    titleBar->setSpacing(0);

    auto* titleLabel = new QLabel(title, m_contentHost);
    titleLabel->setObjectName("sectionTitle");
    titleBar->addWidget(titleLabel);
    titleBar->addStretch();

    // 复用 FramelessWindow 顶栏的 1px 像素叉号绘制，确保叉号风格统一
    auto* closeBtn = new QPushButton(m_contentHost);
    closeBtn->setObjectName("closeBtn");
    closeBtn->setFixedSize(24, 24);
    closeBtn->setIcon(WindowIcons::makeCloseIcon());
    closeBtn->setIconSize(QSize(16, 16));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &SidePanel::closeRequested);
    titleBar->addWidget(closeBtn);

    m_bodyLayout->addLayout(titleBar);
    m_bodyLayout->addWidget(makeSeparator(m_contentHost));

    // 滚动区域
    auto* scrollArea = new QScrollArea(m_contentHost);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* scrollContent = new QWidget();
    scrollContent->setObjectName("configScrollContent");
    scrollContent->setStyleSheet("QWidget#configScrollContent { background: transparent; }");

    m_contentLayout = new QVBoxLayout(scrollContent);
    m_contentLayout->setContentsMargins(14, 14, 14, 14);
    m_contentLayout->setSpacing(14);

    scrollArea->setWidget(scrollContent);
    m_bodyLayout->addWidget(scrollArea, 1);
}

// ── 动作栏（按需懒加载）─────────────────────────────────────

QHBoxLayout* SidePanel::actionLayout()
{
    if (m_actionLayout) return m_actionLayout;

    m_bodyLayout->addWidget(makeSeparator(m_contentHost));

    m_actionLayout = new QHBoxLayout();
    // 上下边距由原本的 10/10 收紧到 6/6，让动作栏整体更扁、按钮文字面积
    // 在条带高度中的占比提升（按钮高度由 Styles 中的 padding/min-height 控制）。
    m_actionLayout->setContentsMargins(14, 6, 14, 6);
    m_actionLayout->setSpacing(8);
    m_bodyLayout->addLayout(m_actionLayout);
    return m_actionLayout;
}
