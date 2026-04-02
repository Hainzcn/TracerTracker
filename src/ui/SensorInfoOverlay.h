#pragma once
#include <QWidget>
#include <QLabel>
#include <optional>

// ============================================================
// SensorInfoOverlay.h — 右下角半透明传感器信息叠加层
//
// 显示三行：
//   ACC  — 三轴线加速度（m/s²）
//   VEL  — 三轴速度（m/s）
//   ΔAlt — 海拔变化（m）
//
// 鼠标事件穿透；首次有效数据到来时自动 show()
// ============================================================

class SensorInfoOverlay : public QWidget {
    Q_OBJECT
public:
    explicit SensorInfoOverlay(QWidget* parent = nullptr);

    // 重置所有数据并隐藏
    void reset();

public slots:
    // 更新加速度显示（单位 m/s²）
    void updateAcceleration(double ax, double ay, double az);
    // 更新速度显示（单位 m/s）
    void updateVelocity(double vx, double vy, double vz);
    // 更新海拔显示（优先直接海拔，回退气压估算）
    void updateAltitude(std::optional<double> pressure = std::nullopt,
                        std::optional<double> altitude = std::nullopt);

protected:
    // 穿透鼠标事件
    void mousePressEvent(QMouseEvent* ev)  override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev)   override;
    void wheelEvent(QWheelEvent* ev)       override;

private:
    QLabel* m_accLabel  = nullptr;
    QLabel* m_velLabel  = nullptr;
    QLabel* m_altLabel  = nullptr;

    std::optional<double> m_refPressure;
    std::optional<double> m_refAltitude;
    bool m_hasData = false;
};
