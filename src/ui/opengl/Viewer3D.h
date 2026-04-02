#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPoint>
#include <QTimer>
#include <QColor>
#include <QString>
#include <QVariantMap>

// 前向声明
class GridRenderer;
class TrackRenderer;

// ============================================================
// Viewer3D.h — 3D OpenGL 视口组件
//
// 继承自 QOpenGLWidget，自行管理轨道相机和投影矩阵。
// 不依赖 pyqtgraph，使用原生 OpenGL 3.3 Core Profile。
//
// 相机模型（与 Python 版保持一致）：
//   distance  - 相机到目标的距离
//   elevation - 仰角（度），0=水平，90=正上方
//   azimuth   - 方位角（度）
//   center    - 目标中心点
//   panOffset - 平移偏移（屏幕空间 XY）
// ============================================================

class Viewer3D : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit Viewer3D(QWidget* parent = nullptr);
    ~Viewer3D();

    // ── 公有接口（委托给子渲染器）────────────────────────────

    // 更新或添加一个命名点的位置
    void updatePoint(const QString& name, double x, double y, double z,
                     const QColor& color, int size);

    // 清除所有点和轨迹
    void clearAll();

    // 全路径模式开关
    void setFullPathMode(bool enabled);

    // 速度尾迹模式开关
    void setTrailMode(bool enabled);

    // 设置轨迹历史长度
    void setTrailLength(int length);

    // 切换透视/正交投影（触发动画）
    void toggleProjection();

    // 自动调整相机距离以适应所有点
    void autoFitView();

    // 动画过渡到指定仰角/方位角（由 ViewGizmo 驱动）
    void animateToView(double elevation, double azimuth);

    // 启动复位动画（full_reset=true 时同时复位距离和缩放）
    void startResetAnimation(bool fullReset = true);

    // 吸附到最近的标准正交视图（最小旋转角，一轴水平一轴垂直）
    void snapToNearestStdView();

    // 设置渲染调试选项
    void setRenderDebugOptions(bool enabled, bool verbosePointUpdates);

    // 获取相机参数（供外部查询）
    double cameraDistance()  const { return m_distance;  }
    double cameraElevation() const { return m_elevation; }
    double cameraAzimuth()   const { return m_azimuth;   }
    bool   isOrtho()         const { return m_useOrtho;  }
    float  orthoBlend()      const { return m_orthoBlend; }

    // 以 QVariantMap 形式返回相机参数（供 ViewOrientationGizmo 使用）
    QVariantMap cameraParams() const {
        QVariantMap m;
        m["elevation"] = m_elevation;
        m["azimuth"]   = m_azimuth;
        m["distance"]  = m_distance;
        return m;
    }

signals:
    // 渲染调试日志
    void logMessage(const QString& msg);
    // 相机参数变化（通知 ViewGizmo 更新显示）
    void cameraChanged();
    // 投影模式变化（true=正交，false=透视）
    void projectionModeChanged(bool isOrtho);

protected:
    // ── QOpenGLWidget 回调 ────────────────────────────────────

    // 初始化 OpenGL 资源（着色器、VBO、子渲染器等）
    void initializeGL() override;
    // 视口大小变化时更新投影参数
    void resizeGL(int w, int h) override;
    // 每帧渲染（OpenGL + QPainter overlay）
    void paintGL() override;

    // ── 鼠标/键盘事件 ────────────────────────────────────────

    void mousePressEvent(QMouseEvent* ev)   override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev)    override;
    void wheelEvent(QWheelEvent* ev)        override;
    void keyPressEvent(QKeyEvent* ev)       override;
    void keyReleaseEvent(QKeyEvent* ev)     override;

private slots:
    // R 键长按 1 秒：恢复默认视角
    void onResetViewTimeout();
    // R 键长按 2 秒：恢复全部默认参数
    void onResetAllTimeout();

    // 缩放平滑动画更新
    void updateZoomAnimation();

    // 正交/透视混合动画更新
    void updateOrthoAnimation();

    // 相机旋转/位移动画更新
    void updateCameraAnimation();

private:
    // ── 渲染器 ────────────────────────────────────────────────
    GridRenderer*  m_gridRenderer  = nullptr;
    TrackRenderer* m_trackRenderer = nullptr;

    // ── 相机状态 ──────────────────────────────────────────────

    // 当前相机参数
    double   m_distance   = 35.0;     // 相机到目标的距离
    double   m_elevation  = 30.0;     // 仰角（度）
    double   m_azimuth    = -135.0;   // 方位角（度）
    QVector3D m_center    = {0,0,0};  // 目标中心
    float    m_panX       = 0.0f;     // 平移 X
    float    m_panY       = -5.0f;    // 平移 Y
    float    m_sceneScale = 1.0f;     // 场景缩放（围绕原点）

    // 初始相机状态（用于复位）
    const double INIT_DISTANCE    = 35.0;
    const double INIT_ELEVATION   = 30.0;
    const double INIT_AZIMUTH     = -135.0;
    const float  INIT_PAN_X       = 0.0f;
    const float  INIT_PAN_Y       = -5.0f;
    const float  INIT_SCENE_SCALE = 1.0f;

    // ── 动画状态 ──────────────────────────────────────────────

    // 缩放动画（通过修改 sceneScale 实现，围绕原点缩放）
    float  m_targetSceneScale = 1.0f;
    QTimer m_zoomAnimTimer;

    // 正交/透视混合动画
    bool   m_useOrtho    = false;
    float  m_orthoBlend  = 0.0f;
    QTimer m_orthoAnimTimer;

    // 相机旋转/平移动画
    QTimer  m_cameraAnimTimer;
    qint64  m_animStartTime   = 0; // 动画开始时间（毫秒）
    int     m_animDuration    = 800; // 动画持续时间（毫秒）
    struct CameraState {
        double distance, elevation, azimuth, panX, panY, sceneScale;
    };
    CameraState m_animStart{};
    CameraState m_animTarget{};

    // ── 鼠标交互状态 ──────────────────────────────────────────

    QPoint m_lastMousePos;     // 上次鼠标位置

    // ── R 键长按复位 ────────────────────────────────────────
    bool   m_rKeyHeld          = false;
    bool   m_resetViewFired    = false;  // 1 秒阈值已触发
    QTimer m_resetViewTimer;             // 1 秒定时器
    QTimer m_resetAllTimer;              // 2 秒定时器

    // ── 投影参数 ──────────────────────────────────────────────

    int   m_viewportW = 1;   // 当前视口宽度
    int   m_viewportH = 1;   // 当前视口高度
    float m_fov       = 60.0f; // 视场角（度）

    // ── 渲染调试 ──────────────────────────────────────────────
    bool m_renderDebugEnabled        = false;
    bool m_renderDebugVerboseUpdates = false;

    // ── 矩阵计算 ──────────────────────────────────────────────

    // 计算当前视图矩阵（轨道相机变换）
    QMatrix4x4 computeViewMatrix() const;

    // 计算当前投影矩阵（支持透视/正交插值）
    QMatrix4x4 computeProjectionMatrix() const;

public:
    // 轨道旋转（鼠标左键拖动，也可由 ViewOrientationGizmo 外部调用）
    void orbit(int dx, int dy);
};
