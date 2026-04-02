#include "DebugConsole.h"
#include "Styles.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QPolygonF>
#include <QTextCursor>
#include <QApplication>
#include <QSizePolicy>
#include <QDateTime>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QEnterEvent>
#include <QStyle>

// ============================================================
// DebugConsole.cpp — 可折叠调试控制台实现
// ============================================================

// ── ConsoleHighlighter ────────────────────────────────────────

ConsoleHighlighter::ConsoleHighlighter(QTextDocument* doc, const QString& mode)
    : QSyntaxHighlighter(doc), m_mode(mode)
{
    m_defaultFmt.setForeground(QColor("#d4d4d4"));

    m_timestampFmt.setForeground(QColor("#888888"));

    m_udpFmt.setForeground(QColor("#4dabf7"));
    m_udpFmt.setFontWeight(QFont::Bold);

    m_serialFmt.setForeground(QColor("#69db7c"));
    m_serialFmt.setFontWeight(QFont::Bold);

    m_warnFmt.setForeground(QColor("#fcc419"));

    m_errorFmt.setForeground(QColor("#ff6b6b"));
}

// 按行应用语法高亮
void ConsoleHighlighter::highlightBlock(const QString& text) {
    setFormat(0, text.length(), m_defaultFmt);

    if (m_mode == "raw") {
        // [HH:MM:SS] 时间戳
        if (text.length() >= 10 && text.startsWith('[') && text[9] == ']')
            setFormat(0, 10, m_timestampFmt);
        int idx;
        if ((idx = text.indexOf("[UDP]")) >= 0)
            setFormat(idx, 5, m_udpFmt);
        else if ((idx = text.indexOf("[SERIAL]")) >= 0)
            setFormat(idx, 8, m_serialFmt);
        return;
    }
    // debug 模式
    QString lower = text.toLower();
    if (lower.contains("stationary detected"))
        setFormat(0, text.length(), m_warnFmt);
    else if (lower.contains("error") || lower.contains("failed"))
        setFormat(0, text.length(), m_errorFmt);
}

// ── RotatingButton ────────────────────────────────────────────

RotatingButton::RotatingButton(bool facesLeft, QWidget* parent)
    : QPushButton("", parent), m_facesLeft(facesLeft)
{
    // 面向左的按钮默认比例 -1（三角形朝左），右侧 +1（朝右）
    m_defaultFlipScale = facesLeft ? -1.0f : 1.0f;
    m_flipScale        = m_defaultFlipScale;

    m_flipAnim = new QPropertyAnimation(this, "flipScale", this);
    m_flipAnim->setDuration(ANIM_MS);
    m_flipAnim->setEasingCurve(QEasingCurve::InOutCubic);
}

void RotatingButton::setFlipScale(float v) {
    m_flipScale = v;
    update();
}

// 播放翻转动画切换折叠/展开方向
void RotatingButton::animateFlip(bool collapsed) {
    m_showDownArrow = false;
    float target = collapsed ? -m_defaultFlipScale : m_defaultFlipScale;
    m_flipAnim->stop();
    m_flipAnim->setStartValue(m_flipScale);
    m_flipAnim->setEndValue(target);
    m_flipAnim->start();
}

// 直接设置翻转状态（无动画）
void RotatingButton::resetFlip(bool collapsed) {
    m_flipAnim->stop();
    m_showDownArrow = false;
    m_flipScale = collapsed ? -m_defaultFlipScale : m_defaultFlipScale;
    update();
}

// 切换为向下箭头（两侧全折叠时）
void RotatingButton::setDownArrow(bool enabled) {
    m_flipAnim->stop();
    m_showDownArrow = enabled;
    if (!m_showDownArrow) m_flipScale = m_defaultFlipScale;
    update();
}

void RotatingButton::enterEvent(QEnterEvent* ev) {
    QPushButton::enterEvent(ev);
    update();
}

void RotatingButton::leaveEvent(QEvent* ev) {
    QPushButton::leaveEvent(ev);
    update();
}

// 绘制矢量三角形箭头
void RotatingButton::paintEvent(QPaintEvent* ev) {
    QPushButton::paintEvent(ev);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor color;
    if (!isEnabled())
        color = QColor("#555555");
    else if (!underMouse())
        color = QColor(224, 224, 224, 0);
    else
        color = QColor("#e0e0e0");

    float tw = std::min(10.0f, std::max(6.0f, width() * 0.24f));
    float th = tw * 1.35f;
    float leftX = -tw / 3.0f;
    float tipX  =  tw * 2.0f / 3.0f;
    float halfH =  th / 2.0f;

    QPolygonF triangle;
    triangle << QPointF(leftX, -halfH)
             << QPointF(leftX,  halfH)
             << QPointF(tipX,   0.0f);

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.translate(width() / 2.0, height() / 2.0);
    if (m_showDownArrow)
        painter.rotate(90.0);
    else
        painter.scale(m_flipScale, 1.0f);
    painter.drawPolygon(triangle);
}

// ── DebugConsole ──────────────────────────────────────────────

DebugConsole::DebugConsole(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(0);
    setMaximumHeight(0);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── QSplitter ──
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(2);
    m_splitter->setChildrenCollapsible(true);
    m_splitter->setCollapsible(0, true);
    m_splitter->setCollapsible(1, true);
    mainLayout->addWidget(m_splitter);

    // ── 左栏：原始/解析日志 ──
    m_rawConsole = new QPlainTextEdit;
    m_rawConsole->setReadOnly(true);
    m_rawConsole->setUndoRedoEnabled(false);
    m_rawConsole->setCenterOnScroll(false);
    m_rawConsole->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rawConsole->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_rawConsole->document()->setMaximumBlockCount(MAX_LOG_BLOCKS);
    m_rawConsole->setPlaceholderText("原始报文日志...");
    m_rawConsole->setStyleSheet(Styles::CONSOLE_STYLE());
    m_rawConsole->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_rawConsole->setMinimumSize(0, 0);
    new ConsoleHighlighter(m_rawConsole->document(), "raw");
    m_rawConsole->installEventFilter(this);

    m_leftWrapper = new QWidget;
    auto* lLayout = new QVBoxLayout(m_leftWrapper);
    lLayout->setContentsMargins(0, 0, 0, 0);
    lLayout->setSpacing(0);
    lLayout->addWidget(m_rawConsole);
    m_leftWrapper->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_leftWrapper->setMinimumWidth(0);

    // ── 右栏：调试日志 ──
    m_debugConsole = new QPlainTextEdit;
    m_debugConsole->setReadOnly(true);
    m_debugConsole->setUndoRedoEnabled(false);
    m_debugConsole->setCenterOnScroll(false);
    m_debugConsole->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_debugConsole->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_debugConsole->document()->setMaximumBlockCount(MAX_LOG_BLOCKS);
    m_debugConsole->setPlaceholderText("调试信息与姿态处理日志...");
    m_debugConsole->setStyleSheet(Styles::CONSOLE_STYLE());
    m_debugConsole->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_debugConsole->setMinimumSize(0, 0);
    new ConsoleHighlighter(m_debugConsole->document(), "debug");
    m_debugConsole->installEventFilter(this);

    m_rightWrapper = new QWidget;
    auto* rLayout = new QVBoxLayout(m_rightWrapper);
    rLayout->setContentsMargins(0, 0, 0, 0);
    rLayout->setSpacing(0);
    rLayout->addWidget(m_debugConsole);
    m_rightWrapper->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_rightWrapper->setMinimumWidth(0);

    m_splitter->addWidget(m_leftWrapper);
    m_splitter->addWidget(m_rightWrapper);
    m_splitter->setSizes({640, 640});

    // ── 折叠按钮 ──
    m_btnFoldLeft  = new RotatingButton(true,  this);
    m_btnFoldLeft->setStyleSheet(Styles::STYLE_FOLD_BTN_LEFT());
    m_btnFoldLeft->setProperty("collapsed", false);
    connect(m_btnFoldLeft, &QPushButton::clicked, this, &DebugConsole::toggleLeftPanel);

    m_btnFoldRight = new RotatingButton(false, this);
    m_btnFoldRight->setStyleSheet(Styles::STYLE_FOLD_BTN_RIGHT());
    m_btnFoldRight->setProperty("collapsed", false);
    connect(m_btnFoldRight, &QPushButton::clicked, this, &DebugConsole::toggleRightPanel);

    m_btnFoldLeft->raise();
    m_btnFoldRight->raise();

    // ── 高度动画 ──
    m_animMin   = new QPropertyAnimation(this, "minimumHeight", this);
    m_animMax   = new QPropertyAnimation(this, "maximumHeight", this);
    m_animGroup = new QParallelAnimationGroup(this);
    for (auto* a : {m_animMin, m_animMax}) {
        a->setDuration(ANIM_DURATION_MS);
        a->setEasingCurve(QEasingCurve::OutCubic);
        m_animGroup->addAnimation(a);
    }
    connect(m_animGroup, &QParallelAnimationGroup::finished,
            this, &DebugConsole::onAnimFinished);

    // ── Splitter 动画 ──
    m_splitterAnim = new QVariantAnimation(this);
    m_splitterAnim->setDuration(180);
    m_splitterAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_splitterAnim, &QVariantAnimation::valueChanged,
            this, &DebugConsole::onSplitterAnimStep);
    connect(m_splitterAnim, &QVariantAnimation::finished,
            this, &DebugConsole::onSplitterAnimFinished);
    connect(m_splitter, &QSplitter::splitterMoved,
            this, &DebugConsole::onSplitterMoved);

    // ── 定时器 ──
    m_logFlushTimer = new QTimer(this);
    m_logFlushTimer->setSingleShot(true);
    m_logFlushTimer->setInterval(50);
    connect(m_logFlushTimer, &QTimer::timeout, this, &DebugConsole::flushPendingLogs);

    m_resizeRestoreTimer = new QTimer(this);
    m_resizeRestoreTimer->setSingleShot(true);
    m_resizeRestoreTimer->setInterval(120);
    connect(m_resizeRestoreTimer, &QTimer::timeout, this, &DebugConsole::finishInteractiveResize);

    // 安装全局事件过滤器（捕获 A/D 快捷键）
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        app->installEventFilter(this);

    hide();
}

// ── 高度动画 ──────────────────────────────────────────────────

// 显示/隐藏控制台（带滑入/出动画）
void DebugConsole::toggleVisibility(bool visible) {
    m_targetVisible = visible;
    m_animGroup->stop();

    int currentH = 0;
    if (visible) {
        if (!isVisible()) {
            setMinimumHeight(0);
            setMaximumHeight(0);
            show();
            currentH = 0;
        } else {
            currentH = height();
        }
        if (m_resetLayoutOnNextShow) {
            m_resetLayoutOnNextShow = false;
            resetToDefaultLayout();
        }
        restorePanelLayout();
    } else {
        currentH = height();
    }

    suspendInteraction();
    suspendConsoleRendering();

    int targetH = visible ? EXPANDED_HEIGHT : 0;
    for (auto* a : {m_animMin, m_animMax}) {
        a->setStartValue(currentH);
        a->setEndValue(targetH);
    }
    m_animGroup->start();
}

// 高度动画完成后处理隐藏/清空
void DebugConsole::onAnimFinished() {
    if (!m_targetVisible) {
        hide();
        m_pendingRawLogs.clear();
        m_pendingParsedLogs.clear();
        m_pendingDebugLogs.clear();
        m_rawLogHistory.clear();
        m_parsedLogHistory.clear();
        m_logFlushTimer->stop();
        m_rawConsole->clear();
        m_debugConsole->clear();
    }
    resumeConsoleRendering();
    resumeInteraction();
}

// ── Splitter 动画 ─────────────────────────────────────────────

void DebugConsole::onSplitterAnimStep(const QVariant& value) {
    if (m_animStartSizes.size() < 2 || m_animEndSizes.size() < 2) return;
    float t = value.toFloat();
    int s1 = int(m_animStartSizes[0] + (m_animEndSizes[0] - m_animStartSizes[0]) * t);
    int s2 = int(m_animStartSizes[1] + (m_animEndSizes[1] - m_animStartSizes[1]) * t);
    m_splitter->setSizes({s1, s2});
}

void DebugConsole::onSplitterAnimFinished() {
    if (m_animEndSizes.size() >= 2)
        m_splitter->setSizes(m_animEndSizes);
    if (m_leftCollapsed)  m_leftWrapper->hide();
    if (m_rightCollapsed) m_rightWrapper->hide();
    syncSplitterHandle();
    resumeConsoleRendering();
    resumeInteraction();
    checkAllCollapsed();
}

// 启动 splitter 缩放动画
void DebugConsole::startSplitterAnim(const QList<int>& start, const QList<int>& end) {
    m_animStartSizes = start;
    m_animEndSizes   = end;
    suspendInteraction();
    suspendConsoleRendering();
    m_splitterAnim->setStartValue(0.0f);
    m_splitterAnim->setEndValue(1.0f);
    m_splitterAnim->start();
}

// splitter 被用户手动拖动时处理折叠阈值
void DebugConsole::onSplitterMoved(int /*pos*/, int /*index*/) {
    if (m_splitterAnim->state() == QAbstractAnimation::Running) return;

    QList<int> sizes = m_splitter->sizes();
    if (!m_leftCollapsed && !m_rightCollapsed) {
        int threshold = std::max({DRAG_COLLAPSE_THRESHOLD,
                                  m_btnFoldLeft->width(),
                                  m_btnFoldRight->width()});
        if (sizes[0] <= threshold) { collapsePanelFromDrag(true);  return; }
        if (sizes[1] <= threshold) { collapsePanelFromDrag(false); return; }
        m_savedSizes = sizes;
    }
    if (!m_interactiveResizeActive) {
        m_interactiveResizeActive = true;
        suspendConsoleRendering();
    }
    m_resizeRestoreTimer->start();
}

void DebugConsole::finishInteractiveResize() {
    if (!m_interactiveResizeActive) return;
    if (m_splitterAnim->state() == QAbstractAnimation::Running) {
        m_resizeRestoreTimer->start(); return;
    }
    m_interactiveResizeActive = false;
    resumeConsoleRendering();
}

// 从拖动触发折叠某侧面板
void DebugConsole::collapsePanelFromDrag(bool collapseLeft) {
    m_resizeRestoreTimer->stop();
    if (m_interactiveResizeActive) {
        m_interactiveResizeActive = false;
        resumeConsoleRendering();
    }
    QList<int> cur = m_splitter->sizes();
    int total = cur[0] + cur[1];

    if (collapseLeft) {
        m_leftCollapsed = true;
        m_rawConsole->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_btnFoldLeft->animateFlip(true);
        setButtonCollapsedState(m_btnFoldLeft, true);
        m_btnFoldRight->setDownArrow(true);
        startSplitterAnim(cur, {0, total});
    } else {
        m_rightCollapsed = true;
        m_debugConsole->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_btnFoldRight->animateFlip(true);
        setButtonCollapsedState(m_btnFoldRight, true);
        m_btnFoldLeft->setDownArrow(true);
        startSplitterAnim(cur, {total, 0});
    }
}

// ── 折叠按钮逻辑 ─────────────────────────────────────────────

void DebugConsole::toggleLeftPanel() {
    if (m_splitterAnim->state() == QAbstractAnimation::Running) return;
    QList<int> cur = m_splitter->sizes();

    if (!m_leftCollapsed) {
        if (m_rightCollapsed) {
            collapseWhenLastPanelCloses(true); return;
        }
        m_savedSizes[0] = cur[0];
        m_leftCollapsed = true;
        m_btnFoldLeft->animateFlip(true);
        setButtonCollapsedState(m_btnFoldLeft, true);
        m_btnFoldRight->setDownArrow(true);
        m_rawConsole->setLineWrapMode(QPlainTextEdit::NoWrap);
        startSplitterAnim(cur, {0, cur[0] + cur[1]});
    } else {
        m_leftCollapsed = false;
        syncSplitterHandle();
        m_btnFoldLeft->animateFlip(false);
        setButtonCollapsedState(m_btnFoldLeft, false);
        m_btnFoldRight->setDownArrow(false);
        m_leftWrapper->show();
        m_rawConsole->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        int targetRight = cur[1] - m_savedSizes[0];
        startSplitterAnim(cur, {m_savedSizes[0], std::max(0, targetRight)});
    }
}

void DebugConsole::toggleRightPanel() {
    if (m_splitterAnim->state() == QAbstractAnimation::Running) return;
    QList<int> cur = m_splitter->sizes();

    if (!m_rightCollapsed) {
        if (m_leftCollapsed) {
            collapseWhenLastPanelCloses(false); return;
        }
        m_savedSizes[1] = cur[1];
        m_rightCollapsed = true;
        m_btnFoldRight->animateFlip(true);
        setButtonCollapsedState(m_btnFoldRight, true);
        m_btnFoldLeft->setDownArrow(true);
        m_debugConsole->setLineWrapMode(QPlainTextEdit::NoWrap);
        startSplitterAnim(cur, {cur[0] + cur[1], 0});
    } else {
        m_rightCollapsed = false;
        syncSplitterHandle();
        m_btnFoldRight->animateFlip(false);
        setButtonCollapsedState(m_btnFoldRight, false);
        m_btnFoldLeft->setDownArrow(false);
        m_rightWrapper->show();
        m_debugConsole->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        int targetLeft = cur[0] - m_savedSizes[1];
        startSplitterAnim(cur, {std::max(0, targetLeft), m_savedSizes[1]});
    }
}

// 最后一侧面板关闭（无动画，直接标记并发射信号）
void DebugConsole::collapseWhenLastPanelCloses(bool closingLeft) {
    if (closingLeft) {
        m_leftCollapsed = true;
        m_rawConsole->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_btnFoldLeft->resetFlip(true);
        m_btnFoldLeft->setDownArrow(true);
        m_btnFoldRight->setDownArrow(false);
        setButtonCollapsedState(m_btnFoldLeft, true);
    } else {
        m_rightCollapsed = true;
        m_debugConsole->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_btnFoldRight->resetFlip(true);
        m_btnFoldRight->setDownArrow(true);
        m_btnFoldLeft->setDownArrow(false);
        setButtonCollapsedState(m_btnFoldRight, true);
    }
    syncSplitterHandle();
    checkAllCollapsed();
}

// ── 辅助 ─────────────────────────────────────────────────────

void DebugConsole::syncSplitterHandle() {
    m_splitter->setHandleWidth((m_leftCollapsed || m_rightCollapsed) ? 0 : 2);
}

void DebugConsole::setButtonCollapsedState(RotatingButton* btn, bool collapsed) {
    btn->setDownArrow(false);
    btn->resetFlip(collapsed);
    btn->setProperty("collapsed", collapsed);
    btn->style()->unpolish(btn);
    btn->style()->polish(btn);
}

void DebugConsole::syncButtonArrowDirections() {
    if (m_leftCollapsed && !m_rightCollapsed)
        m_btnFoldRight->setDownArrow(true);
    else
        m_btnFoldRight->setDownArrow(false);

    if (m_rightCollapsed && !m_leftCollapsed)
        m_btnFoldLeft->setDownArrow(true);
    else
        m_btnFoldLeft->setDownArrow(false);
}

void DebugConsole::restorePanelLayout() {
    setButtonCollapsedState(m_btnFoldLeft,  m_leftCollapsed);
    setButtonCollapsedState(m_btnFoldRight, m_rightCollapsed);

    m_leftWrapper->setVisible(!m_leftCollapsed);
    m_rawConsole->setLineWrapMode(m_leftCollapsed
        ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);

    m_rightWrapper->setVisible(!m_rightCollapsed);
    m_debugConsole->setLineWrapMode(m_rightCollapsed
        ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);

    int total = std::max({m_savedSizes[0] + m_savedSizes[1],
                          m_splitter->width(), 1});
    if (m_leftCollapsed && !m_rightCollapsed)
        m_splitter->setSizes({0, total});
    else if (m_rightCollapsed && !m_leftCollapsed)
        m_splitter->setSizes({total, 0});
    else if (!m_leftCollapsed && !m_rightCollapsed)
        m_splitter->setSizes((m_savedSizes[0] + m_savedSizes[1] > 0)
            ? m_savedSizes : QList<int>{640, 640});

    syncButtonArrowDirections();
    syncSplitterHandle();
}

void DebugConsole::resetToDefaultLayout() {
    m_leftCollapsed  = false;
    m_rightCollapsed = false;
    m_savedSizes     = {640, 640};
}

void DebugConsole::checkAllCollapsed() {
    if (m_leftCollapsed && m_rightCollapsed) {
        m_resetLayoutOnNextShow = true;
        emit allCollapsed();
    }
}

// ── 渲染/交互挂起 ─────────────────────────────────────────────

void DebugConsole::suspendInteraction() {
    m_interactionSuspendCount++;
    if (m_interactionSuspendCount > 1) return;
    for (auto* w : {(QWidget*)m_splitter, (QWidget*)m_btnFoldLeft, (QWidget*)m_btnFoldRight})
        w->setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void DebugConsole::resumeInteraction() {
    if (m_interactionSuspendCount == 0) return;
    m_interactionSuspendCount--;
    if (m_interactionSuspendCount > 0) return;
    for (auto* w : {(QWidget*)m_splitter, (QWidget*)m_btnFoldLeft, (QWidget*)m_btnFoldRight})
        w->setAttribute(Qt::WA_TransparentForMouseEvents, false);
}

void DebugConsole::suspendConsoleRendering() {
    m_renderSuspendCount++;
    if (m_renderSuspendCount > 1) return;
    for (auto* c : {m_rawConsole, m_debugConsole}) {
        c->setLineWrapMode(QPlainTextEdit::NoWrap);
        c->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
}

void DebugConsole::resumeConsoleRendering() {
    if (m_renderSuspendCount == 0) return;
    m_renderSuspendCount--;
    if (m_renderSuspendCount > 0) return;
    m_rawConsole->setLineWrapMode(m_leftCollapsed
        ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
    m_debugConsole->setLineWrapMode(m_rightCollapsed
        ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
    for (auto* c : {m_rawConsole, m_debugConsole}) {
        c->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        c->viewport()->update();
    }
    flushPendingLogs();
}

// ── 日志处理 ─────────────────────────────────────────────────

void DebugConsole::scheduleFlushlogs() {
    if (!m_logFlushTimer->isActive())
        m_logFlushTimer->start();
}

// 批量刷新待处理日志
void DebugConsole::flushPendingLogs() {
    if (!m_targetVisible) {
        m_pendingRawLogs.clear();
        m_pendingParsedLogs.clear();
        m_pendingDebugLogs.clear();
        return;
    }
    if (m_renderSuspendCount > 0) {
        if (!m_pendingRawLogs.isEmpty() ||
            !m_pendingParsedLogs.isEmpty() ||
            !m_pendingDebugLogs.isEmpty())
            scheduleFlushlogs();
        return;
    }

    // 追加到历史环形队列
    for (const auto& l : m_pendingRawLogs) {
        m_rawLogHistory.push_back(l);
        if ((int)m_rawLogHistory.size() > MAX_LOG_BLOCKS)
            m_rawLogHistory.pop_front();
    }
    for (const auto& l : m_pendingParsedLogs) {
        m_parsedLogHistory.push_back(l);
        if ((int)m_parsedLogHistory.size() > MAX_LOG_BLOCKS)
            m_parsedLogHistory.pop_front();
    }

    if (isVisible() && !m_leftCollapsed) {
        if (m_leftPanelMode == "raw" && !m_pendingRawLogs.isEmpty())
            m_rawConsole->appendPlainText(m_pendingRawLogs.join('\n'));
        else if (m_leftPanelMode == "parsed" && !m_pendingParsedLogs.isEmpty())
            m_rawConsole->appendPlainText(m_pendingParsedLogs.join('\n'));
    }
    m_pendingRawLogs.clear();
    m_pendingParsedLogs.clear();

    if (!m_pendingDebugLogs.isEmpty() && isVisible() && !m_rightCollapsed)
        m_debugConsole->appendPlainText(m_pendingDebugLogs.join('\n'));
    m_pendingDebugLogs.clear();
}

// 追加原始数据日志行
void DebugConsole::onRawDataReceived(const QString& source, const QString& rawText) {
    if (!isVisible() || !m_targetVisible || m_leftCollapsed) return;
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_pendingRawLogs << QString("[%1] [%2] %3").arg(ts, source.toUpper(), rawText);
    scheduleFlushlogs();
}

// 追加解析数据日志行
void DebugConsole::onParsedDataReceived(const QString& source, const QString& parsedText) {
    if (!isVisible() || !m_targetVisible || m_leftCollapsed) return;
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_pendingParsedLogs << QString("[%1] [%2] %3").arg(ts, source.toUpper(), parsedText);
    scheduleFlushlogs();
}

// 追加 PoseProcessor 调试日志
void DebugConsole::onPoseLog(const QString& message) {
    if (!isVisible() || !m_targetVisible || m_rightCollapsed) return;
    m_pendingDebugLogs << message;
    scheduleFlushlogs();
}

// ── 左栏模式切换 ─────────────────────────────────────────────

bool DebugConsole::canToggleLeftPanelMode() const {
    return isVisible() && m_targetVisible && !m_leftCollapsed;
}

QString DebugConsole::leftPanelPlaceholder() const {
    return (m_leftPanelMode == "parsed")
        ? "解析数据日志...  按 A 切换为原始报文"
        : "原始报文日志...  按 A 切换为解析数据";
}

void DebugConsole::renderLeftPanelHistory() {
    QStringList history;
    if (m_leftPanelMode == "parsed")
        for (const auto& s : m_parsedLogHistory) history << s;
    else
        for (const auto& s : m_rawLogHistory) history << s;

    m_rawConsole->setPlainText(history.join('\n'));
    QTextCursor cur = m_rawConsole->textCursor();
    cur.movePosition(QTextCursor::End);
    m_rawConsole->setTextCursor(cur);
    m_rawConsole->ensureCursorVisible();
    m_rawConsole->setPlaceholderText(leftPanelPlaceholder());
}

bool DebugConsole::toggleLeftPanelLogMode() {
    if (!canToggleLeftPanelMode()) return false;
    m_leftPanelMode = (m_leftPanelMode == "raw") ? "parsed" : "raw";
    renderLeftPanelHistory();
    return true;
}

bool DebugConsole::scrollLogsToBottom() {
    if (!isVisible() || !m_targetVisible) return false;
    for (auto [console, collapsed] :
         std::initializer_list<std::pair<QPlainTextEdit*, bool>>{
             {m_rawConsole, m_leftCollapsed}, {m_debugConsole, m_rightCollapsed}}) {
        if (collapsed) continue;
        QTextCursor cur = console->textCursor();
        cur.movePosition(QTextCursor::End);
        console->setTextCursor(cur);
        console->ensureCursorVisible();
    }
    return true;
}

// ── 事件 ─────────────────────────────────────────────────────

void DebugConsole::resizeEvent(QResizeEvent* ev) {
    QWidget::resizeEvent(ev);
    // 折叠按钮贴靠左右边缘，全高
    m_btnFoldLeft->setGeometry(0, 0, 30, height());
    m_btnFoldRight->setGeometry(width() - 30, 0, 30, height());
    m_btnFoldLeft->raise();
    m_btnFoldRight->raise();
}

// 捕获 A（切换左栏模式）和 D（滚动到底）快捷键
bool DebugConsole::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() != QEvent::KeyPress) return QWidget::eventFilter(obj, ev);
    auto* ke = static_cast<QKeyEvent*>(ev);
    if (ke->isAutoRepeat()) return QWidget::eventFilter(obj, ev);

    QWidget* focus = QApplication::focusWidget();
    if (!focus || !isAncestorOf(focus)) return QWidget::eventFilter(obj, ev);

    if (ke->key() == Qt::Key_A && canToggleLeftPanelMode()) {
        if (toggleLeftPanelLogMode()) { ke->accept(); return true; }
    }
    if (ke->key() == Qt::Key_D && isVisible() && m_targetVisible) {
        if (scrollLogsToBottom()) { ke->accept(); return true; }
    }
    return QWidget::eventFilter(obj, ev);
}
