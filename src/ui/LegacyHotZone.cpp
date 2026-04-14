#include "LegacyHotZone.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QEnterEvent>

AttitudePanelHotZone::AttitudePanelHotZone(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    m_bgAnim = new QVariantAnimation(this);
    m_bgAnim->setDuration(140);
    m_bgAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_bgAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){
        m_bgAlpha = v.toDouble(); update();
    });

    m_stripAnim = new QVariantAnimation(this);
    m_stripAnim->setDuration(160);
    m_stripAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_stripAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){
        m_stripProgress = v.toDouble(); update();
    });
}

void AttitudePanelHotZone::animateBg(double target) {
    m_bgAnim->stop();
    m_bgAnim->setStartValue(m_bgAlpha);
    m_bgAnim->setEndValue(target);
    m_bgAnim->start();
}

void AttitudePanelHotZone::animateStrip(double target) {
    m_stripAnim->stop();
    m_stripAnim->setStartValue(m_stripProgress);
    m_stripAnim->setEndValue(target);
    m_stripAnim->start();
}

void AttitudePanelHotZone::enterEvent(QEnterEvent* ev) {
    animateBg(128.0);
    animateStrip(1.0);
    QWidget::enterEvent(ev);
}

void AttitudePanelHotZone::leaveEvent(QEvent* ev) {
    m_pressed = false;
    animateBg(0.0);
    animateStrip(0.0);
    QWidget::leaveEvent(ev);
}

void AttitudePanelHotZone::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) {
        m_pressed = true;
        animateBg(156.0);
        ev->accept();
    } else {
        ev->accept();
    }
}

void AttitudePanelHotZone::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) {
        bool inside = rect().contains(ev->position().toPoint());
        m_pressed = false;
        animateBg(inside ? 128.0 : 0.0);
        if (inside) { emit clicked(); ev->accept(); return; }
    }
    ev->accept();
}

void AttitudePanelHotZone::mouseMoveEvent(QMouseEvent* ev)    { ev->accept(); }
void AttitudePanelHotZone::wheelEvent(QWheelEvent* ev)        { ev->accept(); }
void AttitudePanelHotZone::mouseDoubleClickEvent(QMouseEvent* ev){ ev->accept(); }

// 绘制半透明拉条
void AttitudePanelHotZone::paintEvent(QPaintEvent* ev) {
    if (m_bgAlpha <= 0.0 || m_stripProgress <= 0.0) { QWidget::paintEvent(ev); return; }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill(220, 220, 220, int(m_bgAlpha));
    int visW = std::max(1, std::min(VISIBLE_WIDTH, int(std::round(VISIBLE_WIDTH * m_stripProgress))));
    p.fillRect(0, 0, visW, height(), fill);
    QWidget::paintEvent(ev);
}