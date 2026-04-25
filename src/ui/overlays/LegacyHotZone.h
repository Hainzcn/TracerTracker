#pragma once
#include <QWidget>
#include <QVariantAnimation>

// ============================================================
// LegacyHotZone.h — 遗留的面板热区组件
// ============================================================

// 贴靠 Viewer3D 左侧的透明热区，hover 时显示拉条动效
class AttitudePanelHotZone : public QWidget {
    Q_OBJECT
public:
    explicit AttitudePanelHotZone(QWidget* parent = nullptr);

    static constexpr int VISIBLE_WIDTH = 10;

signals:
    void clicked();

protected:
    void enterEvent(QEnterEvent* ev)   override;
    void leaveEvent(QEvent* ev)        override;
    void mousePressEvent(QMouseEvent* ev)   override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev)    override;
    void wheelEvent(QWheelEvent* ev)        override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void paintEvent(QPaintEvent* ev) override;

private:
    void animateBg(double target);
    void animateStrip(double target);

    double m_bgAlpha       = 0.0;
    double m_stripProgress = 0.0;
    bool   m_pressed       = false;

    QVariantAnimation* m_bgAnim    = nullptr;
    QVariantAnimation* m_stripAnim = nullptr;
};
