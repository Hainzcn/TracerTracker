#pragma once
#include <QWidget>
#include <QColor>
#include <deque>
#include <vector>
#include <optional>

// ============================================================
// SensorChartPanel.h — 传感器数据折线图面板
//
// 浮动在 3D 视口左侧，显示三段折线图：
//   - ACC：加速度计三轴（归一化后双轴对称绘制）
//   - RPY：Roll/Pitch/Yaw（归一化后双轴对称绘制）
//   - ALT：海拔/气压（单边绘制）
//
// 200ms 节流刷新，120 点历史缓冲
// ============================================================

class SensorChartPanel : public QWidget {
    Q_OBJECT
public:
    explicit SensorChartPanel(QWidget* parent = nullptr);

    // 重置所有历史数据
    void reset();

    // 推送新的传感器快照（200ms 节流后实际更新）
    // acceleration：三轴加速度（m/s²）
    // euler：RPY（度）
    // pressure：气压（Pa），可空
    // altitude：海拔（m），可空
    void pushSnapshot(std::optional<std::tuple<double,double,double>> acceleration = std::nullopt,
                      std::optional<std::tuple<double,double,double>> euler       = std::nullopt,
                      std::optional<double> pressure = std::nullopt,
                      std::optional<double> altitude = std::nullopt);

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    // ── 布局常量（与 Python 版完全对应）─────────────────────────
    static constexpr int HISTORY_LEN    = 120;  // 历史采样点数
    static constexpr int CHART_WIDTH    = 90;   // 图表宽度（与立方体面板对齐）
    static constexpr int SECTION_HEIGHT = 74;   // 单段高度
    static constexpr int SECTION_GAP    = 8;    // 段间距
    static constexpr int HEADER_HEIGHT  = 12;   // 标题高度
    static constexpr int H_PADDING      = 6;    // 水平内边距
    static constexpr int V_PADDING      = 6;    // 垂直内边距
    static constexpr int FRAME_INSET    = 1;    // 图框内缩
    static constexpr int CHART_INSET    = 2;    // 绘图区安全边距
    static constexpr double UPDATE_INTERVAL_SEC = 0.2; // 节流间隔（s）

    // 颜色常量
    static const QColor COLOR_TITLE;
    static const QColor COLOR_GRID;
    static const QColor COLOR_ZERO_LINE;
    static const QColor COLOR_BORDER;
    static const QColor COLOR_BG;
    static const std::array<QColor, 3> ACC_COLORS; // RGB 三轴颜色
    static const std::array<QColor, 3> RPY_COLORS;
    static const QColor ALT_COLOR;

    // 历史数据缓冲区（各轴独立 deque）
    std::array<std::deque<double>, 3> m_accHistory;
    std::array<std::deque<double>, 3> m_rpyHistory;
    std::deque<double>                m_altHistory;

    // 气压参考值（首次有效值）
    std::optional<double> m_refPressure;
    // 节流：上次实际刷新的时间戳（Qt::ElapsedTimer 毫秒）
    qint64 m_lastFlushMs = 0;
    // 最新待处理快照
    struct PendingSnapshot {
        std::optional<std::tuple<double,double,double>> acc;
        std::optional<std::tuple<double,double,double>> euler;
        std::optional<double> pressure;
        std::optional<double> altitude;
        bool valid = false;
    } m_pending;

    // 解析海拔（优先使用直接海拔值，回退到气压计算）
    std::optional<double> resolveAltitude(std::optional<double> pressure,
                                           std::optional<double> altitude);

    // 绘制单个折线图段（title=段标题，history=各轴数据，colors=颜色数组，centered=是否双轴对称）
    void drawSection(QPainter& p, const QRectF& sectionRect,
                     const QString& title,
                     const std::vector<const std::deque<double>*>& history,
                     const std::vector<QColor>& colors,
                     bool centered) const;
};
