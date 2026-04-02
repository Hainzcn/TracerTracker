#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>
#include <QColor>
#include <QString>
#include <QMap>
#include <deque>
#include <vector>

// ============================================================
// TrackRenderer.h — 轨迹与点渲染器
//
// 功能：
//   - 每个命名点维护位置历史（deque，可配置长度）
//   - 全路径模式：绘制完整历史轨迹线
//   - 速度尾迹模式：按速度大小渐变着色（蓝→青→绿→黄→红）
//   - 当前位置散点渲染
//   - 路径降采样（角度阈值过滤冗余中间点）
// ============================================================

class TrackRenderer {
public:
    // 构造：需要 QOpenGLFunctions_3_3_Core 指针
    explicit TrackRenderer(QOpenGLFunctions_3_3_Core* gl);
    ~TrackRenderer();

    // 在 Viewer3D::initializeGL() 中调用：初始化 VBO 和着色器
    void initialize();

    // 更新或添加一个命名点的位置（由 Viewer3D::updatePoint 调用）
    //   name   - 点的唯一标识符
    //   x,y,z  - 世界坐标
    //   color  - RGBA 颜色（[0-255]）
    //   size   - 点的像素大小
    void updatePoint(const QString& name, double x, double y, double z,
                     const QColor& color, int size);

    // 清除所有点和轨迹历史
    void clearAll();

    // 设置是否显示完整历史路径（全路径模式）
    void setFullPathMode(bool enabled);

    // 设置是否启用速度尾迹（按速度着色）
    void setTrailMode(bool enabled);

    // 设置轨迹历史长度（最多保留 length 个历史位置）
    void setTrailLength(int length);

    // 在 paintGL() 中调用：绘制所有点和轨迹
    void render(const QMatrix4x4& mvpMatrix);

    // 返回所有点位置数据（用于 auto_fit_view 计算最大距离）
    struct PointData {
        std::deque<QVector3D> history; // 历史位置
        QColor color;                  // 点颜色
        int    size;                   // 点大小
    };
    const QMap<QString, PointData>& points() const { return m_points; }

private:
    QOpenGLFunctions_3_3_Core* m_gl;     // OpenGL 函数指针

    // 线段着色器（带颜色插值）
    QOpenGLShaderProgram m_lineShader;
    // 点渲染着色器（支持 gl_PointSize）
    QOpenGLShaderProgram m_pointShader;

    QOpenGLBuffer            m_vbo;       // 顶点缓冲
    QOpenGLVertexArrayObject m_vao;       // 顶点数组对象

    bool m_initialized    = false;
    bool m_fullPathMode   = false;  // 全路径显示开关
    bool m_trailMode      = false;  // 速度尾迹显示开关
    int  m_trailLength    = 120;    // 历史长度

    QMap<QString, PointData> m_points;  // 各命名点的数据

    // 线段顶点格式：[x,y,z, r,g,b,a]
    struct Vertex { float x, y, z, r, g, b, a; };

    // 将速度标量映射到颜色（蓝→青→绿→黄→红，0.0=最低，1.0=最高）
    static QVector4D velocityToColor(float t);

    // 对路径进行角度阈值降采样（过滤方向变化极小的中间点）
    static std::vector<QVector3D> downsamplePath(const std::deque<QVector3D>& path,
                                                   float angleDegThreshold = 2.0f);

    // 着色器源码（GLSL 3.30）
    static const char* LINE_VERT_SRC;
    static const char* LINE_FRAG_SRC;
    static const char* POINT_VERT_SRC;
    static const char* POINT_FRAG_SRC;
};
