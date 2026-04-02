#pragma once
#include <QWidget>
#include <QPointF>
#include <QColor>
#include <QFont>
#include <optional>

// ============================================================
// ViewOrientationGizmo.h — Blender 风格视角方向罗盘
//
// 显示在 3D 视口左上角，90×90 像素。
// 绘制 X/Y/Z 三轴（正轴实心圆 + 标签，负轴空心圆）。
// 点击轴端点 → emit viewSelected(elevation, azimuth) → animateToView
// 拖动 → 直接 orbit 相机
// ============================================================

class Viewer3D;

class ViewOrientationGizmo : public QWidget {
    Q_OBJECT
public:
    // viewer: 用于 orbit() 调用和获取相机参数
    explicit ViewOrientationGizmo(Viewer3D* viewer, QWidget* parent = nullptr);

    // 由 Viewer3D::cameraChanged 触发，同步当前相机朝向
    void updateOrientation();

signals:
    // 用户点击轴端点时发射（elevation, azimuth，度）
    void viewSelected(float elevation, float azimuth);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev)   override;
    void mouseMoveEvent(QMouseEvent* ev)    override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void leaveEvent(QEvent* ev)             override;
    void wheelEvent(QWheelEvent* ev)        override;

private:
    // 3D 轴定义
    struct AxisDef {
        float dx, dy, dz;   // 单位向量
        QColor color;
        QString label;
        float presetPosElev, presetPosAzim;   // 正端点视角预设（度）
        float presetNegElev, presetNegAzim;   // 负端点视角预设
        bool  negAzimInherit;                 // true=Z轴负端沿用当前方位角
    };

    // 已投影的端点（用于命中测试和绘制）
    struct Endpoint {
        float sx, sy;        // 屏幕坐标
        float depth;         // 深度（-1~1）
        float scale;         // 透视缩放
        QColor color;        // 深度调暗后的颜色
        QString label;       // 标签（仅正轴有）
        bool positive;
        QString key;         // "+x", "-y" 等
        int axisIdx;         // 所属 AxisDef 索引
    };

    // 将世界方向投影到 gizmo 屏幕坐标
    std::tuple<float,float,float,float> project(float dx, float dy, float dz) const;

    // 构建所有端点（含深度排序）
    QList<Endpoint> buildEndpoints() const;

    // 命中测试：返回端点 key 与视角预设
    struct HitResult { QString key; int axisIdx; bool positive; };
    std::optional<HitResult> hitTest(const QPointF& pos) const;

    Viewer3D* m_viewer;

    float m_elevation = 30.0f;
    float m_azimuth   = -135.0f;

    QFont m_labelFont;
    QString m_hoveredKey;
    bool    m_hoveringBg = false;

    std::optional<HitResult> m_pressedHit;
    std::optional<QPointF>   m_lastMousePos;

    static constexpr int   SIZE           = 90;
    static constexpr float AXIS_LENGTH    = 0.65f;
    static constexpr float BASE_RADIUS    = 7.0f;
    static constexpr float BG_RADIUS_RATIO= 0.85f;
    static constexpr float CAMERA_DIST   = 6.0f;

    static const std::array<AxisDef, 3> AXES;
};
