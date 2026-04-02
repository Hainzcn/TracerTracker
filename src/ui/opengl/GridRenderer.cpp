#include "GridRenderer.h"
#include <QPainter>
#include <QVector4D>
#include <cmath>
#include <algorithm>
#include <array>

// ── GLSL shaders ────────────────────────────────────────────────

const char* GridRenderer::VERT_SHADER_SRC = R"(
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

const char* GridRenderer::FRAG_SHADER_SRC = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

// ── construction ────────────────────────────────────────────────

GridRenderer::GridRenderer(QOpenGLFunctions_3_3_Core* gl)
    : m_gl(gl)
    , m_lineVBO(QOpenGLBuffer::VertexBuffer)
    , m_triVBO(QOpenGLBuffer::VertexBuffer)
{}

GridRenderer::~GridRenderer() = default;

void GridRenderer::initialize() {
    if (m_initialized) return;

    m_shader.addShaderFromSourceCode(QOpenGLShader::Vertex,   VERT_SHADER_SRC);
    m_shader.addShaderFromSourceCode(QOpenGLShader::Fragment, FRAG_SHADER_SRC);
    m_shader.link();

    constexpr int stride = 7 * sizeof(float);

    // line VAO/VBO
    m_lineVAO.create();
    m_lineVAO.bind();
    m_lineVBO.create();
    m_lineVBO.bind();
    m_lineVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    m_lineVAO.release();
    m_lineVBO.release();

    // triangle VAO/VBO (for arrow billboards)
    m_triVAO.create();
    m_triVAO.bind();
    m_triVBO.create();
    m_triVBO.bind();
    m_triVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    m_triVAO.release();
    m_triVBO.release();

    m_initialized = true;
}

// ── vertex helpers ──────────────────────────────────────────────

void GridRenderer::addLine(const QVector3D& a, const QVector3D& b,
                           float r, float g, float b_, float alpha) {
    m_lineVerts.push_back({a.x(), a.y(), a.z(), r, g, b_, alpha});
    m_lineVerts.push_back({b.x(), b.y(), b.z(), r, g, b_, alpha});
}

void GridRenderer::addTri(const QVector3D& p0, const QVector3D& p1, const QVector3D& p2,
                          float r, float g, float b_, float alpha) {
    m_triVerts.push_back({p0.x(), p0.y(), p0.z(), r, g, b_, alpha});
    m_triVerts.push_back({p1.x(), p1.y(), p1.z(), r, g, b_, alpha});
    m_triVerts.push_back({p2.x(), p2.y(), p2.z(), r, g, b_, alpha});
}

// ── math: grid spacings (x5 power sequence, matching Python) ───

GridRenderer::GridSpacings GridRenderer::computeGridSpacings(float viewRange, int targetMinorCount) {
    if (viewRange <= 0.0f) return {1.0f, 5.0f, 0.5f};
    float ideal = viewRange / float(targetMinorCount);
    static const float LOG5 = std::log(5.0f);
    float log5Val = std::log(std::max(ideal, 1e-15f)) / LOG5;
    int level = int(std::floor(log5Val));
    float minor = std::pow(5.0f, float(level));
    float major = 5.0f * minor;
    float phase = std::max(0.0f, std::min(1.0f, log5Val - float(level)));
    return {minor, major, phase};
}

// ── math: camera direction vector ──────────────────────────────

QVector3D GridRenderer::cameraDirection(double elevation, double azimuth) {
    float elev = float(elevation * M_PI / 180.0);
    float azim = float(azimuth   * M_PI / 180.0);
    QVector3D d(std::cos(elev) * std::sin(azim),
                std::cos(elev) * std::cos(azim),
                -std::sin(elev));
    float len = d.length();
    return (len > 1e-6f) ? d / len : QVector3D(0, 1, 0);
}

// ── math: plane weights (matching Python _compute_plane_weights) ─

GridRenderer::PlaneWeights GridRenderer::computePlaneWeights(
        const QVector3D& camDir, float orthoBlend) const {
    PlaneWeights defaultW{1.0f, 0.0f, 0.0f};
    if (orthoBlend <= 1e-4f) return defaultW;

    // axis components
    float ax = std::abs(camDir.x());
    float ay = std::abs(camDir.y());
    float az = std::abs(camDir.z());

    // sort axes by component magnitude
    struct AC { int idx; float val; };
    std::array<AC,3> axes = {{{0, ax}, {1, ay}, {2, az}}};
    std::sort(axes.begin(), axes.end(), [](const AC& a, const AC& b){ return a.val > b.val; });

    int primaryAxis   = axes[0].idx;
    int secondaryAxis = axes[1].idx;
    float primaryStr   = axes[0].val;
    float secondaryStr = axes[1].val;
    float diff = primaryStr - secondaryStr;

    // plane perpendicular to axis: x->yoz(2), y->xoz(1), z->xoy(0)
    auto planeForAxis = [](int a) -> int { return (a == 0) ? 2 : (a == 1) ? 1 : 0; };
    int primaryPlane   = planeForAxis(primaryAxis);
    int secondaryPlane = planeForAxis(secondaryAxis);

    PlaneWeights target{0, 0, 0};
    auto setW = [&](int p, float v) {
        if (p == 0) target.xoy = v;
        else if (p == 1) target.xoz = v;
        else target.yoz = v;
    };

    if (diff >= ORTHO_SWITCH_BAND) {
        setW(primaryPlane, 1.0f);
    } else {
        float blendT = std::max(0.0f, std::min(1.0f, diff / std::max(ORTHO_SWITCH_BAND, 1e-9f)));
        float pw = 0.5f + 0.5f * blendT;
        setW(primaryPlane, pw);
        setW(secondaryPlane, 1.0f - pw);
    }

    float b = std::max(0.0f, std::min(1.0f, orthoBlend));
    return {
        defaultW.xoy * (1.0f - b) + target.xoy * b,
        defaultW.xoz * (1.0f - b) + target.xoz * b,
        defaultW.yoz * (1.0f - b) + target.yoz * b,
    };
}

int GridRenderer::dominantPlane(const PlaneWeights& w) {
    if (w.xoz >= w.xoy && w.xoz >= w.yoz) return 1;
    if (w.yoz >= w.xoy && w.yoz >= w.xoz) return 2;
    return 0;
}

// ── math: axis label visibility ────────────────────────────────

GridRenderer::AxisVisibility GridRenderer::computeAxisLabelVisibility(
        const QVector3D& camDir) const {
    float ax = std::abs(camDir.x());
    float ay = std::abs(camDir.y());
    float az = std::abs(camDir.z());
    auto fade = [](float c) {
        return std::max(0.0f, std::min(1.0f, 1.0f - std::pow(c, AXIS_LABEL_FADE_POWER)));
    };
    return {fade(ax), fade(ay), fade(az)};
}

// ── math: submersion effect ────────────────────────────────────

float GridRenderer::computeSubmersionFactor(
        const QVector3D& point, int domPlane,
        const PlaneWeights& weights,
        const QVector3D& camDir, float axLen) const {
    float planeWeight = 0.0f;
    int normalAxis = 0;
    if (domPlane == 0)      { planeWeight = weights.xoy; normalAxis = 2; }
    else if (domPlane == 1) { planeWeight = weights.xoz; normalAxis = 1; }
    else                    { planeWeight = weights.yoz; normalAxis = 0; }

    if (planeWeight <= 1e-4f) return 0.0f;

    float camComp = (normalAxis == 0) ? camDir.x() : (normalAxis == 1) ? camDir.y() : camDir.z();
    if (std::abs(camComp) <= 1e-6f) return 0.0f;

    float pointComp = (normalAxis == 0) ? point.x() : (normalAxis == 1) ? point.y() : point.z();
    float signedDepth = pointComp * camComp;
    if (signedDepth <= 0.0f) return 0.0f;

    float normalized = std::min(1.0f, signedDepth / std::max(axLen * SUBMERSION_DEPTH_RANGE, 1e-6f));
    return normalized * planeWeight;
}

GridRenderer::RGBA GridRenderer::applySubmersion(RGBA color, float depthFactor) {
    depthFactor = std::max(0.0f, std::min(1.0f, depthFactor));
    if (depthFactor <= 1e-6f) return color;

    float gray = color.r * 0.299f + color.g * 0.587f + color.b * 0.114f;
    float desat = depthFactor * SUBMERSION_DESAT_MAX;
    float dim = 1.0f - depthFactor * SUBMERSION_DIM_MAX;
    float sr = std::max(0.0f, std::min(1.0f, (color.r * (1.0f - desat) + gray * desat) * dim));
    float sg = std::max(0.0f, std::min(1.0f, (color.g * (1.0f - desat) + gray * desat) * dim));
    float sb = std::max(0.0f, std::min(1.0f, (color.b * (1.0f - desat) + gray * desat) * dim));
    return {sr, sg, sb, color.a};
}

QColor GridRenderer::applyVisibilityToColor(const QColor& base, float visibility,
                                            float depthFactor) {
    visibility = std::max(0.0f, std::min(1.0f, visibility));
    RGBA rgba{float(base.redF()), float(base.greenF()), float(base.blueF()), float(base.alphaF())};
    RGBA styled = applySubmersion(rgba, depthFactor);
    return QColor::fromRgbF(styled.r, styled.g, styled.b,
                            std::max(0.0f, std::min(1.0f, float(base.alphaF()) * visibility)));
}

QString GridRenderer::formatTickValue(float v) {
    if (std::abs(v) < 1e-9f) return QStringLiteral("0");
    float av = std::abs(v);
    if (av >= 1000.0f || (av < 0.01f && av > 0.0f))
        return QString::number(double(v), 'e', 2);
    if (v == std::floor(v))
        return QString::number(int(v));
    QString s = QString::number(double(v), 'f', 2);
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
    return s;
}

// ── grid plane builder (with edge fade) ─────────────────────────

void GridRenderer::buildGridPlane(int planeIdx, float spacing, float halfExtent,
                                  float fadeRadius, int skipMultiple,
                                  float baseR, float baseG, float baseB, float baseA) {
    if (spacing <= 0.0f || halfExtent <= 0.0f || baseA < 1e-5f) return;

    int n = int(halfExtent / spacing + 0.5f);
    bool doFade = fadeRadius < halfExtent;
    float invFadeRange = doFade ? (1.0f / std::max(halfExtent - fadeRadius, 1e-9f)) : 1.0f;

    // axis_u and axis_v for the plane
    int axU, axV;
    if (planeIdx == 0)      { axU = 0; axV = 1; } // XOY
    else if (planeIdx == 1) { axU = 0; axV = 2; } // XOZ
    else                    { axU = 1; axV = 2; } // YOZ

    auto makePoint = [&](float u, float v) -> QVector3D {
        QVector3D p(0, 0, 0);
        if (axU == 0) p.setX(u); else if (axU == 1) p.setY(u); else p.setZ(u);
        if (axV == 0) p.setX(v); else if (axV == 1) p.setY(v); else p.setZ(v);
        return p;
    };

    for (int i = -n; i <= n; ++i) {
        if (skipMultiple > 0 && i % skipMultiple == 0) continue;
        float coord = float(i) * spacing;
        float d = std::abs(coord);

        float alphaMult = 1.0f;
        if (doFade && d > fadeRadius) {
            float t = (d - fadeRadius) * invFadeRange;
            alphaMult = std::max(0.0f, 1.0f - t * t);
            if (alphaMult < 1e-4f) continue;
        }

        float a = baseA * alphaMult;

        if (doFade) {
            // line along U direction, split into 3 segments
            // segment 1: [-halfExtent, -fadeRadius] alpha 0 -> a
            m_lineVerts.push_back({makePoint(-halfExtent, coord).x(), makePoint(-halfExtent, coord).y(), makePoint(-halfExtent, coord).z(), baseR, baseG, baseB, 0.0f});
            m_lineVerts.push_back({makePoint(-fadeRadius, coord).x(), makePoint(-fadeRadius, coord).y(), makePoint(-fadeRadius, coord).z(), baseR, baseG, baseB, a});
            // segment 2: [-fadeRadius, fadeRadius] full alpha
            m_lineVerts.push_back({makePoint(-fadeRadius, coord).x(), makePoint(-fadeRadius, coord).y(), makePoint(-fadeRadius, coord).z(), baseR, baseG, baseB, a});
            m_lineVerts.push_back({makePoint( fadeRadius, coord).x(), makePoint( fadeRadius, coord).y(), makePoint( fadeRadius, coord).z(), baseR, baseG, baseB, a});
            // segment 3: [fadeRadius, halfExtent] alpha a -> 0
            m_lineVerts.push_back({makePoint( fadeRadius, coord).x(), makePoint( fadeRadius, coord).y(), makePoint( fadeRadius, coord).z(), baseR, baseG, baseB, a});
            m_lineVerts.push_back({makePoint( halfExtent, coord).x(), makePoint( halfExtent, coord).y(), makePoint( halfExtent, coord).z(), baseR, baseG, baseB, 0.0f});

            // line along V direction, split into 3 segments
            m_lineVerts.push_back({makePoint(coord, -halfExtent).x(), makePoint(coord, -halfExtent).y(), makePoint(coord, -halfExtent).z(), baseR, baseG, baseB, 0.0f});
            m_lineVerts.push_back({makePoint(coord, -fadeRadius).x(), makePoint(coord, -fadeRadius).y(), makePoint(coord, -fadeRadius).z(), baseR, baseG, baseB, a});
            m_lineVerts.push_back({makePoint(coord, -fadeRadius).x(), makePoint(coord, -fadeRadius).y(), makePoint(coord, -fadeRadius).z(), baseR, baseG, baseB, a});
            m_lineVerts.push_back({makePoint(coord,  fadeRadius).x(), makePoint(coord,  fadeRadius).y(), makePoint(coord,  fadeRadius).z(), baseR, baseG, baseB, a});
            m_lineVerts.push_back({makePoint(coord,  fadeRadius).x(), makePoint(coord,  fadeRadius).y(), makePoint(coord,  fadeRadius).z(), baseR, baseG, baseB, a});
            m_lineVerts.push_back({makePoint(coord,  halfExtent).x(), makePoint(coord,  halfExtent).y(), makePoint(coord,  halfExtent).z(), baseR, baseG, baseB, 0.0f});
        } else {
            addLine(makePoint(-halfExtent, coord), makePoint(halfExtent, coord), baseR, baseG, baseB, a);
            addLine(makePoint(coord, -halfExtent), makePoint(coord, halfExtent), baseR, baseG, baseB, a);
        }
    }
}

// ── axis lines with negative-half fade ──────────────────────────

void GridRenderer::buildAxisLines() {
    float fadeStart = m_negExt * 0.5f;

    struct AxisDef {
        int idx;
        float cr, cg, cb;
    };
    std::array<AxisDef, 3> axes = {{
        {0, 1.0f, 0.0f, 0.0f},
        {1, 0.0f, 1.0f, 0.0f},
        {2, 0.0f, 0.0f, 1.0f},
    }};

    for (auto& ax : axes) {
        QVector3D pNeg(0,0,0), pFade(0,0,0), pPos(0,0,0);
        if (ax.idx == 0) { pNeg.setX(m_negExt); pFade.setX(fadeStart); pPos.setX(m_posExt); }
        else if (ax.idx == 1) { pNeg.setY(m_negExt); pFade.setY(fadeStart); pPos.setY(m_posExt); }
        else { pNeg.setZ(m_negExt); pFade.setZ(fadeStart); pPos.setZ(m_posExt); }

        RGBA cNeg = applySubmersion(
            {ax.cr, ax.cg, ax.cb, 0.0f},
            computeSubmersionFactor(pNeg, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength));
        RGBA cFade = applySubmersion(
            {ax.cr, ax.cg, ax.cb, 1.0f},
            computeSubmersionFactor(pFade, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength));
        RGBA cPos = applySubmersion(
            {ax.cr, ax.cg, ax.cb, 1.0f},
            computeSubmersionFactor(pPos, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength));

        // neg -> fade (alpha 0 -> 1)
        m_lineVerts.push_back({pNeg.x(), pNeg.y(), pNeg.z(), cNeg.r, cNeg.g, cNeg.b, cNeg.a});
        m_lineVerts.push_back({pFade.x(), pFade.y(), pFade.z(), cFade.r, cFade.g, cFade.b, cFade.a});
        // fade -> pos (full alpha)
        m_lineVerts.push_back({pFade.x(), pFade.y(), pFade.z(), cFade.r, cFade.g, cFade.b, cFade.a});
        m_lineVerts.push_back({pPos.x(), pPos.y(), pPos.z(), cPos.r, cPos.g, cPos.b, cPos.a});
    }
}

// ── axis quads (thick axis lines via triangle strips) ───────────

void GridRenderer::buildAxisQuads() {
    float halfW = m_axisLength * 0.004f;
    float fadeStart = m_negExt * 0.5f;

    struct AxisDef {
        int idx;
        float cr, cg, cb;
    };
    std::array<AxisDef, 3> axes = {{
        {0, 1.0f, 0.0f, 0.0f},
        {1, 0.0f, 1.0f, 0.0f},
        {2, 0.0f, 0.0f, 1.0f},
    }};

    for (auto& ax : axes) {
        QVector3D dir(0,0,0);
        if (ax.idx == 0) dir.setX(1); else if (ax.idx == 1) dir.setY(1); else dir.setZ(1);

        QVector3D w = QVector3D::crossProduct(dir, m_camDir);
        float wl = w.length();
        if (wl < 1e-6f) {
            QVector3D fallback = (ax.idx == 0) ? QVector3D(0,1,0) : QVector3D(1,0,0);
            w = QVector3D::crossProduct(dir, fallback);
            wl = w.length();
        }
        if (wl > 1e-6f) w = w / wl;
        w *= halfW;

        QVector3D pNeg = dir * m_negExt;
        QVector3D pFade = dir * fadeStart;
        QVector3D pPos = dir * m_posExt;

        RGBA cNeg = applySubmersion(
            {ax.cr, ax.cg, ax.cb, 0.0f},
            computeSubmersionFactor(pNeg, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength));
        RGBA cFade = applySubmersion(
            {ax.cr, ax.cg, ax.cb, 1.0f},
            computeSubmersionFactor(pFade, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength));
        RGBA cPos = applySubmersion(
            {ax.cr, ax.cg, ax.cb, 1.0f},
            computeSubmersionFactor(pPos, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength));

        // neg -> fade quad (alpha 0 -> 1)
        QVector3D nA = pNeg - w, nB = pNeg + w;
        QVector3D fA = pFade - w, fB = pFade + w;
        addTri(nA, fA, nB, cNeg.r, cNeg.g, cNeg.b, cNeg.a);
        addTri(nB, fA, fB, cNeg.r, cNeg.g, cNeg.b, cNeg.a);
        // fix: use per-vertex color for gradient
        m_triVerts[m_triVerts.size()-6] = {nA.x(), nA.y(), nA.z(), cNeg.r, cNeg.g, cNeg.b, cNeg.a};
        m_triVerts[m_triVerts.size()-5] = {fA.x(), fA.y(), fA.z(), cFade.r, cFade.g, cFade.b, cFade.a};
        m_triVerts[m_triVerts.size()-4] = {nB.x(), nB.y(), nB.z(), cNeg.r, cNeg.g, cNeg.b, cNeg.a};
        m_triVerts[m_triVerts.size()-3] = {nB.x(), nB.y(), nB.z(), cNeg.r, cNeg.g, cNeg.b, cNeg.a};
        m_triVerts[m_triVerts.size()-2] = {fA.x(), fA.y(), fA.z(), cFade.r, cFade.g, cFade.b, cFade.a};
        m_triVerts[m_triVerts.size()-1] = {fB.x(), fB.y(), fB.z(), cFade.r, cFade.g, cFade.b, cFade.a};

        // fade -> pos quad (full alpha)
        QVector3D pA = pPos - w, pB = pPos + w;
        addTri(fA, pA, fB, cFade.r, cFade.g, cFade.b, cFade.a);
        addTri(fB, pA, pB, cFade.r, cFade.g, cFade.b, cFade.a);
        m_triVerts[m_triVerts.size()-6] = {fA.x(), fA.y(), fA.z(), cFade.r, cFade.g, cFade.b, cFade.a};
        m_triVerts[m_triVerts.size()-5] = {pA.x(), pA.y(), pA.z(), cPos.r, cPos.g, cPos.b, cPos.a};
        m_triVerts[m_triVerts.size()-4] = {fB.x(), fB.y(), fB.z(), cFade.r, cFade.g, cFade.b, cFade.a};
        m_triVerts[m_triVerts.size()-3] = {fB.x(), fB.y(), fB.z(), cFade.r, cFade.g, cFade.b, cFade.a};
        m_triVerts[m_triVerts.size()-2] = {pA.x(), pA.y(), pA.z(), cPos.r, cPos.g, cPos.b, cPos.a};
        m_triVerts[m_triVerts.size()-1] = {pB.x(), pB.y(), pB.z(), cPos.r, cPos.g, cPos.b, cPos.a};
    }
}

// ── billboard arrow triangles ───────────────────────────────────

void GridRenderer::buildArrowBillboards() {
    float arrowLen = m_axisLength * 0.05f;
    float arrowWidth = arrowLen * 0.4f;

    struct AxisDef { QVector3D dir; float cr, cg, cb; QVector3D fallback; };
    std::array<AxisDef, 3> axes = {{
        {QVector3D(1,0,0), 1,0,0, QVector3D(0,1,0)},
        {QVector3D(0,1,0), 0,1,0, QVector3D(1,0,0)},
        {QVector3D(0,0,1), 0,0,1, QVector3D(1,0,0)},
    }};

    for (auto& ax : axes) {
        QVector3D w = QVector3D::crossProduct(ax.dir, m_camDir);
        float wl = w.length();
        if (wl > 1e-6f) w = w / wl;
        else w = ax.fallback;
        w *= arrowWidth;

        QVector3D tip  = ax.dir * (m_posExt + arrowLen);
        QVector3D base = ax.dir * m_posExt;

        float depth = computeSubmersionFactor(tip, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength);
        RGBA c = applySubmersion({ax.cr, ax.cg, ax.cb, 1.0f}, depth);

        addTri(tip, base - w, base + w, c.r, c.g, c.b, c.a);
    }
}

// ── tick lines ──────────────────────────────────────────────────

void GridRenderer::buildTickLines() {
    float tickHalf = m_axisLength * TICK_LINE_LENGTH_RATIO;
    float fadeStart = m_negExt * 0.5f;

    struct AxisDef {
        int mainAx;
        float cr, cg, cb;
        int perpA, perpB;
    };
    std::array<AxisDef, 3> axes = {{
        {0, 1.0f, 0.3f, 0.3f, 1, 2},
        {1, 0.3f, 1.0f, 0.3f, 0, 2},
        {2, 0.3f, 0.3f, 1.0f, 0, 1},
    }};

    for (auto& ax : axes) {
        float val = m_majorSpacing;
        while (val <= m_posExt + 1e-9f) {
            for (int si = 0; si < 2; ++si) {
                float signVal = (si == 0) ? val : -val;
                if (si == 1 && std::abs(val) < 1e-9f) continue;
                if (signVal < fadeStart - 1e-9f || signVal > m_posExt + 1e-9f) continue;

                QVector3D p1(0,0,0), p2(0,0,0);
                auto setComp = [](QVector3D& p, int idx, float v) {
                    if (idx == 0) p.setX(v); else if (idx == 1) p.setY(v); else p.setZ(v);
                };
                setComp(p1, ax.mainAx, signVal);
                setComp(p2, ax.mainAx, signVal);
                setComp(p1, ax.perpA, -tickHalf);
                setComp(p2, ax.perpA,  tickHalf);

                QVector3D mid = (p1 + p2) * 0.5f;
                float depth = computeSubmersionFactor(mid, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength);
                RGBA c = applySubmersion({ax.cr, ax.cg, ax.cb, 0.7f}, depth);
                addLine(p1, p2, c.r, c.g, c.b, c.a);
            }
            val += m_majorSpacing;
        }
    }
}

// ── full geometry rebuild ───────────────────────────────────────

void GridRenderer::rebuildGeometry(double distance, double sceneScale,
                                   double elevation, double azimuth,
                                   float orthoBlend) {
    m_lineVerts.clear();
    m_triVerts.clear();

    m_axisLength = float(distance) * AXIS_VISUAL_RATIO / std::max(float(sceneScale), 1e-9f);
    m_negExt = -m_axisLength * 0.5f;
    m_posExt = m_axisLength;

    float totalRange = m_posExt - m_negExt;
    auto sp = computeGridSpacings(totalRange);
    m_majorSpacing = sp.major;
    m_minorSpacing = sp.minor;
    m_phaseT = sp.phase;

    m_camDir = cameraDirection(elevation, azimuth);
    m_planeWeights = computePlaneWeights(m_camDir, orthoBlend);
    m_dominantPlane = dominantPlane(m_planeWeights);
    m_axisLabelVis = computeAxisLabelVisibility(m_camDir);

    // grid extent
    float halfExtentRaw = std::max(m_posExt * 4.0f, 1e-3f);
    m_gridHalfExtent = std::ceil(halfExtentRaw / std::max(m_majorSpacing, 1e-15f)) * m_majorSpacing;
    m_gridFadeRadius = m_posExt * 1.2f;

    // minor line fade based on phase
    float fade = std::max(0.0f, std::min(1.0f, 1.0f - m_phaseT));
    float minorAlphaF = (float(GRID_MAJOR_ALPHA_I) / 255.0f) * std::pow(fade, 3.0f);

    // ── grid planes ─────────────────────────────────────────────
    float planeW[3] = {m_planeWeights.xoy, m_planeWeights.xoz, m_planeWeights.yoz};
    for (int pi = 0; pi < 3; ++pi) {
        if (planeW[pi] <= 1e-4f) continue;

        float majorAlpha = (float(GRID_MAJOR_ALPHA_I) / 255.0f) * planeW[pi];
        buildGridPlane(pi, m_majorSpacing, m_gridHalfExtent, m_gridFadeRadius,
                       0, 0.0f, 1.0f, 1.0f, majorAlpha);

        if (minorAlphaF > 1e-5f) {
            float minorA = minorAlphaF * planeW[pi];
            buildGridPlane(pi, m_minorSpacing, m_gridHalfExtent, m_gridFadeRadius,
                           5, 0.0f, 1.0f, 1.0f, minorA);
        }
    }

    // ── axis quads (thick lines via triangles) ────────────────────
    buildAxisQuads();

    // ── arrow billboards ────────────────────────────────────────
    buildArrowBillboards();

    // ── tick lines ──────────────────────────────────────────────
    buildTickLines();
}

// ── public: update ──────────────────────────────────────────────

void GridRenderer::update(double distance, double sceneScale,
                          double elevation, double azimuth,
                          float orthoBlend) {
    if (!m_initialized) return;

    double effScale = std::max(sceneScale, 1e-9);
    bool changed = (std::abs(distance  - m_lastDistance)  > distance * 0.002 ||
                    std::abs(effScale   - m_lastScale)     > m_lastScale * 0.002 ||
                    std::abs(elevation  - m_lastElevation) > 0.5  ||
                    std::abs(azimuth    - m_lastAzimuth)   > 0.5  ||
                    std::abs(orthoBlend - m_lastOrthoBlend) > 0.005);
    if (!changed) return;

    m_lastDistance   = distance;
    m_lastScale      = effScale;
    m_lastElevation  = elevation;
    m_lastAzimuth    = azimuth;
    m_lastOrthoBlend = orthoBlend;

    rebuildGeometry(distance, sceneScale, elevation, azimuth, orthoBlend);

    m_lineVBO.bind();
    m_lineVBO.allocate(m_lineVerts.data(), int(m_lineVerts.size() * sizeof(Vertex)));
    m_lineVBO.release();

    m_triVBO.bind();
    m_triVBO.allocate(m_triVerts.data(), int(m_triVerts.size() * sizeof(Vertex)));
    m_triVBO.release();
}

// ── public: render ──────────────────────────────────────────────

void GridRenderer::render(const QMatrix4x4& mvpMatrix) {
    if (!m_initialized) return;

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.bind();
    m_shader.setUniformValue("mvpMatrix", mvpMatrix);

    // draw lines (grid + axes + ticks)
    if (!m_lineVerts.empty()) {
        m_gl->glLineWidth(1.5f);
        m_lineVAO.bind();
        m_gl->glDrawArrays(GL_LINES, 0, GLsizei(m_lineVerts.size()));
        m_lineVAO.release();
    }

    // draw triangles (arrow billboards)
    if (!m_triVerts.empty()) {
        m_triVAO.bind();
        m_gl->glDrawArrays(GL_TRIANGLES, 0, GLsizei(m_triVerts.size()));
        m_triVAO.release();
    }

    m_shader.release();
}

// ── public: renderLabels (QPainter overlay) ─────────────────────

void GridRenderer::renderLabels(QPainter* painter, const QMatrix4x4& mvpMatrix,
                                int viewportW, int viewportH) {
    if (!painter) return;

    auto project = [&](const QVector3D& p) -> QPointF {
        QVector4D clip = mvpMatrix * QVector4D(p, 1.0f);
        if (std::abs(clip.w()) < 1e-6f) return QPointF(-9999, -9999);
        float ndcX = clip.x() / clip.w();
        float ndcY = clip.y() / clip.w();
        return QPointF(
            double((ndcX + 1.0f) * 0.5f * float(viewportW)),
            double((1.0f - ndcY) * 0.5f * float(viewportH))
        );
    };

    painter->setRenderHint(QPainter::Antialiasing, true);

    // ── axis name labels (X, Y, Z) ─────────────────────────────
    float arrowLen = m_axisLength * 0.05f;
    float tipDist  = m_posExt + arrowLen;

    struct LabelDef {
        QString text;
        QVector3D tipPos;
        QVector3D axisOrigin;
        QColor baseColor;
        float visibility;
    };
    std::array<LabelDef, 3> labels = {{
        {"X", QVector3D(tipDist, 0, 0), QVector3D(0,0,0), QColor(255, 0, 0, 255), m_axisLabelVis.x},
        {"Y", QVector3D(0, tipDist, 0), QVector3D(0,0,0), QColor(0, 255, 0, 255), m_axisLabelVis.y},
        {"Z", QVector3D(0, 0, tipDist), QVector3D(0,0,0), QColor(0, 0, 255, 255), m_axisLabelVis.z},
    }};

    QFont labelFont("Arial", 13);
    painter->setFont(labelFont);
    QFontMetrics fm(labelFont);
    float screenOffset = 16.0f;

    for (auto& lb : labels) {
        float depth = computeSubmersionFactor(lb.tipPos, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength);
        QColor c = applyVisibilityToColor(lb.baseColor, lb.visibility, depth);
        if (c.alpha() <= 0) continue;

        QPointF spTip = project(lb.tipPos);
        QPointF spOrg = project(lb.axisOrigin);
        if (spTip.x() < -200 || spTip.x() > viewportW + 200) continue;

        QPointF dir = spTip - spOrg;
        double dirLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());

        QSize ts = fm.size(0, lb.text);
        QPointF drawPos;
        if (dirLen > 1.0) {
            QPointF dirN(dir.x() / dirLen, dir.y() / dirLen);
            QPointF labelCenter = spTip + dirN * screenOffset;
            drawPos = labelCenter + QPointF(-ts.width() * 0.5, ts.height() * 0.35);
        } else {
            drawPos = spTip + QPointF(-ts.width() * 0.5, ts.height() * 0.35);
        }

        painter->setPen(c);
        painter->drawText(drawPos, lb.text);
    }

    // ── tick labels (three axes) ────────────────────────────────
    QFont tickFont("Arial", 13);
    painter->setFont(tickFont);
    QFontMetrics tfm(tickFont);
    QColor tickBaseColor(180, 180, 180, 200);
    float fadeStart = m_negExt * 0.5f;
    float tickHalf = m_axisLength * TICK_LINE_LENGTH_RATIO;

    struct TickAxisDef { int mainAx; int perpA; float vis; };
    std::array<TickAxisDef, 3> tickAxes = {{
        {0, 1, m_axisLabelVis.x},
        {1, 0, m_axisLabelVis.y},
        {2, 0, m_axisLabelVis.z},
    }};

    auto setComp = [](QVector3D& p, int idx, float v) {
        if (idx == 0) p.setX(v); else if (idx == 1) p.setY(v); else p.setZ(v);
    };

    for (auto& ta : tickAxes) {
        float val = m_majorSpacing;
        while (val <= m_posExt + 1e-9f) {
            for (int si = 0; si < 2; ++si) {
                float signVal = (si == 0) ? val : -val;
                if (si == 1 && std::abs(val) < 1e-9f) continue;
                if (signVal < fadeStart - 1e-9f || signVal > m_posExt + 1e-9f) continue;

                QVector3D lblPos(0, 0, 0);
                setComp(lblPos, ta.mainAx, signVal);
                setComp(lblPos, ta.perpA, tickHalf * 2.5f);

                float depth = computeSubmersionFactor(lblPos, m_dominantPlane, m_planeWeights, m_camDir, m_axisLength);
                QColor c = applyVisibilityToColor(tickBaseColor, ta.vis, depth);
                if (c.alpha() <= 0) continue;

                QPointF sp = project(lblPos);
                if (sp.x() < -50 || sp.x() > viewportW + 50 ||
                    sp.y() < -50 || sp.y() > viewportH + 50) continue;

                QString txt = formatTickValue(signVal);
                QSize ts = tfm.size(0, txt);

                painter->setPen(c);
                painter->drawText(sp + QPointF(-ts.width() * 0.5, ts.height() * 0.35), txt);
            }
            val += m_majorSpacing;
        }
    }
}
