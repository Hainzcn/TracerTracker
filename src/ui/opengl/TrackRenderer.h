#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>
#include <QColor>
#include <QString>
#include <QHash>
#include <deque>
#include <vector>

// ============================================================
// TrackRenderer.h — 轨迹与点渲染器
//
// 功能：
//   - 每个命名点维护完整位置历史（deque，超出阈值自动压缩）
//   - 绘制路径模式：渲染完整历史轨迹线（角度降采样以控制顶点数）
//   - 路径着色模式：依赖绘制路径，按相邻点距对全路径做颜色映射
//                    （蓝→青→绿→黄→红，归一化基准为动态最大段距）
//   - 当前位置散点渲染
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

    // 设置是否绘制完整历史路径
    void setFullPathMode(bool enabled);

    // 设置是否对路径按速度着色（仅在绘制路径开启时可见）
    void setPathColorMode(bool enabled);

    // 在 paintGL() 中调用：绘制所有点和轨迹
    void render(const QMatrix4x4& mvpMatrix);

    // 返回所有点位置数据（用于 auto_fit_view 计算最大距离）
    struct PointData {
        std::deque<QVector3D> history;     // 历史位置
        QColor color;                      // 点颜色
        int    size;                       // 点大小
        float  maxSampleSpeed = 1e-6f;     // 相邻历史点距的动态最大值（着色归一化基准）
    };
    const QHash<QString, PointData>& points() const { return m_points; }

private:
    QOpenGLFunctions_3_3_Core* m_gl;     // OpenGL 函数指针

    // 线段着色器（带颜色插值）
    QOpenGLShaderProgram m_lineShader;
    // 点渲染着色器（支持 gl_PointSize）
    QOpenGLShaderProgram m_pointShader;

    QOpenGLBuffer            m_lineVbo;   // 线段顶点缓冲
    QOpenGLVertexArrayObject m_lineVao;   // 线段顶点数组
    QOpenGLBuffer            m_pointVbo;  // 点顶点缓冲
    QOpenGLVertexArrayObject m_pointVao;  // 点顶点数组

    bool m_initialized    = false;
    bool m_fullPathMode   = false;  // 是否绘制完整历史路径
    bool m_pathColorMode  = false;  // 是否对路径按速度着色（依赖 m_fullPathMode）

    QHash<QString, PointData> m_points;  // 各命名点的数据
    bool m_dirty = false;               // 数据变更标记，延迟到 render 时重建

    // 线段顶点格式：[x,y,z, r,g,b,a]
    struct Vertex { float x, y, z, r, g, b, a; };

    // 缓存的顶点数组（仅在 dirty 时重建）
    std::vector<Vertex> m_cachedLineVerts;
    struct CachedPoint { Vertex v; float size; };
    std::vector<CachedPoint> m_cachedPoints;
    bool m_vboNeedsUpload = false;

    // 将速度标量映射到颜色（蓝→青→绿→黄→红，0.0=最低，1.0=最高）
    static QVector4D velocityToColor(float t);

    // 对路径进行角度阈值降采样（过滤方向变化极小的中间点）
    static std::vector<QVector3D> downsamplePath(const std::deque<QVector3D>& path,
                                                   float angleDegThreshold = 2.0f);

    // 全路径历史管理参数
    static constexpr int FULL_PATH_RAW_MAX = 200000;  // 触发压缩的原始点上限
    static constexpr int RECENT_PRESERVE   = 20000;   // 压缩时保留最新点数（全分辨率）
    static constexpr float COMPACT_ANGLE_DEG = 3.0f;  // 压缩用角度阈值（比渲染更激进）

    // 当历史超过 FULL_PATH_RAW_MAX 时，对旧部分进行角度降采样压缩
    void compactHistory(PointData& pd);

    // 数据变更时重建缓存的顶点数组
    void rebuildCache();

    // 着色器源码（GLSL 3.30）
    static const char* LINE_VERT_SRC;
    static const char* LINE_FRAG_SRC;
    static const char* POINT_VERT_SRC;
    static const char* POINT_FRAG_SRC;
};
