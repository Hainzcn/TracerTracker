#include "ViewOrientationGizmo.h"
#include "../ui/opengl/Viewer3D.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

// ============================================================
// ViewOrientationGizmo.cpp — 视角罗盘实现
// ============================================================

// 轴定义：方向、颜色、标签、预设视角（度）
const std::array<ViewOrientationGizmo::AxisDef, 3> ViewOrientationGizmo::AXES = {{
    // X 轴：正→右视图（仰角0,方位-90），负→左视图（仰角0,方位90）
    {1,0,0, QColor(240,60,80),  "X",   0,-90,   0,90,   false},
    // Y 轴：正→后视图（仰角0,方位180），负→前视图（仰角0,方位0）
    {0,1,0, QColor(140,200,60), "Y",   0,180,   0,0,    false},
    // Z 轴：正→俯视图（仰角90），负→仰视图（仰角-90，方位角继承）
    {0,0,1, QColor(60,140,240), "Z",  90,0,   -90,0,    true},
}};

ViewOrientationGizmo::ViewOrientationGizmo(Viewer3D* viewer, QWidget* parent)
    : QWidget(parent), m_viewer(viewer)
{
    setFixedSize(SIZE, SIZE);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::ArrowCursor);
    setMouseTracking(true);

    m_labelFont = QFont("Arial", 10, QFont::Bold);
}

// 同步当前相机朝向并触发重绘
void ViewOrientationGizmo::updateOrientation() {
    if (m_viewer) {
        auto params = m_viewer->cameraParams();
        m_elevation = float(params.value("elevation", 30.0).toDouble());
        m_azimuth   = float(params.value("azimuth",  -135.0).toDouble());
    }
    update();
}

// 将世界单位方向投影到 gizmo 屏幕坐标（与 Viewer3D viewMatrix 相同旋转顺序）
std::tuple<float,float,float,float> ViewOrientationGizmo::project(
    float dx, float dy, float dz) const
{
    float e = float(M_PI / 180.0) * m_elevation;
    float a = float(M_PI / 180.0) * m_azimuth;

    float cosA = std::cos(a), sinA = std::sin(a);
    float sinE = std::sin(e), cosE = std::cos(e);

    // R_z(azimuth)
    float x1 = dx * cosA - dy * sinA;
    float y1 = dx * sinA + dy * cosA;
    float z1 = dz;

    // R_x(elevation - 90)
    float x2 = x1;
    float y2 =  y1 * sinE + z1 * cosE;
    float z2 = -y1 * cosE + z1 * sinE;

    float half = SIZE / 2.0f;
    float r    = half * AXIS_LENGTH;

    // 透视缩放
    float scale = CAMERA_DIST / (CAMERA_DIST - z2);

    return {half + x2 * r * scale, half - y2 * r * scale, z2, scale};
}

// 构建已投影端点列表（按深度从后到前排序）
QList<ViewOrientationGizmo::Endpoint> ViewOrientationGizmo::buildEndpoints() const {
    QList<Endpoint> eps;

    for (int i = 0; i < (int)AXES.size(); ++i) {
        const auto& ax = AXES[i];
        // 正端点
        auto [px,py,pd,ps] = project(ax.dx, ax.dy, ax.dz);
        QColor posCol = ax.color;
        if (pd < 0) {
            // 背面：降低饱和度和亮度
            float factor = std::max(0.3f, 1.0f + pd * 0.3f);
            posCol.setHsvF(posCol.hueF(), posCol.saturationF()*factor,
                           posCol.valueF()*factor, posCol.alphaF());
        }
        eps << Endpoint{px,py,pd,ps, posCol, ax.label, true, QString("+") + ax.label.toLower(), i};

        // 负端点
        auto [nx,ny,nd,ns] = project(-ax.dx, -ax.dy, -ax.dz);
        QColor negCol = ax.color;
        if (nd < 0) {
            float factor = std::max(0.3f, 1.0f + nd * 0.3f);
            negCol.setHsvF(negCol.hueF(), negCol.saturationF()*factor,
                           negCol.valueF()*factor, negCol.alphaF());
        }
        eps << Endpoint{nx,ny,nd,ns, negCol, "", false, QString("-") + ax.label.toLower(), i};
    }

    // 按深度升序（远 → 近）
    std::sort(eps.begin(), eps.end(), [](const Endpoint& a, const Endpoint& b){
        return a.depth < b.depth;
    });
    return eps;
}

// 命中测试（从前到后）
std::optional<ViewOrientationGizmo::HitResult> ViewOrientationGizmo::hitTest(
    const QPointF& pos) const
{
    QList<Endpoint> eps = buildEndpoints();
    // 从最前端开始检测
    for (int i = eps.size() - 1; i >= 0; --i) {
        const auto& ep = eps[i];
        float r  = BASE_RADIUS * ep.scale + 2.0f;
        float dx = float(pos.x()) - ep.sx;
        float dy = float(pos.y()) - ep.sy;
        if (dx*dx + dy*dy <= r*r)
            return HitResult{ep.key, ep.axisIdx, ep.positive};
    }
    return std::nullopt;
}

// ── paintEvent ───────────────────────────────────────────────

void ViewOrientationGizmo::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    float cx = SIZE / 2.0f, cy = SIZE / 2.0f;

    // 悬停背景圆
    if (m_hoveringBg) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(128, 128, 128, 60));
        float br = cx * BG_RADIUS_RATIO;
        p.drawEllipse(QPointF(cx, cy), br, br);
    }

    QList<Endpoint> eps = buildEndpoints();

    for (const auto& ep : eps) {
        QColor col = ep.color;
        bool hovered = (ep.key == m_hoveredKey);
        if (hovered) col = col.lighter(130);

        // 连接线（仅正轴）
        if (ep.positive) {
            QPen pen(col, 2.5f * ep.scale);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.drawLine(QPointF(cx,cy), QPointF(ep.sx, ep.sy));
        }

        float r = BASE_RADIUS * ep.scale;

        if (ep.positive) {
            // 实心圆 + 标签
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawEllipse(QPointF(ep.sx, ep.sy), r, r);

            if (!ep.label.isEmpty()) {
                QFont font = m_labelFont;
                float baseSize = (font.pointSizeF() > 0)
                    ? font.pointSizeF() : float(font.pointSize());
                font.setPointSizeF(std::max(1.0f, baseSize * ep.scale));
                p.setFont(font);
                p.setPen(hovered ? QColor(255,255,255) : QColor(20,20,20));
                QRectF tr = p.fontMetrics().boundingRect(ep.label);
                p.drawText(QPointF(ep.sx - tr.width()/2.0f,
                                   ep.sy + tr.height()/2.0f - 2),
                           ep.label);
            }
        } else {
            // 空心圆（负轴）
            QPen pen(col, 2.0f * ep.scale);
            p.setPen(pen);
            p.setBrush(hovered ? QBrush(col.lighter(130))
                               : QBrush(QColor(40,40,40,200)));
            p.drawEllipse(QPointF(ep.sx, ep.sy), r, r);
        }
    }
}

// ── 鼠标事件 ─────────────────────────────────────────────────

void ViewOrientationGizmo::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) { ev->ignore(); return; }

    QPointF pos = ev->position();
    float cx = SIZE / 2.0f, cy = SIZE / 2.0f;
    float dx = float(pos.x()) - cx, dy = float(pos.y()) - cy;
    float bgR = cx * BG_RADIUS_RATIO;

    if (dx*dx + dy*dy <= bgR*bgR) {
        m_pressedHit  = hitTest(pos);
        m_lastMousePos = pos;
        ev->accept();
    } else {
        ev->ignore();
    }
}

void ViewOrientationGizmo::mouseMoveEvent(QMouseEvent* ev) {
    QPointF pos = ev->position();
    float cx = SIZE / 2.0f, cy = SIZE / 2.0f;
    float dx = float(pos.x()) - cx, dy = float(pos.y()) - cy;
    float bgR = cx * BG_RADIUS_RATIO;
    bool inside = (dx*dx + dy*dy <= bgR*bgR);

    bool needUpdate = false;
    if (inside != m_hoveringBg) { m_hoveringBg = inside; needUpdate = true; }

    // 拖动旋转
    if ((ev->buttons() & Qt::LeftButton) && m_lastMousePos.has_value() && m_viewer) {
        QPointF diff = pos - m_lastMousePos.value();
        m_viewer->orbit(int(diff.x()), int(diff.y()));
        m_viewer->update();
        m_lastMousePos = pos;
        updateOrientation();
        if (diff.manhattanLength() > 2) m_pressedHit.reset();
        ev->accept();
        return;
    }

    // 悬停高亮
    auto hit = hitTest(pos);
    QString newKey = hit.has_value() ? hit->key : QString();
    if (newKey != m_hoveredKey) {
        m_hoveredKey = newKey;
        setCursor(newKey.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        needUpdate = true;
    }
    if (needUpdate) update();
    ev->ignore();
}

void ViewOrientationGizmo::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton || !m_lastMousePos.has_value()) {
        ev->ignore(); return;
    }

    QPointF pos = ev->position();
    float cx = SIZE / 2.0f, cy = SIZE / 2.0f;
    float dx = float(pos.x()) - cx, dy = float(pos.y()) - cy;
    float bgR = cx * BG_RADIUS_RATIO;

    if (m_pressedHit.has_value() && dx*dx + dy*dy <= bgR*bgR) {
        const auto& hit = m_pressedHit.value();
        const auto& ax  = AXES[hit.axisIdx];

        float elev, azim;
        if (hit.positive) {
            elev = ax.presetPosElev;
            azim = ax.negAzimInherit ? m_azimuth : ax.presetPosAzim;
        } else {
            elev = ax.presetNegElev;
            azim = ax.negAzimInherit ? m_azimuth : ax.presetNegAzim;
        }

        // 若已经在目标视角附近，翻转到对面
        auto isClose = [](float a, float b) {
            float d = std::fmod(a - b, 360.0f);
            if (d > 180) d -= 360; if (d < -180) d += 360;
            return std::abs(d) < 1.0f;
        };
        if (isClose(m_elevation, elev) && isClose(m_azimuth, azim)) {
            // 翻转到相对面
            if (hit.positive) {
                elev = ax.presetNegElev;
                azim = ax.negAzimInherit ? m_azimuth : ax.presetNegAzim;
            } else {
                elev = ax.presetPosElev;
                azim = ax.negAzimInherit ? m_azimuth : ax.presetPosAzim;
            }
        }
        emit viewSelected(elev, azim);
    }

    m_pressedHit.reset();
    m_lastMousePos.reset();
    ev->accept();
}

void ViewOrientationGizmo::leaveEvent(QEvent*) {
    bool nu = false;
    if (!m_hoveredKey.isEmpty()) { m_hoveredKey.clear(); setCursor(Qt::ArrowCursor); nu = true; }
    if (m_hoveringBg)            { m_hoveringBg = false; nu = true; }
    if (nu) update();
}

// 滚轮穿透到 Viewer3D
void ViewOrientationGizmo::wheelEvent(QWheelEvent* ev) { ev->ignore(); }
