#include "SensorInfoOverlay.h"
#include <QGridLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

// ============================================================
// SensorInfoOverlay.cpp — 传感器信息叠加层实现
// ============================================================

SensorInfoOverlay::SensorInfoOverlay(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(300);
    // 半透明圆角背景
    setStyleSheet(
        "SensorInfoOverlay {"
        "  background-color: rgba(30, 30, 30, 200);"
        "  border: 1px solid rgba(80, 80, 80, 150);"
        "  border-radius: 8px;"
        "}"
    );

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(4);

    // 标题列样式（灰色小号粗体）
    const QString titleStyle =
        "QLabel {"
        "  color: #888888;"
        "  font-family: 'Microsoft YaHei', sans-serif;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  background: transparent;"
        "}";

    // 数值列样式（浅色等宽字体）
    const QString valueStyle =
        "QLabel {"
        "  color: #e0e0e0;"
        "  font-family: 'Consolas', 'JetBrains Mono', monospace;"
        "  font-size: 13px;"
        "  background: transparent;"
        "}";

    // ── 第 0 行：ACC ──
    auto* accTitle = new QLabel("ACC");
    accTitle->setStyleSheet(titleStyle);
    layout->addWidget(accTitle, 0, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_accLabel = new QLabel("--");
    m_accLabel->setStyleSheet(valueStyle);
    layout->addWidget(m_accLabel, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);

    // ── 第 1 行：VEL ──
    auto* velTitle = new QLabel("VEL");
    velTitle->setStyleSheet(titleStyle);
    layout->addWidget(velTitle, 1, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_velLabel = new QLabel("--");
    m_velLabel->setStyleSheet(valueStyle);
    layout->addWidget(m_velLabel, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);

    // ── 第 2 行：ΔAlt ──
    auto* altTitle = new QLabel("\u0394Alt");
    altTitle->setStyleSheet(titleStyle);
    layout->addWidget(altTitle, 2, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_altLabel = new QLabel("--");
    m_altLabel->setStyleSheet(valueStyle);
    layout->addWidget(m_altLabel, 2, 1, Qt::AlignLeft | Qt::AlignVCenter);

    layout->setColumnStretch(1, 1);
    adjustSize();
    setVisible(false);
}

// 重置所有数据并隐藏
void SensorInfoOverlay::reset() {
    m_hasData     = false;
    m_refPressure.reset();
    m_refAltitude.reset();
    m_accLabel->setText("--");
    m_velLabel->setText("--");
    m_altLabel->setText("--");
    setVisible(false);
}

// 更新加速度（三轴 RGB 配色）
void SensorInfoOverlay::updateAcceleration(double ax, double ay, double az) {
    if (!m_hasData) {
        m_hasData = true;
        setVisible(true);
    }
    m_accLabel->setText(QString(
        "<span style='color:#ff6b6b'>%1</span> "
        "<span style='color:#69db7c'>%2</span> "
        "<span style='color:#4dabf7'>%3</span> "
        "<span style='color:#888'>m/s\u00b2</span>"
    ).arg(ax, 8, 'f', 2, ' ')
     .arg(ay, 8, 'f', 2, ' ')
     .arg(az, 8, 'f', 2, ' '));
}

// 更新速度显示
void SensorInfoOverlay::updateVelocity(double vx, double vy, double vz) {
    m_velLabel->setText(QString(
        "<span style='color:#ff6b6b'>%1</span> "
        "<span style='color:#69db7c'>%2</span> "
        "<span style='color:#4dabf7'>%3</span> "
        "<span style='color:#888'>m/s</span>"
    ).arg(vx, 8, 'f', 3, ' ')
     .arg(vy, 8, 'f', 3, ' ')
     .arg(vz, 8, 'f', 3, ' '));
}

// 更新海拔变化（优先直接海拔值，回退气压公式）
void SensorInfoOverlay::updateAltitude(std::optional<double> pressure,
                                        std::optional<double> altitude) {
    if (altitude.has_value() && altitude.value() != 0.0) {
        double h = altitude.value();
        if (!m_refAltitude.has_value()) m_refAltitude = h;
        double delta = h - m_refAltitude.value();
        m_altLabel->setText(QString(
            "<span style='color:#fcc419'>%1</span> "
            "<span style='color:#888'>m  (%2 m)</span>"
        ).arg(delta, 8, 'f', 2, ' ')
         .arg(h, 0, 'f', 1));
        return;
    }
    if (pressure.has_value() && pressure.value() > 0.0) {
        double p = pressure.value();
        if (!m_refPressure.has_value()) m_refPressure = p;
        double h = 44330.0 * (1.0 - std::pow(p / m_refPressure.value(), 1.0 / 5.255));
        m_altLabel->setText(QString(
            "<span style='color:#fcc419'>%1</span> "
            "<span style='color:#888'>m  (P=%2 Pa)</span>"
        ).arg(h, 8, 'f', 2, ' ')
         .arg(p, 0, 'f', 0));
    }
}

// 穿透鼠标事件（让底层 Viewer3D 处理）
void SensorInfoOverlay::mousePressEvent(QMouseEvent* ev)   { ev->ignore(); }
void SensorInfoOverlay::mouseReleaseEvent(QMouseEvent* ev) { ev->ignore(); }
void SensorInfoOverlay::mouseMoveEvent(QMouseEvent* ev)    { ev->ignore(); }
void SensorInfoOverlay::wheelEvent(QWheelEvent* ev)        { ev->ignore(); }
