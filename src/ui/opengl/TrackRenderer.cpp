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
    , m_vbo(QOpenGLBuffer::VertexBuffer)
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

    // 创建 VAO 和 VBO（线段/点共用同一个 VBO，按需上传）
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.setUsagePattern(QOpenGLBuffer::StreamDraw);

    constexpr int stride = 7 * sizeof(float);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                                 reinterpret_cast<void*>(0));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                                 reinterpret_cast<void*>(3 * sizeof(float)));
    m_vao.release();
    m_vbo.release();

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

// ── 公有接口 ──────────────────────────────────────────────────

// 更新命名点的位置，追加到历史记录
void TrackRenderer::updatePoint(const QString& name, double x, double y, double z,
                                  const QColor& color, int size) {
    auto& pd = m_points[name];
    pd.color = color;
    pd.size  = size;

    // 保持历史长度限制
    pd.history.push_back({float(x), float(y), float(z)});
    while ((int)pd.history.size() > m_trailLength) {
        pd.history.pop_front();
    }
}

// 清除所有点和轨迹历史
void TrackRenderer::clearAll() {
    m_points.clear();
}

void TrackRenderer::setFullPathMode(bool enabled) { m_fullPathMode = enabled; }
void TrackRenderer::setTrailMode(bool enabled)    { m_trailMode    = enabled; }
void TrackRenderer::setTrailLength(int length) {
    m_trailLength = std::max(10, length);
    // 截断超出新长度的历史（使用迭代器，避免 QMap 结构化绑定兼容性问题）
    for (auto it = m_points.begin(); it != m_points.end(); ++it) {
        auto& pd = it.value();
        while ((int)pd.history.size() > m_trailLength) pd.history.pop_front();
    }
}

// 渲染所有点和轨迹
void TrackRenderer::render(const QMatrix4x4& mvpMatrix) {
    if (!m_initialized || m_points.isEmpty()) return;

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glEnable(GL_PROGRAM_POINT_SIZE);

    // 使用传统迭代器遍历，避免 QMap structured binding 兼容性问题
    for (auto it = m_points.cbegin(); it != m_points.cend(); ++it) {
        const PointData& pd = it.value();
        if (pd.history.empty()) continue;

        float cr = float(pd.color.redF());
        float cg = float(pd.color.greenF());
        float cb = float(pd.color.blueF());
        float ca = float(pd.color.alphaF());

        // ── 绘制轨迹线 ──────────────────────────────────────
        if ((m_fullPathMode || m_trailMode) && pd.history.size() >= 2) {
            std::vector<Vertex> lineVerts;

            if (m_trailMode) {
                // 速度尾迹模式：基于相邻点间距估算速度，着色渐变
                std::vector<float> speeds;
                float maxSpeed = 0;
                for (size_t i = 1; i < pd.history.size(); ++i) {
                    float d = (pd.history[i] - pd.history[i-1]).length();
                    speeds.push_back(d);
                    maxSpeed = std::max(maxSpeed, d);
                }
                if (maxSpeed < 1e-6f) maxSpeed = 1.0f;

                for (size_t i = 0; i + 1 < pd.history.size(); ++i) {
                    float t = speeds[i] / maxSpeed;
                    QVector4D c = velocityToColor(t);
                    float alpha = 0.3f + 0.7f * float(i) / float(pd.history.size());
                    Vertex v0{pd.history[i].x(),   pd.history[i].y(),   pd.history[i].z(),   c.x(), c.y(), c.z(), alpha};
                    Vertex v1{pd.history[i+1].x(), pd.history[i+1].y(), pd.history[i+1].z(), c.x(), c.y(), c.z(), alpha};
                    lineVerts.push_back(v0);
                    lineVerts.push_back(v1);
                }
            } else {
                // 全路径模式：降采样后统一颜色绘制
                auto sampled = downsamplePath(pd.history, 1.5f);
                for (size_t i = 0; i + 1 < sampled.size(); ++i) {
                    float alpha = 0.3f + 0.7f * float(i) / float(sampled.size());
                    Vertex v0{sampled[i].x(),   sampled[i].y(),   sampled[i].z(),   cr, cg, cb, alpha};
                    Vertex v1{sampled[i+1].x(), sampled[i+1].y(), sampled[i+1].z(), cr, cg, cb, alpha};
                    lineVerts.push_back(v0);
                    lineVerts.push_back(v1);
                }
            }

            if (!lineVerts.empty()) {
                m_vbo.bind();
                m_vbo.allocate(lineVerts.data(),
                                static_cast<int>(lineVerts.size() * sizeof(Vertex)));
                m_lineShader.bind();
                m_lineShader.setUniformValue("mvpMatrix", mvpMatrix);
                m_vao.bind();
                m_gl->glLineWidth(1.5f);
                m_gl->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVerts.size()));
                m_vao.release();
                m_lineShader.release();
                m_vbo.release();
            }
        }

        // ── 绘制当前位置点 ────────────────────────────────────
        const QVector3D& pos = pd.history.back();
        Vertex pv{pos.x(), pos.y(), pos.z(), cr, cg, cb, ca};
        m_vbo.bind();
        m_vbo.allocate(&pv, static_cast<int>(sizeof(Vertex)));
        m_pointShader.bind();
        m_pointShader.setUniformValue("mvpMatrix", mvpMatrix);
        m_pointShader.setUniformValue("pointSize", float(pd.size));
        m_vao.bind();
        m_gl->glDrawArrays(GL_POINTS, 0, 1);
        m_vao.release();
        m_pointShader.release();
        m_vbo.release();
    }

    m_gl->glDisable(GL_PROGRAM_POINT_SIZE);
}
