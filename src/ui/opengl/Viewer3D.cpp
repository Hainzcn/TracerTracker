#include "Viewer3D.h"
#include "GridRenderer.h"
#include "TrackRenderer.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QDateTime>
#include <cmath>

// ============================================================
// Viewer3D.cpp — 3D OpenGL 视口实现
// ============================================================

Viewer3D::Viewer3D(QWidget* parent)
    : QOpenGLWidget(parent)
{
    // 启用深度测试格式
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4); // 4x MSAA 抗锯齿
    setFormat(fmt);

    setFocusPolicy(Qt::StrongFocus);

    // 缩放动画定时器（每 16ms ≈ 60fps）
    m_zoomAnimTimer.setInterval(16);
    connect(&m_zoomAnimTimer, &QTimer::timeout, this, &Viewer3D::updateZoomAnimation);

    // 正交/透视混合动画定时器
    m_orthoAnimTimer.setInterval(16);
    connect(&m_orthoAnimTimer, &QTimer::timeout, this, &Viewer3D::updateOrthoAnimation);

    // 相机动画定时器
    m_cameraAnimTimer.setInterval(16);
    connect(&m_cameraAnimTimer, &QTimer::timeout, this, &Viewer3D::updateCameraAnimation);

    // R 键长按定时器（1 秒恢复视角，2 秒恢复全部）
    m_resetViewTimer.setSingleShot(true);
    m_resetViewTimer.setInterval(1000);
    connect(&m_resetViewTimer, &QTimer::timeout, this, &Viewer3D::onResetViewTimeout);

    m_resetAllTimer.setSingleShot(true);
    m_resetAllTimer.setInterval(2000);
    connect(&m_resetAllTimer, &QTimer::timeout, this, &Viewer3D::onResetAllTimeout);
}

Viewer3D::~Viewer3D() {
    makeCurrent();
    delete m_gridRenderer;
    delete m_trackRenderer;
    doneCurrent();
}

// ── OpenGL 初始化 ─────────────────────────────────────────────

// 初始化 OpenGL 上下文和子渲染器
void Viewer3D::initializeGL() {
    initializeOpenGLFunctions();

    // 设置 3D 场景背景色（深黑色）
    glClearColor(0.071f, 0.071f, 0.071f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 初始化子渲染器（在 OpenGL 上下文内创建 VBO/VAO/Shader）
    m_gridRenderer  = new GridRenderer(this);
    m_trackRenderer = new TrackRenderer(this);
    m_gridRenderer->initialize();
    m_trackRenderer->initialize();

    m_gridRenderer->update(m_distance, m_sceneScale, m_elevation, m_azimuth, m_orthoBlend);
}

// 视口大小变化时更新参数
void Viewer3D::resizeGL(int w, int h) {
    m_viewportW = w;
    m_viewportH = h;
    glViewport(0, 0, w, h);
}

// 每帧渲染（OpenGL 内容 + QPainter overlay）
void Viewer3D::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 计算 MVP 矩阵
    QMatrix4x4 proj = computeProjectionMatrix();
    QMatrix4x4 view = computeViewMatrix();
    QMatrix4x4 mvp  = proj * view;

    // 渲染网格和坐标轴
    m_gridRenderer->render(mvp);

    // 渲染轨迹和点
    m_trackRenderer->render(mvp);

    // ── QPainter Overlay（文字标签）────────────────────────────
    // 注意：QPainter 必须在 OpenGL 绘制完成后创建，且需要调用
    // beginNativePainting/endNativePainting 保护 OpenGL 状态
    QPainter painter(this);
    painter.beginNativePainting();
    painter.endNativePainting();

    // 绘制坐标轴文字标签
    m_gridRenderer->renderLabels(&painter, mvp, m_viewportW, m_viewportH);
    painter.end();
}

// ── 矩阵计算 ──────────────────────────────────────────────────

QMatrix4x4 Viewer3D::computeViewMatrix() const {
    QMatrix4x4 m;
    m.translate(m_panX, m_panY, 0.0f);
    m.translate(0.0f, 0.0f, -float(m_distance));
    m.rotate(float(m_elevation - 90.0), 1.0f, 0.0f, 0.0f);
    m.rotate(float(m_azimuth), 0.0f, 0.0f, 1.0f);
    float s = m_sceneScale;
    m.scale(s, s, s);
    m.translate(-m_center);
    return m;
}

// 计算投影矩阵（支持透视/正交插值混合）
QMatrix4x4 Viewer3D::computeProjectionMatrix() const {
    float aspect = (m_viewportH > 0) ? float(m_viewportW) / m_viewportH : 1.0f;
    float nearClip = float(m_distance) * 0.001f;
    float farClip  = float(m_distance) * 1000.0f;
    if (nearClip < 0.001f) nearClip = 0.001f;

    if (m_orthoBlend <= 0.0f) {
        // 纯透视投影
        QMatrix4x4 p;
        p.perspective(m_fov, aspect, nearClip, farClip);
        return p;
    }
    if (m_orthoBlend >= 1.0f) {
        // 纯正交投影（等价缩放使场景大小与透视视图一致）
        float halfH = float(m_distance) * std::tan(m_fov * 0.5f * float(M_PI) / 180.0f);
        float halfW = halfH * aspect;
        QMatrix4x4 p;
        p.ortho(-halfW, halfW, -halfH, halfH, nearClip, farClip);
        return p;
    }

    // Homogeneous-scale blending (matching Python original):
    // The ortho matrix has constant w=1 while perspective has z-dependent w.
    // Naive element-wise lerp produces scale-inconsistent intermediate matrices.
    // Pre-scale the ortho matrix by dist to match the perspective matrix's
    // homogeneous magnitude, then blend.
    QMatrix4x4 perspM, orthoM;
    perspM.perspective(m_fov, aspect, nearClip, farClip);
    float halfH = float(m_distance) * std::tan(m_fov * 0.5f * float(M_PI) / 180.0f);
    float halfW = halfH * aspect;
    orthoM.ortho(-halfW, halfW, -halfH, halfH, nearClip, farClip);

    float b = m_orthoBlend;
    float dist = std::max(float(m_distance), 1e-6f);

    float p[16], o[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            p[r * 4 + c] = perspM(r, c);
            o[r * 4 + c] = orthoM(r, c) * dist;
        }

    float blended[16];
    for (int i = 0; i < 16; ++i)
        blended[i] = p[i] + (o[i] - p[i]) * b;
    return QMatrix4x4(blended);
}

// ── 轨道相机旋转 ─────────────────────────────────────────────

// 处理鼠标左键拖动的轨道旋转
void Viewer3D::orbit(int dx, int dy) {
    m_azimuth   += dx * 0.5;
    m_elevation += dy * 0.5;
    // 仰角限制在 [-89, 89] 度，避免万向节死锁
    m_elevation = std::max(-89.0, std::min(89.0, m_elevation));
}

// ── 鼠标事件 ──────────────────────────────────────────────────

void Viewer3D::mousePressEvent(QMouseEvent* ev) {
    setFocus();
    m_lastMousePos = ev->pos();

    if (ev->button() == Qt::LeftButton || ev->button() == Qt::RightButton) {
        if (m_cameraAnimTimer.isActive()) m_cameraAnimTimer.stop();
        ev->accept();
    }
}

void Viewer3D::mouseReleaseEvent(QMouseEvent* ev) {
    Q_UNUSED(ev);
}

void Viewer3D::mouseMoveEvent(QMouseEvent* ev) {
    QPoint curr = ev->pos();
    int dx = curr.x() - m_lastMousePos.x();
    int dy = curr.y() - m_lastMousePos.y();
    m_lastMousePos = curr;

    if (ev->buttons() & Qt::LeftButton) {
        orbit(dx, dy);
        m_gridRenderer->update(m_distance, m_sceneScale, m_elevation, m_azimuth, m_orthoBlend);
        emit cameraChanged();
        update();
    } else if (ev->buttons() & Qt::RightButton) {
        float scale = float(m_distance) * 0.001f;
        m_panX += dx * scale;
        m_panY -= dy * scale;
        update();
    }
}

void Viewer3D::wheelEvent(QWheelEvent* ev) {
    if (m_cameraAnimTimer.isActive()) m_cameraAnimTimer.stop();

    int delta = ev->angleDelta().y();
    float factor = (delta > 0) ? 1.1f : (1.0f / 1.1f);
    m_targetSceneScale *= factor;
    if (!m_zoomAnimTimer.isActive()) m_zoomAnimTimer.start();
    ev->accept();
}

void Viewer3D::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_R && !ev->isAutoRepeat()) {
        m_rKeyHeld       = true;
        m_resetViewFired = false;
        m_resetViewTimer.start();
        m_resetAllTimer.start();
        ev->accept();
    } else {
        QOpenGLWidget::keyPressEvent(ev);
    }
}

void Viewer3D::keyReleaseEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_R && !ev->isAutoRepeat()) {
        m_rKeyHeld = false;
        m_resetViewTimer.stop();
        m_resetAllTimer.stop();
        ev->accept();
    } else {
        QOpenGLWidget::keyReleaseEvent(ev);
    }
}

void Viewer3D::onResetViewTimeout() {
    if (!m_rKeyHeld) return;
    m_resetViewFired = true;
    startResetAnimation(false);
}

void Viewer3D::onResetAllTimeout() {
    if (!m_rKeyHeld) return;
    startResetAnimation(true);
}

// ── 动画 ──────────────────────────────────────────────────────

void Viewer3D::updateZoomAnimation() {
    float diff = m_targetSceneScale - m_sceneScale;
    float relDiff = std::abs(diff) / std::max(m_sceneScale, 1e-9f);
    if (relDiff < 1e-4f) {
        m_sceneScale = m_targetSceneScale;
        m_zoomAnimTimer.stop();
    } else {
        m_sceneScale += diff * 0.28f;
    }
    m_gridRenderer->update(m_distance, m_sceneScale, m_elevation, m_azimuth, m_orthoBlend);
    update();
}

// 正交/透视混合动画（指数插值）
void Viewer3D::updateOrthoAnimation() {
    float target = m_useOrtho ? 1.0f : 0.0f;
    float diff   = target - m_orthoBlend;
    if (std::abs(diff) < 1e-4f) {
        m_orthoBlend = target;
        m_orthoAnimTimer.stop();
    } else {
        m_orthoBlend += diff * 0.18f;
    }
    m_gridRenderer->update(m_distance, m_sceneScale, m_elevation, m_azimuth, m_orthoBlend);
    emit cameraChanged();
    update();
}

void Viewer3D::updateCameraAnimation() {
    qint64 now     = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed = now - m_animStartTime;
    float  t       = float(elapsed) / m_animDuration;
    if (t >= 1.0f) {
        t = 1.0f;
        m_cameraAnimTimer.stop();
    }
    float ease = 1.0f - (1.0f - t) * (1.0f - t);

    auto lerp = [](double a, double b, float e) { return a + (b - a) * e; };
    auto lerpf= [](float  a, float  b, float e) { return a + (b - a) * e; };

    m_distance   = lerp(m_animStart.distance,   m_animTarget.distance,   ease);
    m_elevation  = lerp(m_animStart.elevation,  m_animTarget.elevation,  ease);
    m_azimuth    = lerp(m_animStart.azimuth,    m_animTarget.azimuth,    ease);
    m_panX       = lerpf(float(m_animStart.panX), float(m_animTarget.panX), ease);
    m_panY       = lerpf(float(m_animStart.panY), float(m_animTarget.panY), ease);
    m_sceneScale = lerpf(float(m_animStart.sceneScale), float(m_animTarget.sceneScale), ease);
    m_targetSceneScale = m_sceneScale;

    m_gridRenderer->update(m_distance, m_sceneScale, m_elevation, m_azimuth, m_orthoBlend);
    emit cameraChanged();
    update();
}

// 切换透视/正交投影（触发混合动画）
void Viewer3D::toggleProjection() {
    m_useOrtho = !m_useOrtho;
    if (!m_orthoAnimTimer.isActive()) m_orthoAnimTimer.start();
    emit projectionModeChanged(m_useOrtho);
}

void Viewer3D::autoFitView() {
    const auto& pts = m_trackRenderer->points();
    if (pts.isEmpty()) return;

    float maxDist = 0.0f;
    for (auto it = pts.cbegin(); it != pts.cend(); ++it) {
        for (const QVector3D& p : it.value().history) {
            maxDist = std::max(maxDist, p.length());
        }
    }

    double targetDist = (maxDist < 1.0f) ? INIT_DISTANCE : maxDist * 3.5;

    m_animStart  = {m_distance, m_elevation, m_azimuth, m_panX, m_panY, m_sceneScale};
    m_animTarget = {targetDist, m_elevation, m_azimuth, INIT_PAN_X, INIT_PAN_Y, INIT_SCENE_SCALE};
    m_animStartTime = QDateTime::currentMSecsSinceEpoch();
    if (!m_cameraAnimTimer.isActive()) m_cameraAnimTimer.start();
}

void Viewer3D::startResetAnimation(bool fullReset) {
    if (m_zoomAnimTimer.isActive()) m_zoomAnimTimer.stop();

    double targetDist  = fullReset ? INIT_DISTANCE : m_distance;
    float  targetScale = fullReset ? INIT_SCENE_SCALE : m_sceneScale;
    m_targetSceneScale = targetScale;

    double diffAzim = INIT_AZIMUTH - m_azimuth;
    diffAzim = std::fmod(diffAzim + 180.0, 360.0) - 180.0;

    m_animStart  = {m_distance, m_elevation, m_azimuth, m_panX, m_panY, m_sceneScale};
    m_animTarget = {targetDist, INIT_ELEVATION, m_azimuth + diffAzim,
                    INIT_PAN_X, INIT_PAN_Y, targetScale};
    m_animStartTime = QDateTime::currentMSecsSinceEpoch();
    if (!m_cameraAnimTimer.isActive()) m_cameraAnimTimer.start();
}

void Viewer3D::animateToView(double elevation, double azimuth) {
    if (m_zoomAnimTimer.isActive()) m_zoomAnimTimer.stop();

    double diffAzim = azimuth - m_azimuth;
    diffAzim = std::fmod(diffAzim + 180.0, 360.0) - 180.0;

    m_animStart  = {m_distance, m_elevation, m_azimuth, m_panX, m_panY, m_sceneScale};
    m_animTarget = {m_distance, elevation, m_azimuth + diffAzim, m_panX, m_panY, m_sceneScale};
    m_targetSceneScale = m_sceneScale;
    m_animStartTime = QDateTime::currentMSecsSinceEpoch();
    if (!m_cameraAnimTimer.isActive()) m_cameraAnimTimer.start();
}

// ── 公有接口委托 ──────────────────────────────────────────────

void Viewer3D::updatePoint(const QString& name, double x, double y, double z,
                             const QColor& color, int size) {
    if (m_trackRenderer) m_trackRenderer->updatePoint(name, x, y, z, color, size);
    update();
}

void Viewer3D::clearAll() {
    if (m_trackRenderer) m_trackRenderer->clearAll();
    update();
}

void Viewer3D::setFullPathMode(bool e) { if (m_trackRenderer) m_trackRenderer->setFullPathMode(e); update(); }
void Viewer3D::setTrailMode(bool e)    { if (m_trackRenderer) m_trackRenderer->setTrailMode(e);    update(); }
void Viewer3D::setTrailLength(int l)   { if (m_trackRenderer) m_trackRenderer->setTrailLength(l);  update(); }

void Viewer3D::setRenderDebugOptions(bool enabled, bool verbosePointUpdates) {
    m_renderDebugEnabled        = enabled;
    m_renderDebugVerboseUpdates = verbosePointUpdates;
}
