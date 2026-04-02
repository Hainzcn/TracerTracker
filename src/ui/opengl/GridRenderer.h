#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>
#include <QColor>
#include <array>
#include <vector>

class GridRenderer {
public:
    explicit GridRenderer(QOpenGLFunctions_3_3_Core* gl);
    ~GridRenderer();

    void initialize();

    void update(double distance, double sceneScale,
                double elevation, double azimuth,
                float orthoBlend = 0.0f);

    void render(const QMatrix4x4& mvpMatrix);

    void renderLabels(QPainter* painter, const QMatrix4x4& mvpMatrix,
                      int viewportW, int viewportH);

private:
    QOpenGLFunctions_3_3_Core* m_gl;
    QOpenGLShaderProgram       m_shader;

    QOpenGLBuffer              m_lineVBO;
    QOpenGLVertexArrayObject   m_lineVAO;

    QOpenGLBuffer              m_triVBO;
    QOpenGLVertexArrayObject   m_triVAO;

    bool m_initialized = false;

    struct Vertex { float x, y, z, r, g, b, a; };
    std::vector<Vertex> m_lineVerts;
    std::vector<Vertex> m_triVerts;

    // cached parameters to avoid redundant rebuilds
    double m_lastDistance   = -1.0;
    double m_lastScale      = -1.0;
    double m_lastElevation  = -1000.0;
    double m_lastAzimuth    = -1000.0;
    float  m_lastOrthoBlend = -1.0f;

    // ── visual constants (matching Python original) ─────────────

    static constexpr float AXIS_VISUAL_RATIO       = 0.5f;
    static constexpr float TICK_LINE_LENGTH_RATIO   = 0.02f;
    static constexpr int   GRID_MAJOR_ALPHA_I       = 40;
    static constexpr float AXIS_LABEL_FADE_POWER    = 6.0f;
    static constexpr float ORTHO_SWITCH_BAND        = 0.16f;
    static constexpr float SUBMERSION_DESAT_MAX     = 0.72f;
    static constexpr float SUBMERSION_DIM_MAX       = 0.45f;
    static constexpr float SUBMERSION_DEPTH_RANGE   = 0.85f;

    // ── cached per-frame state ──────────────────────────────────

    float m_axisLength  = 20.0f;
    float m_posExt      = 20.0f;
    float m_negExt      = -10.0f;
    float m_majorSpacing = 5.0f;
    float m_minorSpacing = 1.0f;
    float m_phaseT       = 0.5f;
    float m_gridHalfExtent = 50.0f;
    float m_gridFadeRadius = 24.0f;

    QVector3D m_camDir;
    struct PlaneWeights { float xoy, xoz, yoz; };
    PlaneWeights m_planeWeights{1,0,0};
    int m_dominantPlane = 0; // 0=xoy, 1=xoz, 2=yoz
    struct AxisVisibility { float x, y, z; };
    AxisVisibility m_axisLabelVis{1,1,1};

    // ── geometry builders ───────────────────────────────────────

    void rebuildGeometry(double distance, double sceneScale,
                         double elevation, double azimuth,
                         float orthoBlend);

    void addLine(const QVector3D& a, const QVector3D& b,
                 float r, float g, float b_, float alpha);
    void addTri(const QVector3D& p0, const QVector3D& p1, const QVector3D& p2,
                float r, float g, float b_, float alpha);

    void buildGridPlane(int planeIdx, float spacing, float halfExtent,
                        float fadeRadius, int skipMultiple,
                        float baseR, float baseG, float baseB, float baseA);

    void buildAxisLines();
    void buildArrowBillboards();
    void buildTickLines();

    // ── math helpers ────────────────────────────────────────────

    struct GridSpacings { float minor, major, phase; };
    static GridSpacings computeGridSpacings(float viewRange, int targetMinorCount = 15);

    static QVector3D cameraDirection(double elevation, double azimuth);

    PlaneWeights computePlaneWeights(const QVector3D& camDir, float orthoBlend) const;
    static int dominantPlane(const PlaneWeights& w);

    AxisVisibility computeAxisLabelVisibility(const QVector3D& camDir) const;

    float computeSubmersionFactor(const QVector3D& point, int domPlane,
                                  const PlaneWeights& weights,
                                  const QVector3D& camDir, float axLen) const;

    struct RGBA { float r, g, b, a; };
    static RGBA applySubmersion(RGBA color, float depthFactor);
    static QColor applyVisibilityToColor(const QColor& base, float visibility,
                                         float depthFactor = 0.0f);

    static QString formatTickValue(float v);

    static const char* VERT_SHADER_SRC;
    static const char* FRAG_SHADER_SRC;
};
