#include "SensorChartPanel.h"
#include <QPainter>
#include <QPainterPath>
#include <QElapsedTimer>
#include <QDateTime>
#include <cmath>
#include <algorithm>
#include <numeric>

// ============================================================
// SensorChartPanel.cpp — 传感器折线图面板实现
// ============================================================

// 颜色常量初始化
const QColor SensorChartPanel::COLOR_TITLE    = QColor(153,153,153);
const QColor SensorChartPanel::COLOR_GRID     = QColor(255,255,255,24);
const QColor SensorChartPanel::COLOR_ZERO_LINE= QColor(255,255,255,46);
const QColor SensorChartPanel::COLOR_BORDER   = QColor(80,80,80,150);
const QColor SensorChartPanel::COLOR_BG       = QColor(30,30,30,150);
const std::array<QColor,3> SensorChartPanel::ACC_COLORS = {{
    QColor("#ff6b6b"), QColor("#69db7c"), QColor("#4dabf7")
}};
const std::array<QColor,3> SensorChartPanel::RPY_COLORS = {{
    QColor("#ff6b6b"), QColor("#69db7c"), QColor("#4dabf7")
}};
const QColor SensorChartPanel::ALT_COLOR = QColor("#fcc419");

SensorChartPanel::SensorChartPanel(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    int totalH = SECTION_HEIGHT * 3 + SECTION_GAP * 2;
    setFixedSize(CHART_WIDTH, totalH);
    setVisible(false);
}

// 重置所有历史数据
void SensorChartPanel::reset() {
    for (auto& d : m_accHistory) d.clear();
    for (auto& d : m_rpyHistory) d.clear();
    m_altHistory.clear();
    m_refPressure.reset();
    m_pending.valid = false;
    m_lastFlushMs   = 0;
    update();
}

// 解析海拔（优先直接海拔，回退气压计算）
std::optional<double> SensorChartPanel::resolveAltitude(
    std::optional<double> pressure, std::optional<double> altitude) {
    if (altitude.has_value() && altitude.value() != 0.0)
        return altitude;
    if (pressure.has_value() && pressure.value() > 0.0) {
        if (!m_refPressure.has_value()) m_refPressure = pressure.value();
        double ratio = pressure.value() / m_refPressure.value();
        return 44330.0 * (1.0 - std::pow(ratio, 1.0 / 5.255));
    }
    return std::nullopt;
}

// 推送快照（节流刷新）
void SensorChartPanel::pushSnapshot(
    std::optional<std::tuple<double,double,double>> acc,
    std::optional<std::tuple<double,double,double>> euler,
    std::optional<double> pressure,
    std::optional<double> altitude) {
    m_pending = {acc, euler, pressure, altitude, true};

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - m_lastFlushMs < qint64(UPDATE_INTERVAL_SEC * 1000)) return;
    m_lastFlushMs = nowMs;

    auto snap = m_pending;
    m_pending.valid = false;

    // 追加加速度历史
    if (snap.acc.has_value()) {
        auto [ax,ay,az] = snap.acc.value();
        auto push3 = [&](auto vals) {
            double v[3] = {std::get<0>(vals), std::get<1>(vals), std::get<2>(vals)};
            for (int i = 0; i < 3; ++i) {
                m_accHistory[i].push_back(v[i]);
                if ((int)m_accHistory[i].size() > HISTORY_LEN) m_accHistory[i].pop_front();
            }
        };
        push3(snap.acc.value());
    }
    // 追加 RPY 历史
    if (snap.euler.has_value()) {
        auto [r,p,y] = snap.euler.value();
        double vals[3] = {r, p, y};
        for (int i = 0; i < 3; ++i) {
            m_rpyHistory[i].push_back(vals[i]);
            if ((int)m_rpyHistory[i].size() > HISTORY_LEN) m_rpyHistory[i].pop_front();
        }
    }
    // 追加海拔历史
    auto alt = resolveAltitude(snap.pressure, snap.altitude);
    if (alt.has_value()) {
        m_altHistory.push_back(alt.value());
        if ((int)m_altHistory.size() > HISTORY_LEN) m_altHistory.pop_front();
    }

    update();
}

// ── 绘制 ──────────────────────────────────────────────────────

void SensorChartPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 三段折线图垂直排列
    struct Section {
        QString title;
        std::vector<const std::deque<double>*> history;
        std::vector<QColor> colors;
        bool centered;
    };

    std::vector<Section> sections = {
        {"ACC",
         {&m_accHistory[0], &m_accHistory[1], &m_accHistory[2]},
         {ACC_COLORS[0], ACC_COLORS[1], ACC_COLORS[2]}, true},
        {"RPY",
         {&m_rpyHistory[0], &m_rpyHistory[1], &m_rpyHistory[2]},
         {RPY_COLORS[0], RPY_COLORS[1], RPY_COLORS[2]}, true},
        {"ALT",
         {&m_altHistory},
         {ALT_COLOR}, false},
    };

    int top = 0;
    for (const auto& sec : sections) {
        QRectF rect(0, top, width(), SECTION_HEIGHT);
        drawSection(p, rect, sec.title, sec.history, sec.colors, sec.centered);
        top += SECTION_HEIGHT + SECTION_GAP;
    }
}

// 绘制单个图段
void SensorChartPanel::drawSection(QPainter& p, const QRectF& sectionRect,
                                    const QString& title,
                                    const std::vector<const std::deque<double>*>& history,
                                    const std::vector<QColor>& colors,
                                    bool centered) const {
    // 标题区域
    QRectF titleRect(sectionRect.left(), sectionRect.top(), sectionRect.width(), HEADER_HEIGHT);
    // 图框区域
    QRectF frameRect(
        sectionRect.left() + H_PADDING + FRAME_INSET,
        titleRect.bottom() + 3,
        sectionRect.width() - 2*H_PADDING - 2*FRAME_INSET,
        sectionRect.height() - HEADER_HEIGHT - 3 - FRAME_INSET
    );
    // 绘图区域（图框内缩）
    QRectF chartRect = frameRect.adjusted(
        CHART_INSET, V_PADDING + CHART_INSET, -CHART_INSET, -(V_PADDING + CHART_INSET));

    // 绘制图框背景
    p.setPen(QPen(COLOR_BORDER, 1));
    p.setBrush(COLOR_BG);
    p.drawRect(frameRect.adjusted(0.5, 0.5, -0.5, -0.5));

    // 绘制标题
    p.save();
    p.setFont(QFont("Consolas", 10));
    p.setPen(COLOR_TITLE);
    p.drawText(titleRect, Qt::AlignHCenter | Qt::AlignBottom, title);
    p.restore();

    // 绘制中心线（零线）
    QColor lineColor = centered ? COLOR_ZERO_LINE : COLOR_GRID;
    double midY = chartRect.center().y();
    p.setPen(QPen(lineColor, 1));
    p.drawLine(QPointF(chartRect.left(), midY), QPointF(chartRect.right(), midY));

    // 收集所有有效历史数据
    std::vector<std::vector<double>> seriesData;
    for (const auto* d : history) {
        if (!d->empty()) seriesData.push_back({d->begin(), d->end()});
    }
    if (seriesData.empty()) return;

    // 计算 Y 轴映射函数
    std::function<double(double)> valueToY;
    if (centered) {
        // 双轴对称：以最大绝对值缩放
        double maxAbs = 1.0;
        for (const auto& s : seriesData)
            for (double v : s) maxAbs = std::max(maxAbs, std::abs(v));
        double amplitude = chartRect.height() * 0.42;
        valueToY = [=](double v) -> double {
            return chartRect.center().y() - (v / maxAbs) * amplitude;
        };
    } else {
        // 单边：按实际范围缩放
        double minVal = 1e18, maxVal = -1e18;
        for (const auto& s : seriesData)
            for (double v : s) { minVal = std::min(minVal, v); maxVal = std::max(maxVal, v); }
        if (maxVal - minVal < 1e-6) { minVal -= 0.5; maxVal += 0.5; }
        double pad = std::max((maxVal - minVal) * 0.08, 0.2);
        minVal -= pad; maxVal += pad;
        valueToY = [=](double v) -> double {
            double ratio = (v - minVal) / (maxVal - minVal);
            return chartRect.bottom() - ratio * chartRect.height();
        };
    }

    // 绘制各轴折线
    for (size_t si = 0; si < seriesData.size() && si < colors.size(); ++si) {
        const auto& data = seriesData[si];
        if (data.empty()) continue;

        QPainterPath path;
        for (size_t idx = 0; idx < data.size(); ++idx) {
            double x = chartRect.left() + chartRect.width() * idx / std::max(HISTORY_LEN - 1, 1);
            double y = valueToY(data[idx]);
            if (idx == 0) path.moveTo(x, y);
            else          path.lineTo(x, y);
        }
        p.setPen(QPen(colors[si], 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
}
