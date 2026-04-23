#include "MainWindow.h"
#include "Styles.h"
#include "ToolBar.h"
#include "SideBar.h"
#include "DebugConsole.h"
#include "AttitudeWidget.h"
#include "SensorChartPanel.h"
#include "SensorInfoOverlay.h"
#include "ViewOrientationGizmo.h"
#include "ProtocolConfigPanel.h"
#include "opengl/Viewer3D.h"
#include "../io/DataReceiver.h"
#include "../ins/PoseProcessor.h"
#include "../config/ConfigLoader.h"

#include <QSizePolicy>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QEasingCurve>
#include <QDateTime>
#include <QAbstractAnimation>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QEnterEvent>

// ============================================================
// MainWindow.cpp — 主窗口实现
// ============================================================

// ── MainWindow ────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : FramelessWindow(parent)
{
    setWindowTitle("TracerTracker");
    resize(1280, 720);

    // ── 核心数据对象 ──
    m_dataReceiver  = new DataReceiver(this);
    m_poseProcessor = new PoseProcessor(this);

    auto* layout = contentLayout();

    // ── 工具栏（添加到基类顶栏左侧）──
    m_toolbar = new ToolBar(this);
    m_toolbar->bindDataReceiver(m_dataReceiver);
    connect(m_toolbar, &ToolBar::serialStopRequested, this, &MainWindow::clearScene);
    toolBarLayout()->insertWidget(0, m_toolbar);

    // ── 主内容区 (水平布局) ──
    auto* mainHLayout = new QHBoxLayout();
    mainHLayout->setContentsMargins(0, 0, 0, 0);
    mainHLayout->setSpacing(0);
    layout->addLayout(mainHLayout, 1);

    // ── 左侧栏 ──
    m_sideBar = new SideBar(this);
    mainHLayout->addWidget(m_sideBar);

    connect(m_sideBar->infoBtn(), &QPushButton::clicked, this, [this](bool checked) {
        if (m_attitudePanelExpanded != checked) toggleAttitudePanel();
        if (m_sensorChartPanelExpanded != checked) toggleSensorChartPanel();
    });

    connect(m_sideBar->configBtn(), &QPushButton::clicked, this, [this]() {
        toggleConfigPanel();
    });

    connect(m_sideBar->settingsBtn(), &QPushButton::clicked, this, [this]() {
        onViewerLog("Settings button clicked (Placeholder)");
    });

    // ── 右侧内容区 (垂直布局) ──
    auto* rightVLayout = new QVBoxLayout();
    rightVLayout->setContentsMargins(0, 0, 0, 0);
    rightVLayout->setSpacing(0);
    mainHLayout->addLayout(rightVLayout, 1);

    // ── 3D 视口 ──
    m_viewer = new Viewer3D(this);
    m_viewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_viewer, &Viewer3D::logMessage, this, &MainWindow::onViewerLog);
    rightVLayout->addWidget(m_viewer, 1);

    // ── 浮动子控件（父为 Viewer3D）──
    m_attitudeWidget = new AttitudeWidget(m_viewer);
    m_attitudeWidget->setVisible(false);

    m_sensorChart = new SensorChartPanel(m_viewer);
    m_sensorChart->setVisible(false);

    m_sensorOverlay = new SensorInfoOverlay(m_viewer);

    m_gizmo = new ViewOrientationGizmo(m_viewer, m_viewer);
    connect(m_viewer, &Viewer3D::cameraChanged, m_gizmo, &ViewOrientationGizmo::updateOrientation);
    connect(m_gizmo, &ViewOrientationGizmo::viewSelected, m_viewer, &Viewer3D::animateToView);
    m_gizmo->show();

    m_projToggleBtn = new QPushButton("透视", m_viewer);
    m_projToggleBtn->setAttribute(Qt::WA_TranslucentBackground);
    m_projToggleBtn->setStyleSheet(Styles::STYLE_PROJECTION_BTN());
    m_projToggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_projToggleBtn, &QPushButton::clicked, m_viewer, &Viewer3D::toggleProjection);
    connect(m_viewer, &Viewer3D::projectionModeChanged, this, [this](bool isOrtho){
        m_projToggleBtn->setText(isOrtho ? "正交" : "透视");
    });
    m_projToggleBtn->show();

    // ── 协议配置面板（浮动在 Viewer3D 左侧）──
    m_configPanel = new ProtocolConfigPanel(m_viewer);
    m_configPanel->setVisible(false);
    connect(m_configPanel, &ProtocolConfigPanel::closeRequested, this, &MainWindow::toggleConfigPanel);
    connect(m_configPanel, &ProtocolConfigPanel::applied, this, [this]() {
        m_hasQuaternionSensor = ConfigLoader::instance().hasSensorPurpose(PointPurpose::Quaternion);
        onViewerLog("协议配置已应用");
    });

    // ── 面板动画 ──
    // 协议配置面板覆盖在 QOpenGLWidget (Viewer3D) 之上。Qt6 已知：
    //   "Once a QOpenGLWidget is added to a hierarchy, the entire top-level
    //    window switches to OpenGL-based compositing. The window's backing
    //    store is not always correctly invalidated by the underlying platform
    //    when the child widget moves/hides."
    // 因此当面板 hide/move 出 viewer 范围后，OpenGL 合成层会保留上一帧子
    // 控件的像素，必须由我们主动驱动 viewer 重绘 + 收尾时同步 repaint 才
    // 能彻底擦除。X 关闭路径之所以"看起来正常"，是因为 closeBtn 在面板
    // 内部触发的 hover-leave / focus-out 链恰好顺带刷新了一次 viewer；
    // 而点击侧栏按钮关闭时，鼠标全程在 viewer 之外，没有副作用刷新，
    // 残留就裸露出来了。
    m_configPanelAnim = new QPropertyAnimation(m_configPanel, "pos", this);
    m_configPanelAnim->setDuration(200);
    m_configPanelAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_configPanelAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant&) {
        if (m_viewer) m_viewer->update();
    });
    connect(m_configPanelAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_configPanelExpanded) {
            m_configPanel->move(0, 0);
            if (m_viewer) m_viewer->update();
        } else {
            // 关键：hide() 必须发生在 move() 之前。setVisible(false) 会让
            // Qt 主动把子控件从 OpenGL 合成层里摘除并刷新被让出的区域；
            // 反过来若先 move() 到 (-PANEL_WIDTH, 0) 再 hide()，合成器可
            // 能仍然把"上一帧位置"上的子控件像素留在 backing store 里。
            m_configPanel->setVisible(false);
            m_configPanel->move(-ProtocolConfigPanel::PANEL_WIDTH, 0);
            // 收尾必须同步 repaint：update() 只是 schedule，事件循环可能
            // 把它和动画的最后一帧合并成一次 paint，结果落在 hide 之前。
            // repaint() 强制立刻走一次 paintGL，glClear 把 FBO 整个清掉，
            // 之后合成器用最新（已 hide）的子控件状态做合成，确保干净。
            if (m_viewer) {
                m_viewer->repaint();
                // 顶层窗口的 backing store 也可能持有旧像素（Qt6 OpenGL
                // 合成路径下的已知问题），追加一次 update 兜底。
                if (auto* top = m_viewer->window()) top->update();
            }
        }
    });

    m_attitudePanelAnim = new QPropertyAnimation(m_attitudeWidget, "pos", this);
    m_attitudePanelAnim->setDuration(ATTITUDE_PANEL_ANIM_MS);
    m_attitudePanelAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_attitudePanelAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant&) {
        if (m_viewer) m_viewer->update();
    });
    connect(m_attitudePanelAnim, &QPropertyAnimation::finished,
            this, &MainWindow::onAttitudePanelAnimFinished);

    m_sensorChartPanelAnim = new QPropertyAnimation(m_sensorChart, "pos", this);
    m_sensorChartPanelAnim->setDuration(ATTITUDE_PANEL_ANIM_MS);
    m_sensorChartPanelAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_sensorChartPanelAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant&) {
        if (m_viewer) m_viewer->update();
    });
    connect(m_sensorChartPanelAnim, &QPropertyAnimation::finished,
            this, &MainWindow::onSensorChartPanelAnimFinished);

    // ── 信号连接 DataReceiver ──
    connect(m_dataReceiver, &DataReceiver::dataReceived,
            this, &MainWindow::onDataReceived);

    // ── 信号连接 PoseProcessor ──
    connect(m_poseProcessor, &PoseProcessor::positionUpdated,
            this, &MainWindow::onPoseUpdated);
    connect(m_poseProcessor, &PoseProcessor::velocityUpdated,
            this, &MainWindow::onVelocityUpdated);
    connect(m_poseProcessor, &PoseProcessor::parsedDataUpdated,
            this, &MainWindow::onParsedDataUpdated);
    connect(m_poseProcessor, &PoseProcessor::filterQuaternionsUpdated,
            this, &MainWindow::onFilterQuaternionsUpdated);
    connect(m_poseProcessor, &PoseProcessor::logMessage,
            this, &MainWindow::onPoseLog);

    // ── 调试控制台 ──
    m_debugConsole = new DebugConsole(this);
    connect(m_dataReceiver, &DataReceiver::rawDataReceived,
            m_debugConsole, &DebugConsole::onRawDataReceived);
    connect(m_dataReceiver, &DataReceiver::parsedDataReceived,
            m_debugConsole, &DebugConsole::onParsedDataReceived);
    rightVLayout->addWidget(m_debugConsole);

    // ── 状态栏 ──
    buildStatusBar();
    layout->addWidget(m_statusBarWidget);

    // debugConsole 全折叠 → 取消勾选复选框
    connect(m_debugConsole, &DebugConsole::allCollapsed, this, [this](){
        m_debugCheckbox->setChecked(false);
    });

    // ── 全局样式 ──
    setStyleSheet(Styles::MAIN_WINDOW_STYLE());

    // ── 缓存传感器 purpose 存在性 ──
    m_hasQuaternionSensor = ConfigLoader::instance().hasSensorPurpose(PointPurpose::Quaternion);

    // ── 状态超时定时器（1s 周期）──
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::checkStatusTimeout);
    m_statusTimer->start(1000);

    // 延迟定位叠加层（等待布局完成）
    m_viewer->installEventFilter(this);
    QTimer::singleShot(0, this, &MainWindow::repositionOverlays);
}

MainWindow::~MainWindow() {}

// ── 状态栏 ────────────────────────────────────────────────────

void MainWindow::buildStatusBar() {
    m_statusBarWidget = new QWidget(this);
    m_statusBarWidget->setObjectName("statusBar");
    m_statusBarWidget->setStyleSheet(Styles::STATUS_BAR_STYLE());
    m_statusBarWidget->setFixedHeight(28);

    auto* sbl = new QHBoxLayout(m_statusBarWidget);
    sbl->setContentsMargins(10, 0, 10, 0);

    m_udpStatusLabel = new QLabel("⚪ UDP: 无");
    m_udpStatusLabel->setStyleSheet(Styles::STATUS_LABEL_STYLE());
    sbl->addWidget(m_udpStatusLabel);
    sbl->addSpacing(24);

    m_serialStatusLabel = new QLabel("⚪ 串口: 无");
    m_serialStatusLabel->setStyleSheet(Styles::STATUS_LABEL_STYLE());
    sbl->addWidget(m_serialStatusLabel);
    sbl->addStretch();

    m_debugCheckbox = new QCheckBox("调试日志");
    m_debugCheckbox->setStyleSheet(Styles::STYLE_CHECKBOX());
    connect(m_debugCheckbox, &QCheckBox::checkStateChanged,
            m_debugConsole, [this](Qt::CheckState state){
                m_debugConsole->toggleVisibility(state != Qt::Unchecked);
            });
    sbl->addWidget(m_debugCheckbox);

    sbl->addSpacing(16);
    m_fullPathCheckbox = new QCheckBox("绘制路径");
    m_fullPathCheckbox->setStyleSheet(Styles::STYLE_CHECKBOX());
    connect(m_fullPathCheckbox, &QCheckBox::toggled,
            this, &MainWindow::toggleFullPathMode);
    sbl->addWidget(m_fullPathCheckbox);

    sbl->addSpacing(16);
    m_trailCheckbox = new QCheckBox("路径着色");
    m_trailCheckbox->setStyleSheet(Styles::STYLE_CHECKBOX());
    connect(m_trailCheckbox, &QCheckBox::toggled,
            this, &MainWindow::toggleTrailMode);
    sbl->addWidget(m_trailCheckbox);

    // 路径着色依赖绘制路径，未勾选时禁用
    m_trailCheckbox->setEnabled(false);
}

// ── 叠加层定位 ────────────────────────────────────────────────

QPoint MainWindow::attitudeVisiblePos() const {
    return QPoint(ATTITUDE_PANEL_MARGIN, ATTITUDE_PANEL_MARGIN);
}

QPoint MainWindow::attitudeHiddenPos() const {
    return QPoint(-m_attitudeWidget->width() + 50, ATTITUDE_PANEL_MARGIN);
}

QPoint MainWindow::chartVisiblePos() const {
    // AttitudeWidget 下方 CHART_PANEL_SPACING 处
    return QPoint(
        ATTITUDE_PANEL_MARGIN,
        ATTITUDE_PANEL_MARGIN + m_attitudeWidget->height() + CHART_PANEL_SPACING
    );
}

QPoint MainWindow::chartHiddenPos() const {
    return QPoint(-m_sensorChart->width() + 50, chartVisiblePos().y());
}

void MainWindow::syncAttitudeOverlayGeometry() {
    int top = ATTITUDE_PANEL_MARGIN;

    if (m_attitudePanelAnim->state() == QAbstractAnimation::Running) {
        QPoint cur = m_attitudeWidget->pos();
        m_attitudeWidget->move(cur.x(), top);
    } else {
        QPoint target = m_attitudePanelExpanded
            ? attitudeVisiblePos() : attitudeHiddenPos();
        m_attitudeWidget->move(target);
        m_attitudeWidget->setVisible(m_attitudePanelExpanded);
    }
    if (m_attitudeWidget->isVisible()) m_attitudeWidget->raise();
}

void MainWindow::syncSensorChartGeometry() {
    int top = chartVisiblePos().y();

    if (m_sensorChartPanelAnim->state() == QAbstractAnimation::Running) {
        QPoint cur = m_sensorChart->pos();
        m_sensorChart->move(cur.x(), top);
    } else {
        QPoint target = m_sensorChartPanelExpanded
            ? chartVisiblePos() : chartHiddenPos();
        m_sensorChart->move(target);
        m_sensorChart->setVisible(m_sensorChartPanelExpanded);
    }
    if (m_sensorChart->isVisible()) m_sensorChart->raise();
}

void MainWindow::repositionOverlays() {
    int vw = m_viewer->width(), vh = m_viewer->height();
    int mg = ATTITUDE_PANEL_MARGIN;

    syncAttitudeOverlayGeometry();
    syncSensorChartGeometry();

    // 协议配置面板高度跟随 Viewer3D
    if (m_configPanel->isVisible()) {
        m_configPanel->setFixedHeight(vh);
        if (m_configPanelAnim->state() != QAbstractAnimation::Running) {
            m_configPanel->move(m_configPanelExpanded ? 0 : -ProtocolConfigPanel::PANEL_WIDTH, 0);
        }
        m_configPanel->raise();
    }

    // SensorInfoOverlay：右下角
    m_sensorOverlay->adjustSize();
    m_sensorOverlay->move(vw - m_sensorOverlay->width() - mg,
                          vh - m_sensorOverlay->height() - mg);

    // ViewOrientationGizmo：右上角
    m_gizmo->move(vw - m_gizmo->width() - mg, mg);

    // ProjectionToggleBtn：Gizmo 正下方居中
    m_projToggleBtn->adjustSize();
    m_projToggleBtn->move(
        m_gizmo->x() + (m_gizmo->width() - m_projToggleBtn->width()) / 2,
        m_gizmo->y() + m_gizmo->height() + 2
    );
}

// ── 面板滑入/出动画 ───────────────────────────────────────────

void MainWindow::toggleAttitudePanel() {
    QPoint hiddenPos  = attitudeHiddenPos();
    QPoint visiblePos = attitudeVisiblePos();
    QPoint current;

    if (m_attitudePanelAnim->state() == QAbstractAnimation::Running) {
        m_attitudePanelAnim->stop();
        current = m_attitudeWidget->pos();
    } else if (m_attitudeWidget->isVisible()) {
        current = m_attitudeWidget->pos();
    } else {
        current = hiddenPos;
        m_attitudeWidget->move(current);
    }

    m_attitudePanelExpanded = !m_attitudePanelExpanded;
    if (m_sideBar->infoBtn()->isChecked() != m_attitudePanelExpanded) {
        m_sideBar->infoBtn()->setChecked(m_attitudePanelExpanded);
    }
    QPoint target = m_attitudePanelExpanded ? visiblePos : hiddenPos;
    m_attitudePanelAnim->setEasingCurve(m_attitudePanelExpanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);

    m_attitudeWidget->setVisible(true);
    m_attitudeWidget->raise();
    m_attitudePanelAnim->setStartValue(current);
    m_attitudePanelAnim->setEndValue(target);
    m_attitudePanelAnim->start();
}

void MainWindow::onAttitudePanelAnimFinished() {
    syncAttitudeOverlayGeometry();
    if (m_viewer) m_viewer->update();
}

void MainWindow::toggleSensorChartPanel() {
    QPoint hiddenPos  = chartHiddenPos();
    QPoint visiblePos = chartVisiblePos();
    QPoint current;

    if (m_sensorChartPanelAnim->state() == QAbstractAnimation::Running) {
        m_sensorChartPanelAnim->stop();
        current = m_sensorChart->pos();
    } else if (m_sensorChart->isVisible()) {
        current = m_sensorChart->pos();
    } else {
        current = hiddenPos;
        m_sensorChart->move(current);
    }

    m_sensorChartPanelExpanded = !m_sensorChartPanelExpanded;
    if (m_sideBar->infoBtn()->isChecked() != m_sensorChartPanelExpanded) {
        m_sideBar->infoBtn()->setChecked(m_sensorChartPanelExpanded);
    }
    QPoint target = m_sensorChartPanelExpanded ? visiblePos : hiddenPos;
    m_sensorChartPanelAnim->setEasingCurve(m_sensorChartPanelExpanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);

    m_sensorChart->setVisible(true);
    m_sensorChart->raise();
    m_sensorChartPanelAnim->setStartValue(current);
    m_sensorChartPanelAnim->setEndValue(target);
    m_sensorChartPanelAnim->start();
}

void MainWindow::onSensorChartPanelAnimFinished() {
    syncSensorChartGeometry();
    if (m_viewer) m_viewer->update();
}

// ── 协议配置面板滑入/出 ─────────────────────────────────────

void MainWindow::toggleConfigPanel() {
    constexpr int panelW = ProtocolConfigPanel::PANEL_WIDTH;
    int viewH  = m_viewer->height();
    m_configPanel->setFixedHeight(viewH);
    if (m_configPanel->width() != panelW)
        m_configPanel->resize(panelW, viewH);

    const QPoint hiddenPos(-panelW, 0);
    const QPoint visiblePos(0, 0);
    QPoint current;

    // 若上一段动画还在运行，必须先 stop 并保留当前帧位置作为起点。
    // 注意：QPropertyAnimation::stop() 不会回退到 endValue，pos() 即真实位置。
    if (m_configPanelAnim->state() == QAbstractAnimation::Running) {
        m_configPanelAnim->stop();
        current = m_configPanel->pos();
    } else if (m_configPanel->isVisible()) {
        current = m_configPanel->pos();
    } else {
        current = hiddenPos;
        m_configPanel->move(current);
    }

    m_configPanelExpanded = !m_configPanelExpanded;
    // 同步 SideBar 按钮 checked 态。无论触发路径（X 关闭 vs 侧栏按钮）都
    // 强制对齐，防止状态漂移导致后续切换走错分支。
    if (m_sideBar->configBtn()->isChecked() != m_configPanelExpanded)
        m_sideBar->configBtn()->setChecked(m_configPanelExpanded);

    const QPoint target = m_configPanelExpanded ? visiblePos : hiddenPos;
    m_configPanelAnim->setEasingCurve(
        m_configPanelExpanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);

    if (m_configPanelExpanded)
        m_configPanel->loadFromConfig();

    m_configPanel->setVisible(true);
    m_configPanel->raise();
    m_configPanelAnim->setStartValue(current);
    m_configPanelAnim->setEndValue(target);
    m_configPanelAnim->start();
    // 起始一帧也驱动一次重绘，避免 QOpenGLWidget 父控件残留上一帧像素
    if (m_viewer) m_viewer->update();
}

// ── 数据处理 ─────────────────────────────────────────────────

// 接收解析完毕的传感器数据帧，分发给各子系统
void MainWindow::onDataReceived(const QString& source, const QString& prefix,
                                 const QList<double>& data)
{
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    m_poseProcessor->process(source, prefix, data);

    updateOverlays(source, prefix, data);
    updateSensorCharts(source, prefix, data);

    QString statusText = QString("接收中 (%1 个值)").arg(data.size());
    if (!prefix.isEmpty()) statusText += QString(" [%1]").arg(prefix);

    if (source == "udp") {
        m_lastUdpTime = nowMs;
        m_udpStatusLabel->setText("🟢 UDP: " + statusText);
        m_udpStatusLabel->setStyleSheet(Styles::STATUS_LABEL_ACTIVE_STYLE());
    } else if (source == "serial") {
        m_lastSerialTime = nowMs;
        m_serialStatusLabel->setText("🟢 Serial: " + statusText);
        m_serialStatusLabel->setStyleSheet(Styles::STATUS_LABEL_ACTIVE_STYLE());
    }

    // 更新 Viewer3D 中配置的可视化点（purpose 为空或 "position"）
    for (const auto& pc : ConfigLoader::instance().getPoints()) {
        if (!pc.purpose.isEmpty()) continue;
        if (!pc.matchesSource(source, prefix)) continue;
        if (data.size() < pc.requiredSizeXYZ()) continue;

        double x = data[pc.x.index] * pc.x.multiplier;
        double y = data[pc.y.index] * pc.y.multiplier;
        double z = data[pc.z.index] * pc.z.multiplier;
        m_viewer->updatePoint(pc.name, x, y, z, pc.color, pc.size);
    }

    m_viewer->update();
}

// 位置更新（来自 PoseProcessor）→ 在 Viewer3D 中绘制青色点
void MainWindow::onPoseUpdated(const QString& name, double x, double y, double z) {
    m_viewer->updatePoint(name, x, y, z, QColor(0, 255, 255, 255), 15);
    m_viewer->update();
}

// 速度更新 → SensorInfoOverlay
void MainWindow::onVelocityUpdated(double vx, double vy, double vz) {
    m_sensorOverlay->updateVelocity(vx, vy, vz);
}

void MainWindow::onParsedDataUpdated(const QString& /*source*/, const QString& /*prefix*/,
                                      const Vec3d& linearAcc,
                                      const Vec3d& /*gyr*/,
                                      const Vec3d& /*mag*/)
{
    m_sensorOverlay->updateAcceleration(linearAcc[0], linearAcc[1], linearAcc[2]);
}

void MainWindow::onFilterQuaternionsUpdated(const Quat4d& madgwickQ,
                                             const Quat4d& mahonyQ)
{
    m_attitudeWidget->updateMadgwickQuaternion(
        madgwickQ[0], madgwickQ[1], madgwickQ[2], madgwickQ[3]);
    m_attitudeWidget->updateMahonyQuaternion(
        mahonyQ[0], mahonyQ[1], mahonyQ[2], mahonyQ[3]);
}

void MainWindow::onPoseLog(const QString& msg)   { m_debugConsole->onPoseLog(msg); }
void MainWindow::onViewerLog(const QString& msg) { m_debugConsole->onPoseLog(msg); }

// 通过 points 配置提取姿态/海拔数据，更新 AttitudeWidget 和 SensorInfoOverlay
void MainWindow::updateOverlays(const QString& source, const QString& prefix,
                                 const QList<double>& data)
{
    auto& cfg = ConfigLoader::instance();

    // 姿态显示：优先使用四元数配置
    if (m_hasQuaternionSensor) {
        const auto* qp = cfg.findSensorPoint(PointPurpose::Quaternion, source, prefix);
        if (qp && data.size() >= qp->requiredSizeQuat()) {
            double w = data[qp->w.index] * qp->w.multiplier;
            double x = data[qp->x.index] * qp->x.multiplier;
            double y = data[qp->y.index] * qp->y.multiplier;
            double z = data[qp->z.index] * qp->z.multiplier;
            m_attitudeWidget->updateQuaternion(w, x, y, z);
        }
    }

    // 气压/海拔
    const auto* bp = cfg.findSensorPoint(PointPurpose::Barometer, source, prefix);
    if (bp && data.size() >= bp->requiredSizeBaro()) {
        double pres = data[bp->pressure.index] * bp->pressure.multiplier;
        double alt  = data[bp->altitude.index] * bp->altitude.multiplier;
        m_sensorOverlay->updateAltitude(pres, alt);
    }
}

// 通过 points 配置提取传感器数据，更新 SensorChartPanel
void MainWindow::updateSensorCharts(const QString& source, const QString& prefix,
                                     const QList<double>& data)
{
    auto& cfg = ConfigLoader::instance();
    std::optional<std::tuple<double,double,double>> acc, euler;
    std::optional<double> pressure, altitude;

    // 加速度
    const auto* ap = cfg.findSensorPoint(PointPurpose::Accelerometer, source, prefix);
    if (ap && data.size() >= ap->requiredSizeXYZ()) {
        acc = std::make_tuple(
            data[ap->x.index] * ap->x.multiplier,
            data[ap->y.index] * ap->y.multiplier,
            data[ap->z.index] * ap->z.multiplier
        );
    }

    // 姿态角：优先从四元数转换，否则无数据
    if (m_hasQuaternionSensor) {
        const auto* qp = cfg.findSensorPoint(PointPurpose::Quaternion, source, prefix);
        if (qp && data.size() >= qp->requiredSizeQuat()) {
            double w = data[qp->w.index] * qp->w.multiplier;
            double x = data[qp->x.index] * qp->x.multiplier;
            double y = data[qp->y.index] * qp->y.multiplier;
            double z = data[qp->z.index] * qp->z.multiplier;
            auto [r, p, yaw] = AttitudeWidget::quaternionToEuler(w, x, y, z);
            euler = std::make_tuple(r, p, yaw);
        }
    }

    // 气压/海拔
    const auto* bp = cfg.findSensorPoint(PointPurpose::Barometer, source, prefix);
    if (bp && data.size() >= bp->requiredSizeBaro()) {
        pressure = data[bp->pressure.index] * bp->pressure.multiplier;
        altitude = data[bp->altitude.index] * bp->altitude.multiplier;
    }

    m_sensorChart->pushSnapshot(acc, euler, pressure, altitude);
}

// 清场：停止后重置所有可视状态
void MainWindow::clearScene() {
    m_viewer->clearAll();
    m_attitudeWidget->reset();
    m_sensorChart->reset();
    m_sensorOverlay->reset();
    m_poseProcessor->reset();
    syncAttitudeOverlayGeometry();
    syncSensorChartGeometry();
}

// ── 复选框处理 ────────────────────────────────────────────────

void MainWindow::toggleFullPathMode(bool checked) {
    m_viewer->setFullPathMode(checked);
    m_trailCheckbox->setEnabled(checked);
}

void MainWindow::toggleTrailMode(bool checked) {
    m_viewer->setPathColorMode(checked);
}

// ── 状态超时检测 ──────────────────────────────────────────────

void MainWindow::checkStatusTimeout() {
    qint64 nowMs  = QDateTime::currentMSecsSinceEpoch();
    qint64 timeoutMs = 2000;

    if (nowMs - m_lastUdpTime > timeoutMs) {
        m_udpStatusLabel->setText("⚪ UDP: Idle");
        m_udpStatusLabel->setStyleSheet(Styles::STATUS_LABEL_STYLE());
    }
    if (nowMs - m_lastSerialTime > timeoutMs) {
        m_serialStatusLabel->setText("⚪ Serial: Idle");
        m_serialStatusLabel->setStyleSheet(Styles::STATUS_LABEL_STYLE());
    }
}

// ── 事件 ─────────────────────────────────────────────────────

void MainWindow::resizeEvent(QResizeEvent* ev) {
    FramelessWindow::resizeEvent(ev);
    repositionOverlays();
}

// Viewer3D 尺寸变化时重新定位叠加层
bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_viewer && ev->type() == QEvent::Resize)
        repositionOverlays();
    return FramelessWindow::eventFilter(obj, ev);
}

void MainWindow::closeEvent(QCloseEvent* ev) {
    m_dataReceiver->stopAll();
    FramelessWindow::closeEvent(ev);
}

