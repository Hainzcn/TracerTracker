#pragma once
#include <QWidget>
#include <QLabel>
#include <array>

// ============================================================
// AttitudeWidget.h — 姿态显示叠加组件
//
// 在 3D 视口左侧浮动显示三个 3D 姿态立方体：
//   - Raw（原始四元数或欧拉角）
//   - Madgwick 滤波四元数
//   - Mahony 滤波四元数
// 每个立方体旁显示 Roll/Pitch/Yaw 文字
// ============================================================

// 单个 3D 立方体绘制子部件（QPainter 手动投影）
class CubePaintWidget : public QWidget {
    Q_OBJECT
public:
    explicit CubePaintWidget(QWidget* parent = nullptr);

    // 通过四元数 [w,x,y,z] 设置旋转
    void setRotationQuaternion(double w, double x, double y, double z);

    // 通过欧拉角（ZYX 顺序，度）设置旋转
    void setRotationEuler(double rollDeg, double pitchDeg, double yawDeg);

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    // 旋转矩阵（3×3，行优先）
    std::array<double, 9> m_rot;

    // 将局部 3D 点投影到 2D 屏幕坐标，返回 (screenPos, depthZ)
    std::pair<QPointF, double> project(double px, double py, double pz) const;

    // 立方体顶点（单位边长 1.4）
    static const std::array<std::array<double,3>, 8> VERTICES;
    // 六个面（各面 4 个顶点索引）
    static const std::array<std::array<int,4>, 6> FACES;
    // 12 条边（各边 2 个顶点索引）
    static const std::array<std::array<int,2>, 12> EDGES;
    // 六个面的颜色
    static const std::array<QColor, 6> FACE_COLORS;

    // 相机参数
    static constexpr double CAM_DISTANCE  = 3.6;
    static constexpr double CAM_ELEVATION = 25.0 * M_PI / 180.0;  // 弧度
    static constexpr double CAM_AZIMUTH   = -45.0 * M_PI / 180.0; // 弧度
};

// ============================================================
// AttitudeWidget — 包含三组立方体的面板
// ============================================================

class AttitudeWidget : public QWidget {
    Q_OBJECT
public:
    explicit AttitudeWidget(QWidget* parent = nullptr);

    // 尺寸常量（供外部布局使用）
    static int cubeSize()     { return CUBE_SIZE; }
    static int totalWidth()   { return WIDGET_WIDTH; }
    static std::array<int,4> contentMargins() { return {4,2,4,2}; }

    // 四元数 → 欧拉角（ZYX 顺序，返回度）
    static std::tuple<double,double,double> quaternionToEuler(double w, double x, double y, double z);

public slots:
    // 更新原始四元数（第一个立方体）
    void updateQuaternion(double q0, double q1, double q2, double q3);
    // 更新原始欧拉角（第一个立方体）
    void updateEuler(double roll, double pitch, double yaw);
    // 更新 Madgwick 滤波四元数（第二个立方体）
    void updateMadgwickQuaternion(double q0, double q1, double q2, double q3);
    // 更新 Mahony 滤波四元数（第三个立方体）
    void updateMahonyQuaternion(double q0, double q1, double q2, double q3);
    // 重置所有姿态数据
    void reset();

protected:
    // 让鼠标事件穿透到下方 3D 视图
    void mousePressEvent(QMouseEvent* ev)  override;
    void mouseReleaseEvent(QMouseEvent* ev)override;
    void mouseMoveEvent(QMouseEvent* ev)   override;
    void wheelEvent(QWheelEvent* ev)       override;

private:
    static constexpr int CUBE_SIZE    = 90;
    static constexpr int ANGLE_COL_W  = 80;
    static constexpr int WIDGET_WIDTH = ANGLE_COL_W + CUBE_SIZE + 10;

    // 一组立方体和其对应的欧拉角标签
    struct CubeGroup {
        CubePaintWidget* cube;
        QLabel* rollLbl;
        QLabel* pitchLbl;
        QLabel* yawLbl;
    };

    CubeGroup m_raw, m_madgwick, m_mahony;
    bool m_hasData = false;

    // 创建一组立方体部件并添加到布局
    CubeGroup createCubeGroup(QLayout* parentLayout, const QString& title);
    // 更新一组标签的文字
    static void setEulerLabels(const CubeGroup& g, double roll, double pitch, double yaw);
};
