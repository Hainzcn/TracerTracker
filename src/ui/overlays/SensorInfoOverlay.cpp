#include "ui/overlays/SensorInfoOverlay.h"
#include <QGridLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

// ============================================================
// SensorInfoOverlay.cpp — 传感器信息叠加层实现
// ============================================================

static QString signedFixed(double v, int width, int decimals) {
    QChar sign = (v < 0.0) ? QChar('-') : QChar('+');
    QString num = QString::number(std::abs(v), 'f', decimals);
    while (num.length() < width - 1) num.prepend(' ');
    return sign + num;
}

SensorInfoOverlay::SensorInfoOverlay(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(300);
    setStyleSheet(
        "SensorInfoOverlay {"
        "  background-color: rgba(30, 30, 30, 200);"
        "  border: 1px solid rgba(80, 80, 80, 150);"
        "  border-radius: 8px;"
        "}"
    );

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(4);

    const QString titleStyle =
        "QLabel {"
        "  color: #888888;"
        "  font-family: 'Microsoft YaHei', sans-serif;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  background: transparent;"
        "}";

    const QString monoBase =
        "  font-family: 'Consolas', 'JetBrains Mono', monospace;"
        "  font-size: 13px;"
        "  background: transparent;";

    auto makeColorLabel = [&](const QString& colorHex) -> QLabel* {
        auto* l = new QLabel("--");
        l->setStyleSheet(QString("QLabel { color: %1; %2 }").arg(colorHex, monoBase));
        return l;
    };

    auto makeUnitLabel = [&]() -> QLabel* {
        auto* l = new QLabel();
        l->setStyleSheet(QString("QLabel { color: #888888; %1 }").arg(monoBase));
        return l;
    };

    // ── 第 0 行：ACC ──
    auto* accTitle = new QLabel("ACC");
    accTitle->setStyleSheet(titleStyle);
    layout->addWidget(accTitle, 0, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_accX = makeColorLabel("#ff6b6b"); layout->addWidget(m_accX, 0, 1);
    m_accY = makeColorLabel("#69db7c"); layout->addWidget(m_accY, 0, 2);
    m_accZ = makeColorLabel("#4dabf7"); layout->addWidget(m_accZ, 0, 3);
    m_accUnit = makeUnitLabel(); m_accUnit->setText("m/s\u00b2");
    layout->addWidget(m_accUnit, 0, 4);

    // ── 第 1 行：VEL ──
    auto* velTitle = new QLabel("VEL");
    velTitle->setStyleSheet(titleStyle);
    layout->addWidget(velTitle, 1, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_velX = makeColorLabel("#ff6b6b"); layout->addWidget(m_velX, 1, 1);
    m_velY = makeColorLabel("#69db7c"); layout->addWidget(m_velY, 1, 2);
    m_velZ = makeColorLabel("#4dabf7"); layout->addWidget(m_velZ, 1, 3);
    m_velUnit = makeUnitLabel(); m_velUnit->setText("m/s");
    layout->addWidget(m_velUnit, 1, 4);

    // ── 第 2 行：ΔAlt ──
    auto* altTitle = new QLabel("\u0394Alt");
    altTitle->setStyleSheet(titleStyle);
    layout->addWidget(altTitle, 2, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_altVal = makeColorLabel("#fcc419"); layout->addWidget(m_altVal, 2, 1);
    m_altDetail = makeUnitLabel(); layout->addWidget(m_altDetail, 2, 2, 1, 3);

    adjustSize();
    setVisible(false);
}

// 重置所有数据并隐藏
void SensorInfoOverlay::reset() {
    m_hasData     = false;
    m_refPressure.reset();
    m_refAltitude.reset();
    m_accX->setText("--"); m_accY->setText("--"); m_accZ->setText("--");
    m_velX->setText("--"); m_velY->setText("--"); m_velZ->setText("--");
    m_altVal->setText("--"); m_altDetail->clear();
    setVisible(false);
}

void SensorInfoOverlay::updateAcceleration(double ax, double ay, double az) {
    if (!m_hasData) {
        m_hasData = true;
        setVisible(true);
    }
    m_accX->setText(signedFixed(ax, 8, 2));
    m_accY->setText(signedFixed(ay, 8, 2));
    m_accZ->setText(signedFixed(az, 8, 2));
}

void SensorInfoOverlay::updateVelocity(double vx, double vy, double vz) {
    m_velX->setText(signedFixed(vx, 8, 3));
    m_velY->setText(signedFixed(vy, 8, 3));
    m_velZ->setText(signedFixed(vz, 8, 3));
}

void SensorInfoOverlay::updateAltitude(std::optional<double> pressure,
                                        std::optional<double> altitude) {
    if (altitude.has_value() && altitude.value() != 0.0) {
        double h = altitude.value();
        if (!m_refAltitude.has_value()) m_refAltitude = h;
        double delta = h - m_refAltitude.value();
        m_altVal->setText(signedFixed(delta, 8, 2));
        m_altDetail->setText(QString("m  (%1 m)").arg(h, 0, 'f', 1));
        return;
    }
    if (pressure.has_value() && pressure.value() > 0.0) {
        double p = pressure.value();
        if (!m_refPressure.has_value()) m_refPressure = p;
        double h = 44330.0 * (1.0 - std::pow(p / m_refPressure.value(), 1.0 / 5.255));
        m_altVal->setText(signedFixed(h, 8, 2));
        m_altDetail->setText(QString("m  (P=%1 Pa)").arg(p, 0, 'f', 0));
    }
}

// 穿透鼠标事件（让底层 Viewer3D 处理）
void SensorInfoOverlay::mousePressEvent(QMouseEvent* ev)   { ev->ignore(); }
void SensorInfoOverlay::mouseReleaseEvent(QMouseEvent* ev) { ev->ignore(); }
void SensorInfoOverlay::mouseMoveEvent(QMouseEvent* ev)    { ev->ignore(); }
void SensorInfoOverlay::wheelEvent(QWheelEvent* ev)        { ev->ignore(); }
