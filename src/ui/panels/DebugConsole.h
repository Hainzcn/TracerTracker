#pragma once
#include <QWidget>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QVariantAnimation>
#include <QTimer>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <deque>
#include <QPolygonF>

// ============================================================
// DebugConsole.h — 可折叠调试控制台
//
// 布局：左栏（原始/解析数据）+ 右栏（PoseProcessor 日志）
//   - 高度动画：toggleVisibility()
//   - 左右面板可独立折叠（带翻转箭头动画）
//   - 关键字高亮：UDP/Serial 来源、WARNING/ERROR
//   - 键盘：A 切换左栏模式，D 滚动到底部
// ============================================================

// ── ConsoleHighlighter ────────────────────────────────────────

// 支持 "raw" 和 "debug" 两种模式的语法高亮器
class ConsoleHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit ConsoleHighlighter(QTextDocument* doc, const QString& mode);
protected:
    void highlightBlock(const QString& text) override;
private:
    QString          m_mode;
    QTextCharFormat  m_defaultFmt;
    QTextCharFormat  m_timestampFmt;
    QTextCharFormat  m_udpFmt;
    QTextCharFormat  m_serialFmt;
    QTextCharFormat  m_warnFmt;
    QTextCharFormat  m_errorFmt;
};

// ── RotatingButton ────────────────────────────────────────────

// 支持水平翻转动画的折叠箭头按钮（矢量三角形绘制）
class RotatingButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(float flipScale READ flipScale WRITE setFlipScale)
public:
    explicit RotatingButton(bool facesLeft, QWidget* parent = nullptr);

    float flipScale() const { return m_flipScale; }
    void  setFlipScale(float v);

    // 播放翻转动画（collapsed=true → 折叠方向）
    void animateFlip(bool collapsed);
    // 直接重置到折叠/展开状态（无动画）
    void resetFlip(bool collapsed);
    // 切换为向下箭头（所有面板均折叠时）
    void setDownArrow(bool enabled);

protected:
    void enterEvent(QEnterEvent* ev) override;
    void leaveEvent(QEvent* ev) override;
    void paintEvent(QPaintEvent* ev) override;

private:
    bool  m_facesLeft;          // true=左侧按钮，false=右侧按钮
    float m_defaultFlipScale;   // 默认比例（面向右 +1，面向左 -1）
    float m_flipScale;
    bool  m_showDownArrow = false;

    QPropertyAnimation* m_flipAnim;
    static constexpr int ANIM_MS = 180;
};

// ── DebugConsole ──────────────────────────────────────────────

class DebugConsole : public QWidget {
    Q_OBJECT
public:
    explicit DebugConsole(QWidget* parent = nullptr);

    // 显示/隐藏控制台（带高度滑入/出动画）
    void toggleVisibility(bool visible);

    // 切换左栏在原始/解析数据模式间切换
    bool toggleLeftPanelLogMode();
    // 滚动两栏到最底部
    bool scrollLogsToBottom();

    // 折叠/展开左右面板
    void toggleLeftPanel();
    void toggleRightPanel();

signals:
    // 左右面板全部折叠时发射（主窗口取消勾选复选框）
    void allCollapsed();

public slots:
    // 追加原始数据日志（来自 DataReceiver::rawDataReceived）
    void onRawDataReceived(const QString& source, const QString& rawText);
    // 追加解析数据日志（来自 DataReceiver::parsedDataReceived）
    void onParsedDataReceived(const QString& source, const QString& parsedText);
    // 追加 PoseProcessor 调试日志
    void onPoseLog(const QString& message);

protected:
    void resizeEvent(QResizeEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onAnimFinished();
    void onSplitterAnimStep(const QVariant& value);
    void onSplitterAnimFinished();
    void onSplitterMoved(int pos, int index);
    void flushPendingLogs();
    void finishInteractiveResize();

private:
    // ── 布局 ──
    QSplitter*      m_splitter      = nullptr;
    QWidget*        m_leftWrapper   = nullptr;
    QWidget*        m_rightWrapper  = nullptr;
    QPlainTextEdit* m_rawConsole    = nullptr;
    QPlainTextEdit* m_debugConsole  = nullptr;
    RotatingButton* m_btnFoldLeft   = nullptr;
    RotatingButton* m_btnFoldRight  = nullptr;

    // ── 高度动画 ──
    QPropertyAnimation*      m_animMin   = nullptr;
    QPropertyAnimation*      m_animMax   = nullptr;
    QParallelAnimationGroup* m_animGroup = nullptr;
    bool m_targetVisible = false;

    // ── 面板折叠状态 ──
    bool         m_leftCollapsed  = false;
    bool         m_rightCollapsed = false;
    QList<int>   m_savedSizes     = {640, 640};
    bool         m_resetLayoutOnNextShow = false;

    // ── Splitter 动画 ──
    QVariantAnimation* m_splitterAnim = nullptr;
    QList<int> m_animStartSizes;
    QList<int> m_animEndSizes;

    // ── 日志节流 ──
    QTimer* m_logFlushTimer   = nullptr;
    QTimer* m_resizeRestoreTimer = nullptr;
    bool    m_interactiveResizeActive = false;

    // ── 渲染/交互挂起计数器 ──
    int  m_renderSuspendCount      = 0;
    int  m_interactionSuspendCount = 0;

    // ── 日志队列 ──
    QStringList m_pendingRawLogs;
    QStringList m_pendingParsedLogs;
    QStringList m_pendingDebugLogs;
    std::deque<QString> m_rawLogHistory;
    std::deque<QString> m_parsedLogHistory;
    static constexpr int MAX_LOG_BLOCKS = 500;

    // ── 左栏模式 ("raw" / "parsed") ──
    QString m_leftPanelMode = "raw";

    // ── 内部辅助 ──
    void suspendInteraction();
    void resumeInteraction();
    void suspendConsoleRendering();
    void resumeConsoleRendering();
    void startSplitterAnim(const QList<int>& start, const QList<int>& end);
    void syncSplitterHandle();
    void syncButtonArrowDirections();
    void restorePanelLayout();
    void resetToDefaultLayout();
    void setButtonCollapsedState(RotatingButton* btn, bool collapsed);
    void scheduleFlushlogs();
    void collapsePanelFromDrag(bool collapseLeft);
    void collapseWhenLastPanelCloses(bool closingLeft);
    void checkAllCollapsed();
    void renderLeftPanelHistory();
    bool canToggleLeftPanelMode() const;
    QString leftPanelPlaceholder() const;

    static constexpr int EXPANDED_HEIGHT         = 200;
    static constexpr int ANIM_DURATION_MS        = 180;
    static constexpr int DRAG_COLLAPSE_THRESHOLD = 50;
};
