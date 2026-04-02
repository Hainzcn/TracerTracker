#include "MainWindow.h"
#include "Styles.h"
#include "ToolBar.h"
#include "DebugConsole.h"
#include "AttitudeWidget.h"
#include "SensorChartPanel.h"
#include "SensorInfoOverlay.h"
#include "ViewOrientationGizmo.h"
#include "opengl/Viewer3D.h"
#include "../io/DataReceiver.h"
#include "../ins/PoseProcessor.h"
#include "../config/ConfigLoader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
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

// ── AttitudePanelHotZone ──────────────────────────────────────

AttitudePanelHotZone::AttitudePanelHotZone(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    m_bgAnim = new QVariantAnimation(this);
    m_bgAnim->setDuration(140);
    m_bgAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_bgAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){
        m_bgAlpha = v.toDouble(); update();
    });

    m_stripAnim = new QVariantAnimation(this);
    m_stripAnim->setDuration(160);
    m_stripAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_stripAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){
        m_stripProgress = v.toDouble(); update();
    });
}

void AttitudePanelHotZone::animateBg(double target) {
    m_bgAnim->stop();
    m_bgAnim->setStartValue(m_bgAlpha);
    m_bgAnim->setEndValue(target);
    m_bgAnim->start();
}

void AttitudePanelHotZone::animateStrip(double target) {
    m_stripAnim->stop();
    m_stripAnim->setStartValue(m_stripProgress);
    m_stripAnim->setEndValue(target);
    m_stripAnim->start();
}

void AttitudePanelHotZone::enterEvent(QEnterEvent* ev) {
    animateBg(128.0);
    animateStrip(1.0);
    QWidget::enterEvent(ev);
}

void AttitudePanelHotZone::leaveEvent(QEvent* ev) {
    m_pressed = false;
    animateBg(0.0);
    animateStrip(0.0);
    QWidget::leaveEvent(ev);
}

void AttitudePanelHotZone::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) {
        m_pressed = true;
        animateBg(156.0);
        ev->accept();
    } else {
        ev->accept();
    }
}

void AttitudePanelHotZone::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) {
        bool inside = rect().contains(ev->position().toPoint());
        m_pressed = false;
        animateBg(inside ? 128.0 : 0.0);
        if (inside) { emit clicked(); ev->accept(); return; }
    }
    ev->accept();
}

void AttitudePanelHotZone::mouseMoveEvent(QMouseEvent* ev)    { ev->accept(); }
void AttitudePanelHotZone::wheelEvent(QWheelEvent* ev)        { ev->accept(); }
void AttitudePanelHotZone::mouseDoubleClickEvent(QMouseEvent* ev){ ev->accept(); }

// 绘制半透明拉条
void AttitudePanelHotZone::paintEvent(QPaintEvent* ev) {
    if (m_bgAlpha <= 0.0 || m_stripProgress <= 0.0) { QWidget::paintEvent(ev); return; }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill(220, 220, 220, int(m_bgAlpha));
    int visW = std::max(1, std::min(VISIBLE_WIDTH, int(std::round(VISIBLE_WIDTH * m_stripProgress))));
    p.fillRect(0, 0, visW, height(), fill);
    QWidget::paintEvent(ev);
}

// ── MainWindow ────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("TracerTracker");
    resize(1280, 720);

    // ── 核心数据对象 ──
    m_dataReceiver  = new DataReceiver(this);
    m_poseProcessor = new PoseProcessor(this);

    // ── 中心部件与布局 ──
    auto* central = new QWidget(this);
    central->setObjectName("centralWidget");
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── 工具栏 ──
    m_toolbar = new ToolBar(this);
    m_toolbar->bindDataReceiver(m_dataReceiver);
    connect(m_toolbar, &ToolBar::serialStopRequested, this, &MainWindow::clearScene);
    layout->addWidget(m_toolbar);

    // ── 3D 视口 ──
    m_viewer = new Viewer3D(this);
    m_viewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_viewer, &Viewer3D::logMessage, this, &MainWindow::onViewerLog);
    layout->addWidget(m_viewer, 1);

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

    // ── 热区 ──
    m_attitudeHotzone = new AttitudePanelHotZone(m_viewer);
    m_attitudeHotzone->setFixedWidth(ATTITUDE_HOTZONE_WIDTH + ATTITUDE_HOTZONE_PADDING);
    connect(m_attitudeHotzone, &AttitudePanelHotZone::clicked,
            this, &MainWindow::toggleAttitudePanel);

    m_chartHotzone = new AttitudePanelHotZone(m_viewer);
    m_chartHotzone->setFixedWidth(ATTITUDE_HOTZONE_WIDTH + ATTITUDE_HOTZONE_PADDING);
    connect(m_chartHotzone, &AttitudePanelHotZone::clicked,
            this, &MainWindow::toggleSensorChartPanel);

    // ── 面板动画 ──
    m_attitudePanelAnim = new QPropertyAnimation(m_attitudeWidget, "pos", this);
    m_attitudePanelAnim->setDuration(ATTITUDE_PANEL_ANIM_MS);
    m_attitudePanelAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_attitudePanelAnim, &QPropertyAnimation::finished,
            this, &MainWindow::onAttitudePanelAnimFinished);

    m_sensorChartPanelAnim = new QPropertyAnimation(m_sensorChart, "pos", this);
    m_sensorChartPanelAnim->setDuration(ATTITUDE_PANEL_ANIM_MS);
    m_sensorChartPanelAnim->setEasingCurve(QEasingCurve::OutCubic);
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
    layout->addWidget(m_debugConsole);

    // ── 状态栏 ──
    buildStatusBar();
    layout->addWidget(m_statusBarWidget);

    // debugConsole 全折叠 → 取消勾选复选框
    connect(m_debugConsole, &DebugConsole::allCollapsed, this, [this](){
        m_debugCheckbox->setChecked(false);
    });

    // ── 全局样式 ──
    setStyleSheet(Styles::MAIN_WINDOW_STYLE());

    // ── 是否有四元数类型的点配置 ──
    auto& cfg = ConfigLoader::instance();
    auto points = cfg.getPoints();
    m_hasQuaternionPoint = std::any_of(points.begin(), points.end(),
        [](const PointConfig& p){ return p.purpose == "quaternion"; });

    // ── 状态超时定时器（1s 周期）──
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::checkStatusTimeout);
    m_statusTimer->start(1000);

    // ── 初始化尾迹长度 ──
    m_viewer->setTrailLength(m_trailLengthSpin->value());

    // 延迟定位叠加层（等待布局完成）
    m_viewer->installEventFilter(this);
    QTimer::singleShot(0, this, &MainWindow::repositionOverlays);
}

MainWindow::~MainWindow() {}

// ── 状态栏 ────────────────────────────────────────────────────

void MainWindow::buildStatusBar() {
    m_statusBarWidget = new QWidget(this);
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
    m_fullPathCheckbox = new QCheckBox("全路径");
    m_fullPathCheckbox->setStyleSheet(Styles::STYLE_CHECKBOX());
    connect(m_fullPathCheckbox, &QCheckBox::toggled,
            this, &MainWindow::toggleFullPathMode);
    sbl->addWidget(m_fullPathCheckbox);

    sbl->addSpacing(16);
    m_trailCheckbox = new QCheckBox("速度尾迹");
    m_trailCheckbox->setStyleSheet(Styles::STYLE_CHECKBOX());
    connect(m_trailCheckbox, &QCheckBox::toggled,
            this, &MainWindow::toggleTrailMode);
    sbl->addWidget(m_trailCheckbox);

    sbl->addSpacing(8);
    m_trailLengthLabel = new QLabel("长度");
    m_trailLengthLabel->setStyleSheet("color: #666666; font-size: 12px;"
        " font-family: 'Microsoft YaHei', sans-serif; border: none;");
    sbl->addWidget(m_trailLengthLabel);

    m_trailLengthSpin = new QSpinBox(this);
    m_trailLengthSpin->setRange(10, 5000);
    m_trailLengthSpin->setValue(120);
    m_trailLengthSpin->setFixedWidth(72);
    m_trailLengthSpin->setFixedHeight(22);
    m_trailLengthSpin->setStyleSheet(Styles::STYLE_SPINBOX());
    connect(m_trailLengthSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::onTrailLengthChanged);
    sbl->addWidget(m_trailLengthSpin);

    m_trailLengthSpin->setEnabled(false);
    m_trailLengthLabel->setEnabled(false);
}

// ── 叠加层定位 ────────────────────────────────────────────────

QPoint MainWindow::attitudeVisiblePos() const {
    return QPoint(ATTITUDE_PANEL_MARGIN, ATTITUDE_PANEL_MARGIN);
}

QPoint MainWindow::attitudeHiddenPos() const {
    return QPoint(-m_attitudeWidget->width(), ATTITUDE_PANEL_MARGIN);
}

QPoint MainWindow::chartVisiblePos() const {
    // AttitudeWidget 下方 CHART_PANEL_SPACING 处
    return QPoint(
        ATTITUDE_PANEL_MARGIN,
        ATTITUDE_PANEL_MARGIN + m_attitudeWidget->height() + CHART_PANEL_SPACING
    );
}

QPoint MainWindow::chartHiddenPos() const {
    return QPoint(-m_sensorChart->width(), chartVisiblePos().y());
}

void MainWindow::syncAttitudeOverlayGeometry() {
    int top = ATTITUDE_PANEL_MARGIN;
    m_attitudeHotzone->setGeometry(
        0, top,
        ATTITUDE_HOTZONE_WIDTH + ATTITUDE_HOTZONE_PADDING,
        m_attitudeWidget->height()
    );

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
    m_attitudeHotzone->raise();
}

void MainWindow::syncSensorChartGeometry() {
    int top = chartVisiblePos().y();
    m_chartHotzone->setGeometry(
        0, top,
        ATTITUDE_HOTZONE_WIDTH + ATTITUDE_HOTZONE_PADDING,
        m_sensorChart->height()
    );

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
    m_chartHotzone->raise();
}

void MainWindow::repositionOverlays() {
    int vw = m_viewer->width(), vh = m_viewer->height();
    int mg = ATTITUDE_PANEL_MARGIN;

    syncAttitudeOverlayGeometry();
    syncSensorChartGeometry();

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
    QPoint target = m_attitudePanelExpanded ? visiblePos : hiddenPos;

    m_attitudeWidget->setVisible(true);
    m_attitudeWidget->raise();
    m_attitudeHotzone->raise();
    m_attitudePanelAnim->setStartValue(current);
    m_attitudePanelAnim->setEndValue(target);
    m_attitudePanelAnim->start();
}

void MainWindow::onAttitudePanelAnimFinished() {
    syncAttitudeOverlayGeometry();
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
    QPoint target = m_sensorChartPanelExpanded ? visiblePos : hiddenPos;

    m_sensorChart->setVisible(true);
    m_sensorChart->raise();
    m_chartHotzone->raise();
    m_sensorChartPanelAnim->setStartValue(current);
    m_sensorChartPanelAnim->setEndValue(target);
    m_sensorChartPanelAnim->start();
}

void MainWindow::onSensorChartPanelAnimFinished() {
    syncSensorChartGeometry();
}

// ── 数据处理 ─────────────────────────────────────────────────

// 接收解析完毕的传感器数据帧，分发给各子系统
void MainWindow::onDataReceived(const QString& source, const QString& prefix,
                                 const QList<double>& data)
{
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // 转发给 INS 管线处理
    m_poseProcessor->process(source, prefix, data);

    updateOverlays(data);
    updateSensorCharts(data);

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

    // 直接更新 Viewer3D 中配置的点
    {
    auto& cfg2 = ConfigLoader::instance();
    auto pointCfgs = cfg2.getPoints();
    for (const auto& pc : pointCfgs) {
        if (!pc.purpose.isEmpty() && pc.purpose != "position") continue;

        if (pc.source != "any" && pc.source != source) continue;

        QString cfgPrefix = pc.prefix.value_or(QString());
        if (cfgPrefix != prefix) continue;

        int xi = pc.x.index;
        int yi = pc.y.index;
        int zi = pc.z.index;
        int need = std::max(std::max(xi, yi), zi) + 1;
        if (data.size() < need) continue;

        double x = data[xi] * pc.x.multiplier;
        double y = data[yi] * pc.y.multiplier;
        double z = data[zi] * pc.z.multiplier;
        m_viewer->updatePoint(pc.name, x, y, z, pc.color, pc.size);
    }
    } // end cfg2 scope
}

// 位置更新（来自 PoseProcessor）→ 在 Viewer3D 中绘制青色点
void MainWindow::onPoseUpdated(const QString& name, double x, double y, double z) {
    m_viewer->updatePoint(name, x, y, z, QColor(0, 255, 255, 255), 15);
}

// 速度更新 → SensorInfoOverlay
void MainWindow::onVelocityUpdated(double vx, double vy, double vz) {
    m_sensorOverlay->updateVelocity(vx, vy, vz);
}

// 线加速度更新 → SensorInfoOverlay
void MainWindow::onParsedDataUpdated(const QString& /*source*/, const QString& /*prefix*/,
                                      const QList<double>& linearAcc,
                                      const QList<double>& /*gyr*/,
                                      const QList<double>& /*mag*/)
{
    if (linearAcc.size() >= 3)
        m_sensorOverlay->updateAcceleration(linearAcc[0], linearAcc[1], linearAcc[2]);
}

// 滤波四元数 → AttitudeWidget
void MainWindow::onFilterQuaternionsUpdated(const QList<double>& madgwickQ,
                                             const QList<double>& mahonyQ)
{
    if (madgwickQ.size() >= 4)
        m_attitudeWidget->updateMadgwickQuaternion(
            madgwickQ[0], madgwickQ[1], madgwickQ[2], madgwickQ[3]);
    if (mahonyQ.size() >= 4)
        m_attitudeWidget->updateMahonyQuaternion(
            mahonyQ[0], mahonyQ[1], mahonyQ[2], mahonyQ[3]);
}

void MainWindow::onPoseLog(const QString& msg)   { m_debugConsole->onPoseLog(msg); }
void MainWindow::onViewerLog(const QString& msg) { m_debugConsole->onPoseLog(msg); }

// 更新 AttitudeWidget 和 SensorInfoOverlay 的姿态/海拔数据
void MainWindow::updateOverlays(const QList<double>& data) {
    if (data.size() < 19) return;
    if (m_hasQuaternionPoint)
        m_attitudeWidget->updateQuaternion(data[6], data[7], data[8], data[9]);
    else
        m_attitudeWidget->updateEuler(data[14], data[15], data[16]);
    m_sensorOverlay->updateAltitude(data[17], data[18]);
}

// 更新 SensorChartPanel 的各轴历史数据
void MainWindow::updateSensorCharts(const QList<double>& data) {
    std::optional<std::tuple<double,double,double>> acc, euler;
    std::optional<double> pressure, altitude;

    if (data.size() >= 3)
        acc = {data[0], data[1], data[2]};

    if (m_hasQuaternionPoint && data.size() >= 10) {
        auto [r,p,y] = AttitudeWidget::quaternionToEuler(data[6], data[7], data[8], data[9]);
        euler = {r, p, y};
    } else if (data.size() >= 17) {
        euler = {data[14], data[15], data[16]};
    }

    if (data.size() >= 19) {
        pressure = data[17];
        altitude = data[18];
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
}

void MainWindow::toggleTrailMode(bool checked) {
    m_viewer->setTrailMode(checked);
    m_trailLengthSpin->setEnabled(checked);
    m_trailLengthLabel->setEnabled(checked);
    m_trailLengthLabel->setStyleSheet(
        checked ? Styles::STATUS_LABEL_STYLE()
                : "color: #555; font-size: 12px; border: none;"
    );
}

void MainWindow::onTrailLengthChanged(int value) {
    m_viewer->setTrailLength(value);
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
    QMainWindow::resizeEvent(ev);
    repositionOverlays();
}

// Viewer3D 尺寸变化时重新定位叠加层
bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_viewer && ev->type() == QEvent::Resize)
        repositionOverlays();
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::closeEvent(QCloseEvent* ev) {
    m_dataReceiver->stopAll();
    QMainWindow::closeEvent(ev);
}
