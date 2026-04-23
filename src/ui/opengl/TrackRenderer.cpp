#include "TrackRenderer.h"
#include <cmath>
#include <algorithm>
#include <QPainter>

// ============================================================
// TrackRenderer.cpp — 轨迹与点渲染器实现
// ============================================================

// ── GLSL 着色器源码 ───────────────────────────────────────────

// 线段顶点着色器（带颜色属性）
const char* TrackRenderer::LINE_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
uniform mat4 mvpMatrix;
out vec4 vColor;
void main() {
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vColor = color;
}
)";

// 线段片元着色器
const char* TrackRenderer::LINE_FRAG_SRC = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

// 点渲染顶点着色器（支持可变大小 gl_PointSize）
const char* TrackRenderer::POINT_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
uniform mat4 mvpMatrix;
uniform float pointSize;
out vec4 vColor;
void main() {
    gl_Position = mvpMatrix * vec4(position, 1.0);
    gl_PointSize = pointSize;
    vColor = color;
}
)";

// 点片元着色器（圆形裁剪）
const char* TrackRenderer::POINT_FRAG_SRC = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    // 将点裁剪为圆形
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    if (dot(uv, uv) > 1.0) discard;
    fragColor = vColor;
}
)";

// ── 构造与初始化 ──────────────────────────────────────────────

TrackRenderer::TrackRenderer(QOpenGLFunctions_3_3_Core* gl)
    : m_gl(gl)
    , m_lineVbo(QOpenGLBuffer::VertexBuffer)
    , m_pointVbo(QOpenGLBuffer::VertexBuffer)
{}

TrackRenderer::~TrackRenderer() {}

// 初始化 OpenGL 资源
void TrackRenderer::initialize() {
    if (m_initialized) return;

    // 编译线段着色器
    m_lineShader.addShaderFromSourceCode(QOpenGLShader::Vertex,   LINE_VERT_SRC);
    m_lineShader.addShaderFromSourceCode(QOpenGLShader::Fragment, LINE_FRAG_SRC);
    m_lineShader.link();

    // 编译点着色器
    m_pointShader.addShaderFromSourceCode(QOpenGLShader::Vertex,   POINT_VERT_SRC);
    m_pointShader.addShaderFromSourceCode(QOpenGLShader::Fragment, POINT_FRAG_SRC);
    m_pointShader.link();

    constexpr int stride = 7 * sizeof(float);

    // 线段 VAO/VBO
    m_lineVao.create();
    m_lineVao.bind();
    m_lineVbo.create();
    m_lineVbo.bind();
    m_lineVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                                 reinterpret_cast<void*>(0));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                                 reinterpret_cast<void*>(3 * sizeof(float)));
    m_lineVao.release();
    m_lineVbo.release();

    // 点 VAO/VBO
    m_pointVao.create();
    m_pointVao.bind();
    m_pointVbo.create();
    m_pointVbo.bind();
    m_pointVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                                 reinterpret_cast<void*>(0));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                                 reinterpret_cast<void*>(3 * sizeof(float)));
    m_pointVao.release();
    m_pointVbo.release();

    m_initialized = true;
}

// ── 速度→颜色映射 ────────────────────────────────────────────

// 将归一化速度 t ∈ [0,1] 映射为颜色渐变（蓝→青→绿→黄→红）
QVector4D TrackRenderer::velocityToColor(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    float r, g, b;
    if      (t < 0.25f) { r=0; g=t*4.0f; b=1.0f; }
    else if (t < 0.5f)  { r=0; g=1.0f; b=1.0f-(t-0.25f)*4.0f; }
    else if (t < 0.75f) { r=(t-0.5f)*4.0f; g=1.0f; b=0; }
    else                { r=1.0f; g=1.0f-(t-0.75f)*4.0f; b=0; }
    return {r, g, b, 1.0f};
}

// ── 路径降采样 ────────────────────────────────────────────────

// 使用角度阈值过滤冗余中间点（保留方向转弯较大的关键点）
std::vector<QVector3D> TrackRenderer::downsamplePath(
    const std::deque<QVector3D>& path, float angleDegThreshold) {
    if (path.size() <= 2) {
        return {path.begin(), path.end()};
    }

    float cosThreshold = std::cos(angleDegThreshold * float(M_PI) / 180.0f);
    std::vector<QVector3D> result;
    result.push_back(path.front());

    for (size_t i = 1; i + 1 < path.size(); ++i) {
        QVector3D prev = path[i - 1];
        QVector3D curr = path[i];
        QVector3D next = path[i + 1];
        QVector3D d1 = (curr - prev);
        QVector3D d2 = (next - curr);
        float l1 = d1.length(), l2 = d2.length();
        if (l1 < 1e-6f || l2 < 1e-6f) { result.push_back(curr); continue; }
        float cosAngle = QVector3D::dotProduct(d1 / l1, d2 / l2);
        // 方向变化超过阈值时保留该点
        if (cosAngle < cosThreshold) result.push_back(curr);
    }
    result.push_back(path.back());
    return result;
}

// ── 历史压缩 ──────────────────────────────────────────────────

// 当历史点数超过 FULL_PATH_RAW_MAX 时调用
// 对旧部分（前 total - RECENT_PRESERVE）做角度降采样，保留最新部分不动
void TrackRenderer::compactHistory(PointData& pd) {
    size_t total = pd.history.size();
    if ((int)total <= FULL_PATH_RAW_MAX) return;

    size_t oldCount = total - RECENT_PRESERVE;

    std::deque<QVector3D> oldPart(pd.history.begin(),
                                   pd.history.begin() + static_cast<std::ptrdiff_t>(oldCount));
    auto compacted = downsamplePath(oldPart, COMPACT_ANGLE_DEG);

    std::deque<QVector3D> newHistory;
    newHistory.insert(newHistory.end(), compacted.begin(), compacted.end());
    for (size_t i = oldCount; i < total; ++i)
        newHistory.push_back(pd.history[i]);

    pd.history = std::move(newHistory);

    // 降采样后样本变稀，相邻段距分布改变，重新计算最大段距作为着色归一化基准
    pd.maxSampleSpeed = 1e-6f;
    for (size_t i = 1; i < pd.history.size(); ++i) {
        float seg = (pd.history[i] - pd.history[i-1]).length();
        if (seg > pd.maxSampleSpeed) pd.maxSampleSpeed = seg;
    }
}

// ── 公有接口 ──────────────────────────────────────────────────

// 更新命名点的位置，追加到历史记录
void TrackRenderer::updatePoint(const QString& name, double x, double y, double z,
                                  const QColor& color, int size) {
    auto& pd = m_points[name];
    pd.color = color;
    pd.size  = size;

    QVector3D next{float(x), float(y), float(z)};

    // 增量维护最大段距（着色归一化基准）
    if (!pd.history.empty()) {
        float seg = (next - pd.history.back()).length();
        if (seg > pd.maxSampleSpeed) pd.maxSampleSpeed = seg;
    }

    pd.history.push_back(next);

    if ((int)pd.history.size() > FULL_PATH_RAW_MAX) {
        compactHistory(pd);
    }

    m_dirty = true;
}

// 清除所有点和轨迹历史
void TrackRenderer::clearAll() {
    m_points.clear();
    m_cachedLineVerts.clear();
    m_cachedPoints.clear();
    m_dirty = true;
}

void TrackRenderer::setFullPathMode(bool enabled)  { m_fullPathMode  = enabled; m_dirty = true; }
void TrackRenderer::setPathColorMode(bool enabled) { m_pathColorMode = enabled; m_dirty = true; }

// 当数据变更时重建缓存的顶点数组
void TrackRenderer::rebuildCache() {
    m_cachedLineVerts.clear();
    m_cachedPoints.clear();

    for (auto it = m_points.cbegin(); it != m_points.cend(); ++it) {
        const PointData& pd = it.value();
        if (pd.history.empty()) continue;

        float cr = float(pd.color.redF());
        float cg = float(pd.color.greenF());
        float cb = float(pd.color.blueF());
        float ca = float(pd.color.alphaF());

        // 仅当开启「绘制路径」时才生成线段顶点
        if (m_fullPathMode && pd.history.size() >= 2) {
            auto sampled = downsamplePath(pd.history, 1.5f);
            const size_t segCount = sampled.size() > 0 ? sampled.size() - 1 : 0;
            const float invDenom = (segCount > 0) ? 1.0f / float(segCount) : 0.0f;
            const float invMaxSpeed = 1.0f / std::max(pd.maxSampleSpeed, 1e-6f);

            for (size_t i = 0; i + 1 < sampled.size(); ++i) {
                float alpha = 0.3f + 0.7f * float(i) * invDenom;

                float r, g, b;
                if (m_pathColorMode) {
                    // 按当前段长度（≈瞬时速度）做颜色映射
                    float seg = (sampled[i+1] - sampled[i]).length();
                    QVector4D c = velocityToColor(seg * invMaxSpeed);
                    r = c.x(); g = c.y(); b = c.z();
                } else {
                    r = cr; g = cg; b = cb;
                }

                m_cachedLineVerts.push_back({sampled[i].x(),   sampled[i].y(),   sampled[i].z(),   r, g, b, alpha});
                m_cachedLineVerts.push_back({sampled[i+1].x(), sampled[i+1].y(), sampled[i+1].z(), r, g, b, alpha});
            }
        }

        const QVector3D& pos = pd.history.back();
        m_cachedPoints.push_back({{pos.x(), pos.y(), pos.z(), cr, cg, cb, ca}, float(pd.size)});
    }

    m_vboNeedsUpload = true;
    m_dirty = false;
}

// 渲染所有点和轨迹
void TrackRenderer::render(const QMatrix4x4& mvpMatrix) {
    if (!m_initialized || m_points.isEmpty()) return;

    if (m_dirty) rebuildCache();

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glEnable(GL_PROGRAM_POINT_SIZE);

    if (!m_cachedLineVerts.empty()) {
        m_lineVbo.bind();
        if (m_vboNeedsUpload)
            m_lineVbo.allocate(m_cachedLineVerts.data(),
                               static_cast<int>(m_cachedLineVerts.size() * sizeof(Vertex)));
        m_lineShader.bind();
        m_lineShader.setUniformValue("mvpMatrix", mvpMatrix);
        m_lineVao.bind();
        m_gl->glLineWidth(1.5f);
        m_gl->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_cachedLineVerts.size()));
        m_lineVao.release();
        m_lineShader.release();
        m_lineVbo.release();
    }

    if (!m_cachedPoints.empty()) {
        m_pointShader.bind();
        m_pointShader.setUniformValue("mvpMatrix", mvpMatrix);
        m_pointVao.bind();
        for (const auto& cp : m_cachedPoints) {
            m_pointVbo.bind();
            m_pointVbo.allocate(&cp.v, static_cast<int>(sizeof(Vertex)));
            m_pointShader.setUniformValue("pointSize", cp.size);
            m_gl->glDrawArrays(GL_POINTS, 0, 1);
            m_pointVbo.release();
        }
        m_pointVao.release();
        m_pointShader.release();
    }

    m_vboNeedsUpload = false;
    m_gl->glDisable(GL_PROGRAM_POINT_SIZE);
}
