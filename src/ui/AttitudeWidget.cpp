#include "AttitudeWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPolygonF>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>

// ============================================================
// AttitudeWidget.cpp — 姿态立方体部件实现
// ============================================================

// ── CubePaintWidget 静态成员初始化 ────────────────────────────

// 立方体 8 个顶点（边长 1.4，以原点为中心）
const std::array<std::array<double,3>, 8> CubePaintWidget::VERTICES = {{
    {-0.7, -0.7, -0.7}, { 0.7, -0.7, -0.7}, { 0.7,  0.7, -0.7}, {-0.7,  0.7, -0.7},
    {-0.7, -0.7,  0.7}, { 0.7, -0.7,  0.7}, { 0.7,  0.7,  0.7}, {-0.7,  0.7,  0.7},
}};

// 6 个面（每面 4 顶点索引，顺序为正面绕序）
const std::array<std::array<int,4>, 6> CubePaintWidget::FACES = {{
    {4,5,6,7}, {1,0,3,2}, {1,2,6,5}, {0,4,7,3}, {3,7,6,2}, {0,1,5,4}
}};

// 12 条棱
const std::array<std::array<int,2>, 12> CubePaintWidget::EDGES = {{
    {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
}};

// 6 个面的颜色（深蓝/浅蓝/深红/浅红/深绿/浅绿）
const std::array<QColor, 6> CubePaintWidget::FACE_COLORS = {{
    QColor(64,64,242),  QColor(38,38,140),
    QColor(242,64,64),  QColor(140,38,38),
    QColor(64,242,64),  QColor(38,140,38),
}};

// ── CubePaintWidget 实现 ──────────────────────────────────────

CubePaintWidget::CubePaintWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    // 初始化为单位旋转矩阵
    m_rot = {1,0,0, 0,1,0, 0,0,1};
}

// 将 3D 点经旋转、相机变换后投影到 2D 屏幕坐标
std::pair<QPointF, double> CubePaintWidget::project(double px, double py, double pz) const {
    // 应用旋转矩阵
    double rx = m_rot[0]*px + m_rot[1]*py + m_rot[2]*pz;
    double ry = m_rot[3]*px + m_rot[4]*py + m_rot[5]*pz;
    double rz = m_rot[6]*px + m_rot[7]*py + m_rot[8]*pz;

    // 方位角旋转（绕 Z 轴）
    double cosA = std::cos(CAM_AZIMUTH), sinA = std::sin(CAM_AZIMUTH);
    double x1 = rx*cosA - ry*sinA;
    double y1 = rx*sinA + ry*cosA;
    double z1 = rz;

    // 仰角旋转（绕新 X 轴）
    double sinE = std::sin(CAM_ELEVATION), cosE = std::cos(CAM_ELEVATION);
    double x2 = x1;
    double y2 = y1*sinE + z1*cosE;
    double z2 = -y1*cosE + z1*sinE;

    // 透视投影
    double denom = std::max(CAM_DISTANCE - z2, 0.2);
    double scale = (width() * 0.25) * (CAM_DISTANCE / denom);
    double cx = width()  / 2.0;
    double cy = height() / 2.0;
    return { QPointF(cx + x2*scale, cy - y2*scale), z2 };
}

// 绘制立方体（面深度排序 + 绘制棱）
void CubePaintWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 绘制半透明背景
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(50, 50, 50, 100));
    p.drawRect(rect());

    // 计算所有顶点投影位置
    std::array<QPointF, 8> proj2d;
    std::array<double, 8>  projZ;
    for (int i = 0; i < 8; ++i) {
        auto [pos, z] = project(VERTICES[i][0], VERTICES[i][1], VERTICES[i][2]);
        proj2d[i] = pos;
        projZ[i]  = z;
    }

    // 按平均深度排序面（画家算法：从远到近）
    std::array<int, 6> faceOrder = {0,1,2,3,4,5};
    std::sort(faceOrder.begin(), faceOrder.end(), [&](int a, int b) {
        double da = 0, db = 0;
        for (int idx : FACES[a]) da += projZ[idx];
        for (int idx : FACES[b]) db += projZ[idx];
        return da < db; // 深度小（更远）先画
    });

    // 绘制面
    for (int fi : faceOrder) {
        QPolygonF poly;
        for (int vi : FACES[fi]) poly << proj2d[vi];
        p.setPen(Qt::NoPen);
        p.setBrush(FACE_COLORS[fi]);
        p.drawPolygon(poly);
    }

    // 绘制棱（半透明白色）
    p.setPen(QPen(QColor(255,255,255,76), 1.2));
    p.setBrush(Qt::NoBrush);
    for (const auto& edge : EDGES) {
        p.drawLine(proj2d[edge[0]], proj2d[edge[1]]);
    }
}

// 通过四元数设置旋转（构建旋转矩阵）
void CubePaintWidget::setRotationQuaternion(double w, double x, double y, double z) {
    double n2 = w*w + x*x + y*y + z*z;
    if (n2 < 1e-12) return;
    double inv = 1.0 / std::sqrt(n2);
    w*=inv; x*=inv; y*=inv; z*=inv;
    m_rot = {
        1-2*(y*y+z*z),  2*(x*y-w*z),    2*(x*z+w*y),
        2*(x*y+w*z),    1-2*(x*x+z*z),  2*(y*z-w*x),
        2*(x*z-w*y),    2*(y*z+w*x),    1-2*(x*x+y*y),
    };
    update();
}

// 通过欧拉角（ZYX 顺序）设置旋转
void CubePaintWidget::setRotationEuler(double rollDeg, double pitchDeg, double yawDeg) {
    double r = rollDeg  * M_PI / 180.0;
    double p = pitchDeg * M_PI / 180.0;
    double y = yawDeg   * M_PI / 180.0;
    double cr=std::cos(r), sr=std::sin(r);
    double cp=std::cos(p), sp=std::sin(p);
    double cy=std::cos(y), sy=std::sin(y);
    m_rot = {
        cy*cp,             cy*sp*sr-sy*cr,  cy*sp*cr+sy*sr,
        sy*cp,             sy*sp*sr+cy*cr,  sy*sp*cr-cy*sr,
        -sp,               cp*sr,           cp*cr,
    };
    update();
}

// ── AttitudeWidget 实现 ──────────────────────────────────────

// 四元数转欧拉角（ZYX 顺序，返回度）
std::tuple<double,double,double>
AttitudeWidget::quaternionToEuler(double w, double x, double y, double z) {
    double sinr_cosp = 2.0*(w*x + y*z);
    double cosr_cosp = 1.0 - 2.0*(x*x + y*y);
    double roll  = std::atan2(sinr_cosp, cosr_cosp);

    double sinp = 2.0*(w*y - z*x);
    sinp = std::max(-1.0, std::min(1.0, sinp));
    double pitch = std::asin(sinp);

    double siny_cosp = 2.0*(w*z + x*y);
    double cosy_cosp = 1.0 - 2.0*(y*y + z*z);
    double yaw = std::atan2(siny_cosp, cosy_cosp);

    constexpr double R2D = 180.0 / M_PI;
    return {roll*R2D, pitch*R2D, yaw*R2D};
}

// 创建一组立方体+标签布局
AttitudeWidget::CubeGroup AttitudeWidget::createCubeGroup(QLayout* parentLayout,
                                                            const QString& title) {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0,0,0,0);
    row->setSpacing(8);

    // 立方体列（标题 + 立方体）
    auto* cubeCol = new QVBoxLayout();
    cubeCol->setContentsMargins(0,0,0,0);
    cubeCol->setSpacing(0);
    auto* titleLbl = new QLabel(title);
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setFixedSize(CUBE_SIZE, 16);
    titleLbl->setStyleSheet("QLabel { color: #999999; background: transparent;"
                             " font-family: Consolas, monospace; font-size: 12px; }");
    auto* cube = new CubePaintWidget();
    cube->setFixedSize(CUBE_SIZE, CUBE_SIZE);
    cubeCol->addWidget(titleLbl);
    cubeCol->addWidget(cube);

    // 角度标签列
    auto* angleCol = new QVBoxLayout();
    angleCol->setContentsMargins(0,0,0,0);
    angleCol->setSpacing(6);
    auto* rollLbl  = new QLabel("R:   --");
    auto* pitchLbl = new QLabel("P:   --");
    auto* yawLbl   = new QLabel("Y:   --");
    const QString baseStyle = "background: transparent; padding: 0px; margin: 0px;"
                              " font-family: Consolas, monospace; font-size: 13px;";
    rollLbl->setStyleSheet("QLabel { color: #ff6b6b; " + baseStyle + " }");
    pitchLbl->setStyleSheet("QLabel { color: #69db7c; " + baseStyle + " }");
    yawLbl->setStyleSheet("QLabel { color: #4dabf7; " + baseStyle + " }");
    for (QLabel* l : {rollLbl, pitchLbl, yawLbl}) l->setFixedWidth(ANGLE_COL_W);
    angleCol->addSpacing(16);
    angleCol->addStretch();
    angleCol->addWidget(rollLbl);
    angleCol->addWidget(pitchLbl);
    angleCol->addWidget(yawLbl);
    angleCol->addStretch();

    row->addLayout(cubeCol);
    row->addLayout(angleCol);
    static_cast<QBoxLayout*>(parentLayout)->addLayout(row);

    return {cube, rollLbl, pitchLbl, yawLbl};
}

// 构造 AttitudeWidget：三组立方体垂直排列
AttitudeWidget::AttitudeWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4,2,4,2);
    layout->setSpacing(0);

    m_raw      = createCubeGroup(layout, "Raw");
    m_madgwick = createCubeGroup(layout, "Madgwick");
    m_mahony   = createCubeGroup(layout, "Mahony");

    int groupH = 16 + CUBE_SIZE;
    setFixedSize(WIDGET_WIDTH, 2 + 3*groupH + 2);
    setVisible(false);
}

// 更新一组标签文字（R/P/Y 各带符号和度符号）
void AttitudeWidget::setEulerLabels(const CubeGroup& g, double roll, double pitch, double yaw) {
    g.rollLbl->setText(QString("R:%1°").arg(roll,  +6, 'f', 1));
    g.pitchLbl->setText(QString("P:%1°").arg(pitch, +6, 'f', 1));
    g.yawLbl->setText(QString("Y:%1°").arg(yaw,   +6, 'f', 1));
}

// 更新原始四元数
void AttitudeWidget::updateQuaternion(double q0, double q1, double q2, double q3) {
    m_hasData = true;
    m_raw.cube->setRotationQuaternion(q0, q1, q2, q3);
    auto [r,p,y] = quaternionToEuler(q0, q1, q2, q3);
    setEulerLabels(m_raw, r, p, y);
}

// 更新原始欧拉角
void AttitudeWidget::updateEuler(double roll, double pitch, double yaw) {
    m_hasData = true;
    m_raw.cube->setRotationEuler(roll, pitch, yaw);
    setEulerLabels(m_raw, roll, pitch, yaw);
}

// 更新 Madgwick 四元数
void AttitudeWidget::updateMadgwickQuaternion(double q0, double q1, double q2, double q3) {
    m_hasData = true;
    m_madgwick.cube->setRotationQuaternion(q0, q1, q2, q3);
    auto [r,p,y] = quaternionToEuler(q0, q1, q2, q3);
    setEulerLabels(m_madgwick, r, p, y);
}

// 更新 Mahony 四元数
void AttitudeWidget::updateMahonyQuaternion(double q0, double q1, double q2, double q3) {
    m_hasData = true;
    m_mahony.cube->setRotationQuaternion(q0, q1, q2, q3);
    auto [r,p,y] = quaternionToEuler(q0, q1, q2, q3);
    setEulerLabels(m_mahony, r, p, y);
}

// 重置所有姿态数据
void AttitudeWidget::reset() {
    m_hasData = false;
    for (const CubeGroup* g : {&m_raw, &m_madgwick, &m_mahony}) {
        g->rollLbl->setText("R:   --");
        g->pitchLbl->setText("P:   --");
        g->yawLbl->setText("Y:   --");
    }
}

// 鼠标事件穿透（交由下方 3D 视图处理）
void AttitudeWidget::mousePressEvent(QMouseEvent* ev)  { ev->ignore(); }
void AttitudeWidget::mouseReleaseEvent(QMouseEvent* ev){ ev->ignore(); }
void AttitudeWidget::mouseMoveEvent(QMouseEvent* ev)   { ev->ignore(); }
void AttitudeWidget::wheelEvent(QWheelEvent* ev)       { ev->ignore(); }
