#pragma once
#include "FramelessWindow.h"
#include "ins/MathUtils.h"
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QTimer>

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

class SideBar;
class Viewer3D;
class ToolBar;
class DebugConsole;
class AttitudeWidget;
class SensorChartPanel;
class SensorInfoOverlay;
class ViewOrientationGizmo;
class DataReceiver;
class PoseProcessor;

class MainWindow : public FramelessWindow {
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
    void onFilterQuaternionsUpdated(const Quat4d& madgwickQ,
                                    const Quat4d& mahonyQ);
    void onParsedDataUpdated(const QString& source, const QString& prefix,
                             const Vec3d& linearAcc,
                             const Vec3d& gyr,
                             const Vec3d& mag);
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
    // ── 场景点更新（通过 points 配置提取数据，无硬编码索引）──
    void updateOverlays(const QString& source, const QString& prefix,
                        const QList<double>& data);
    void updateSensorCharts(const QString& source, const QString& prefix,
                            const QList<double>& data);

    // ── 核心对象 ──
    DataReceiver*        m_dataReceiver    = nullptr;
    PoseProcessor*       m_poseProcessor   = nullptr;

    // ── UI 组件 ──
    SideBar*             m_sideBar         = nullptr;
    Viewer3D*            m_viewer          = nullptr;
    ToolBar*             m_toolbar         = nullptr;
    DebugConsole*        m_debugConsole    = nullptr;
    AttitudeWidget*      m_attitudeWidget  = nullptr;
    SensorChartPanel*    m_sensorChart     = nullptr;
    SensorInfoOverlay*   m_sensorOverlay   = nullptr;
    ViewOrientationGizmo* m_gizmo          = nullptr;
    QPushButton*         m_projToggleBtn   = nullptr;

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
    bool m_hasQuaternionSensor = false;

    static constexpr int ATTITUDE_PANEL_MARGIN     = 10;
    static constexpr int CHART_PANEL_SPACING       = 10;
    static constexpr int ATTITUDE_PANEL_ANIM_MS    = 180;
};
