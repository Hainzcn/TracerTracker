#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QTimer>
#include <QHBoxLayout>

// ============================================================
// MainWindow.h — TracerTracker 主窗口
//
// 布局（从上到下）：
//   ToolBar (32px)
//   Viewer3D (展开，子控件浮动其上)
//   DebugConsole (可折叠)
//   StatusBar (28px)
//
// 浮动子控件（Viewer3D 内部）：
//   AttitudeWidget       — 左上，滑入/出动画
//   SensorChartPanel     — 紧接 AttitudeWidget 下方
//   SensorInfoOverlay    — 右下角
//   ViewOrientationGizmo — 右上角
//   ProjectionToggleBtn  — Gizmo 正下方
//
// 热区（AttitudePanelHotZone）：贴左边缘，触发面板滑入/出
// ============================================================

class Viewer3D;
class ToolBar;
class DebugConsole;
class AttitudeWidget;
class SensorChartPanel;
class SensorInfoOverlay;
class ViewOrientationGizmo;
class DataReceiver;
class PoseProcessor;
class AttitudePanelHotZone;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent* ev) override;
    void closeEvent(QCloseEvent* ev)   override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    // 接收来自 DataReceiver 的解析数据
    void onDataReceived(const QString& source, const QString& prefix,
                        const QList<double>& data);
    // 位置更新 → 更新 Viewer3D 点
    void onPoseUpdated(const QString& name, double x, double y, double z);
    // 速度更新 → 更新 SensorInfoOverlay
    void onVelocityUpdated(double vx, double vy, double vz);
    // 滤波四元数更新 → 更新 AttitudeWidget
    void onFilterQuaternionsUpdated(const QList<double>& madgwickQ,
                                    const QList<double>& mahonyQ);
    // PoseProcessor 解析数据信号
    void onParsedDataUpdated(const QString& source, const QString& prefix,
                             const QList<double>& linearAcc,
                             const QList<double>& gyr,
                             const QList<double>& mag);
    // 日志消息转发
    void onPoseLog(const QString& msg);
    void onViewerLog(const QString& msg);
    // 全路径/尾迹复选框
    void toggleFullPathMode(bool checked);
    void toggleTrailMode(bool checked);
    void onTrailLengthChanged(int value);
    // 状态超时检测（每 1s 触发）
    void checkStatusTimeout();
    // 面板动画完成
    void onAttitudePanelAnimFinished();
    void onSensorChartPanelAnimFinished();
    // 序列停止后清场
    void clearScene();

public slots:
    // 切换 AttitudeWidget 显隐（带动画）
    void toggleAttitudePanel();
    // 切换 SensorChartPanel 显隐（带动画）
    void toggleSensorChartPanel();

private:
    // ── 布局搭建 ──
    void buildStatusBar();
    // ── 叠加层定位 ──
    void repositionOverlays();
    void syncAttitudeOverlayGeometry();
    void syncSensorChartGeometry();
    // ── 面板位置计算 ──
    QPoint attitudeVisiblePos() const;
    QPoint attitudeHiddenPos()  const;
    QPoint chartVisiblePos()    const;
    QPoint chartHiddenPos()     const;
    // ── 场景点更新 ──
    void updateOverlays(const QList<double>& data);
    void updateSensorCharts(const QList<double>& data);

    // ── 核心对象 ──
    DataReceiver*        m_dataReceiver    = nullptr;
    PoseProcessor*       m_poseProcessor   = nullptr;

    // ── UI 组件 ──
    Viewer3D*            m_viewer          = nullptr;
    ToolBar*             m_toolbar         = nullptr;
    DebugConsole*        m_debugConsole    = nullptr;
    AttitudeWidget*      m_attitudeWidget  = nullptr;
    SensorChartPanel*    m_sensorChart     = nullptr;
    SensorInfoOverlay*   m_sensorOverlay   = nullptr;
    ViewOrientationGizmo* m_gizmo          = nullptr;
    QPushButton*         m_projToggleBtn   = nullptr;
    AttitudePanelHotZone* m_attitudeHotzone = nullptr;
    AttitudePanelHotZone* m_chartHotzone    = nullptr;

    // ── 状态栏控件 ──
    QWidget*   m_statusBarWidget   = nullptr;
    QLabel*    m_udpStatusLabel    = nullptr;
    QLabel*    m_serialStatusLabel = nullptr;
    QCheckBox* m_debugCheckbox     = nullptr;
    QCheckBox* m_fullPathCheckbox  = nullptr;
    QCheckBox* m_trailCheckbox     = nullptr;
    QLabel*    m_trailLengthLabel  = nullptr;
    QSpinBox*  m_trailLengthSpin   = nullptr;

    // ── 面板动画 ──
    QPropertyAnimation* m_attitudePanelAnim    = nullptr;
    QPropertyAnimation* m_sensorChartPanelAnim = nullptr;
    bool m_attitudePanelExpanded    = false;
    bool m_sensorChartPanelExpanded = false;

    // ── 状态计时 ──
    QTimer* m_statusTimer    = nullptr;
    qint64  m_lastUdpTime    = 0;
    qint64  m_lastSerialTime = 0;

    // ── 配置缓存 ──
    bool m_hasQuaternionPoint = false;

    static constexpr int ATTITUDE_PANEL_MARGIN     = 10;
    static constexpr int CHART_PANEL_SPACING       = 10;
    static constexpr int ATTITUDE_HOTZONE_WIDTH    = 10;
    static constexpr int ATTITUDE_HOTZONE_PADDING  = 10;
    static constexpr int ATTITUDE_PANEL_ANIM_MS    = 180;
};

// ── AttitudePanelHotZone ──────────────────────────────────────

// 贴靠 Viewer3D 左侧的透明热区，hover 时显示拉条动效
class AttitudePanelHotZone : public QWidget {
    Q_OBJECT
public:
    explicit AttitudePanelHotZone(QWidget* parent = nullptr);

    static constexpr int VISIBLE_WIDTH = 10;

signals:
    void clicked();

protected:
    void enterEvent(QEnterEvent* ev)   override;
    void leaveEvent(QEvent* ev)        override;
    void mousePressEvent(QMouseEvent* ev)   override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev)    override;
    void wheelEvent(QWheelEvent* ev)        override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void paintEvent(QPaintEvent* ev) override;

private:
    void animateBg(double target);
    void animateStrip(double target);

    double m_bgAlpha       = 0.0;
    double m_stripProgress = 0.0;
    bool   m_pressed       = false;

    QVariantAnimation* m_bgAnim    = nullptr;
    QVariantAnimation* m_stripAnim = nullptr;
};
