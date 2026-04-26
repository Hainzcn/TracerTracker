#include "ui/window/MainWindow.h"
#include "ui/common/Styles.h"
#include "ui/window/ToolBar.h"
#include "ui/window/SideBar.h"
#include "ui/panels/DebugConsole.h"
#include "ui/overlays/AttitudeWidget.h"
#include "ui/overlays/SensorChartPanel.h"
#include "ui/overlays/SensorInfoOverlay.h"
#include "ui/overlays/ViewOrientationGizmo.h"
#include "ui/panels/ProtocolConfigPanel.h"
#include "ui/panels/SettingsPanel.h"
#include "ui/opengl/Viewer3D.h"
#include "io/DataReceiver.h"
#include "ins/PoseProcessor.h"
#include "config/ConfigLoader.h"

#include <QSizePolicy>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QEasingCurve>
#include <QDateTime>
#include <QAbstractAnimation>
#include <QGraphicsOpacityEffect>
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
        toggleSettingsPanel();
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

    // 同位淡入淡出：当用户在「协议配置」与「设置」面板之间切换时，不再
    // 滑出再滑入，而是两个面板都停留在 (0,0) 上，通过 QGraphicsOpacityEffect
    // 同步反向动画做内容替换。默认 opacity=1，滑入/出路径不会受影响。
    //
    // 关键：effect 挂在 SidePanel::contentHost() 而非 SidePanel 自身——这样
    // 「面板背景」（#sidePanel 的深色底）始终保持不透明，只有内容（标题栏 +
    // 滚动区 + 动作栏）真正参与淡入淡出。两个面板的 chrome 视觉一致，叠在
    // (0,0) 时用户感知到的就是「面板纹丝不动，里面的内容做了一次轻微淡入」。
    m_configFadeFx = new QGraphicsOpacityEffect(m_configPanel->contentHost());
    m_configFadeFx->setOpacity(1.0);
    m_configPanel->contentHost()->setGraphicsEffect(m_configFadeFx);
    m_configFadeAnim = new QPropertyAnimation(m_configFadeFx, "opacity", this);
    m_configFadeAnim->setDuration(160);
    m_configFadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_configFadeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant&) {
        if (m_viewer) m_viewer->update();
    });
    connect(m_configFadeAnim, &QPropertyAnimation::finished, this, [this]() {
        // 淡到 0 的一方就是「被替换掉」的面板，需收回视觉层 + 同步 OpenGL 合成
        if (m_configFadeFx && m_configFadeFx->opacity() < 0.01) {
            m_configPanel->setVisible(false);
            // 把不可见的面板挪回 hidden 槽位，避免下次再被 raise 时占据 (0,0)
            m_configPanel->move(-ProtocolConfigPanel::PANEL_WIDTH, 0);
            m_configFadeFx->setOpacity(1.0);  // 复位，下次滑入/淡入直接可用
            if (m_viewer) {
                m_viewer->repaint();
                if (auto* top = m_viewer->window()) top->update();
            }
        }
    });

    // ── 通用设置面板（与协议面板共用左侧 (0,0) 滑入区，运行时互斥）──
    m_settingsPanel = new SettingsPanel(m_viewer);
    m_settingsPanel->setVisible(false);
    connect(m_settingsPanel, &SettingsPanel::closeRequested, this, &MainWindow::toggleSettingsPanel);
    connect(m_settingsPanel, &SettingsPanel::applied,        this, &MainWindow::onSettingsApplied);
    connect(m_settingsPanel, &SettingsPanel::confirmed,      this, &MainWindow::onSettingsConfirmed);

    // 动画配置完整照搬 m_configPanelAnim：finished 中 setVisible→move→
    // viewer->repaint→top->update 的顺序是 Qt6 OpenGL 合成路径的已知必要
    // 收尾，否则关闭瞬间会留下上一帧子控件像素。
    m_settingsPanelAnim = new QPropertyAnimation(m_settingsPanel, "pos", this);
    m_settingsPanelAnim->setDuration(200);
    m_settingsPanelAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_settingsPanelAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant&) {
        if (m_viewer) m_viewer->update();
    });
    connect(m_settingsPanelAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_settingsPanelExpanded) {
            m_settingsPanel->move(0, 0);
            if (m_viewer) m_viewer->update();
        } else {
            m_settingsPanel->setVisible(false);
            m_settingsPanel->move(-SettingsPanel::PANEL_WIDTH, 0);
            if (m_viewer) {
                m_viewer->repaint();
                if (auto* top = m_viewer->window()) top->update();
            }
        }
    });

    // 与 m_configFadeAnim 同结构，承担「设置面板」一侧的同位淡入淡出。
    // 同样挂在 contentHost() 上，使面板深色背景在切换全程保持不透明。
    m_settingsFadeFx = new QGraphicsOpacityEffect(m_settingsPanel->contentHost());
    m_settingsFadeFx->setOpacity(1.0);
    m_settingsPanel->contentHost()->setGraphicsEffect(m_settingsFadeFx);
    m_settingsFadeAnim = new QPropertyAnimation(m_settingsFadeFx, "opacity", this);
    m_settingsFadeAnim->setDuration(160);
    m_settingsFadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_settingsFadeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant&) {
        if (m_viewer) m_viewer->update();
    });
    connect(m_settingsFadeAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_settingsFadeFx && m_settingsFadeFx->opacity() < 0.01) {
            m_settingsPanel->setVisible(false);
            m_settingsPanel->move(-SettingsPanel::PANEL_WIDTH, 0);
            m_settingsFadeFx->setOpacity(1.0);
            if (m_viewer) {
                m_viewer->repaint();
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

    // 设置面板高度同步（与协议面板镜像）
    if (m_settingsPanel->isVisible()) {
        m_settingsPanel->setFixedHeight(vh);
        if (m_settingsPanelAnim->state() != QAbstractAnimation::Running) {
            m_settingsPanel->move(m_settingsPanelExpanded ? 0 : -SettingsPanel::PANEL_WIDTH, 0);
        }
        m_settingsPanel->raise();
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
    // 互斥升级：协议面板与设置面板共用左侧 (0,0)。
    //   · 若对方面板已展开 → 同位淡入淡出替换内容（不做滑出再滑入）。
    //   · 否则按原本的滑入/滑出动画执行。
    if (!m_configPanelExpanded && m_settingsPanelExpanded) {
        crossFadeToPanel(/*toConfig=*/true);
        return;
    }

    // 进入滑动路径前，先清理任何可能残留的淡入淡出状态：
    //   · 若上一次刚完成 Settings→Config 的淡入，本面板 fx.opacity 已经是 1，
    //     无影响；
    //   · 若用户在淡入淡出过程中再点协议按钮，需要立刻收尾——把动画停掉，
    //     opacity 复位为 1（确保滑出时面板可见），并强制隐藏对方面板（它在
    //     淡到 0 之前其实仍然 visible）。
    if (m_configFadeAnim   && m_configFadeAnim  ->state() == QAbstractAnimation::Running) m_configFadeAnim  ->stop();
    if (m_settingsFadeAnim && m_settingsFadeAnim->state() == QAbstractAnimation::Running) m_settingsFadeAnim->stop();
    if (m_configFadeFx)   m_configFadeFx  ->setOpacity(1.0);
    if (m_settingsFadeFx) m_settingsFadeFx->setOpacity(1.0);
    if (!m_settingsPanelExpanded && m_settingsPanel->isVisible()) {
        m_settingsPanel->setVisible(false);
        m_settingsPanel->move(-SettingsPanel::PANEL_WIDTH, 0);
    }

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

// ── 通用设置面板滑入/出 ─────────────────────────────────────

void MainWindow::toggleSettingsPanel() {
    // 互斥升级（与 toggleConfigPanel 反向对称）：若对方面板已展开 → 同位
    // 淡入淡出替换内容；否则按原本的滑入/滑出动画执行。
    if (!m_settingsPanelExpanded && m_configPanelExpanded) {
        crossFadeToPanel(/*toConfig=*/false);
        return;
    }

    // 滑动路径前清理任何残留的淡入淡出状态（详见 toggleConfigPanel 注释）。
    if (m_configFadeAnim   && m_configFadeAnim  ->state() == QAbstractAnimation::Running) m_configFadeAnim  ->stop();
    if (m_settingsFadeAnim && m_settingsFadeAnim->state() == QAbstractAnimation::Running) m_settingsFadeAnim->stop();
    if (m_configFadeFx)   m_configFadeFx  ->setOpacity(1.0);
    if (m_settingsFadeFx) m_settingsFadeFx->setOpacity(1.0);
    if (!m_configPanelExpanded && m_configPanel->isVisible()) {
        m_configPanel->setVisible(false);
        m_configPanel->move(-ProtocolConfigPanel::PANEL_WIDTH, 0);
    }

    constexpr int panelW = SettingsPanel::PANEL_WIDTH;
    int viewH = m_viewer->height();
    m_settingsPanel->setFixedHeight(viewH);
    if (m_settingsPanel->width() != panelW)
        m_settingsPanel->resize(panelW, viewH);

    const QPoint hiddenPos(-panelW, 0);
    const QPoint visiblePos(0, 0);
    QPoint current;

    if (m_settingsPanelAnim->state() == QAbstractAnimation::Running) {
        m_settingsPanelAnim->stop();
        current = m_settingsPanel->pos();
    } else if (m_settingsPanel->isVisible()) {
        current = m_settingsPanel->pos();
    } else {
        current = hiddenPos;
        m_settingsPanel->move(current);
    }

    m_settingsPanelExpanded = !m_settingsPanelExpanded;
    if (m_sideBar->settingsBtn()->isChecked() != m_settingsPanelExpanded)
        m_sideBar->settingsBtn()->setChecked(m_settingsPanelExpanded);

    const QPoint target = m_settingsPanelExpanded ? visiblePos : hiddenPos;
    m_settingsPanelAnim->setEasingCurve(
        m_settingsPanelExpanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);

    if (m_settingsPanelExpanded)
        m_settingsPanel->loadFromConfig();

    m_settingsPanel->setVisible(true);
    m_settingsPanel->raise();
    m_settingsPanelAnim->setStartValue(current);
    m_settingsPanelAnim->setEndValue(target);
    m_settingsPanelAnim->start();
    if (m_viewer) m_viewer->update();
}

// ── 协议/设置面板同位淡入淡出 ─────────────────────────────────
//
// 使用前提：调用方已确认对方面板正处于展开态（位于 (0,0) 上）。本函数把
// 目标面板也吸附到 (0,0)、置于 raise() 后的栈顶，然后驱动两条 opacity
// 动画做镜像变化，实现「内容替换」式的过渡，而非滑出再滑入。
//
// 收尾由各自 fade animation 的 finished 槽完成（opacity≈0 一侧 setVisible(false)
// + 复位 opacity，避免下次复用时初始就透明）。
void MainWindow::crossFadeToPanel(bool toConfig) {
    if (!m_viewer) return;

    auto* inFx    = toConfig ? m_configFadeFx     : m_settingsFadeFx;
    auto* outFx   = toConfig ? m_settingsFadeFx   : m_configFadeFx;
    auto* inAnim  = toConfig ? m_configFadeAnim   : m_settingsFadeAnim;
    auto* outAnim = toConfig ? m_settingsFadeAnim : m_configFadeAnim;

    // 在 stop() 之前先采样：用于区分「全新淡入淡出」与「中途反向被打断」。
    // —— 全新：incoming 默认 opacity 仍为 1（构造期初始值或上一轮收尾复位），
    //          需要主动把它压回 0 才能看到由暗到明的过渡。
    // —— 反向打断：incoming 此时已是上一轮的 outgoing，opacity 卡在 0~1 之
    //          间的某个值；保留当前值作为起点能避免视觉跳变。
    const bool incomingMidFade = (inAnim  && inAnim ->state() == QAbstractAnimation::Running);
    const bool outgoingMidFade = (outAnim && outAnim->state() == QAbstractAnimation::Running);

    // 立刻终止任何正在跑的滑动/淡入淡出动画——两面板都将停留在 (0,0)
    if (m_configPanelAnim   && m_configPanelAnim  ->state() == QAbstractAnimation::Running) m_configPanelAnim  ->stop();
    if (m_settingsPanelAnim && m_settingsPanelAnim->state() == QAbstractAnimation::Running) m_settingsPanelAnim->stop();
    if (incomingMidFade) inAnim ->stop();
    if (outgoingMidFade) outAnim->stop();

    // 同步两面板的几何到 (0,0) 全高度
    constexpr int cW    = ProtocolConfigPanel::PANEL_WIDTH;
    constexpr int sW    = SettingsPanel::PANEL_WIDTH;
    const int     viewH = m_viewer->height();
    m_configPanel  ->setFixedHeight(viewH);
    m_settingsPanel->setFixedHeight(viewH);
    if (m_configPanel  ->width() != cW) m_configPanel  ->resize(cW, viewH);
    if (m_settingsPanel->width() != sW) m_settingsPanel->resize(sW, viewH);
    m_configPanel  ->move(0, 0);
    m_settingsPanel->move(0, 0);
    m_configPanel  ->setVisible(true);
    m_settingsPanel->setVisible(true);

    // 进入面板时先用 ConfigLoader 最新值刷新一次（与滑入路径保持一致）
    if (toConfig) m_configPanel->loadFromConfig();
    else          m_settingsPanel->loadFromConfig();

    // 目标面板压在栈顶，避免视觉上被对方遮挡
    if (toConfig) m_configPanel->raise();
    else          m_settingsPanel->raise();

    // 全新淡入淡出：把 incoming 强制压回 0，outgoing 拉满到 1，确保两侧
    // 都有可见的过渡曲线（而非仅 outgoing 一方淡出、incoming 直接 pop 上）。
    if (!incomingMidFade) inFx ->setOpacity(0.0);
    if (!outgoingMidFade) outFx->setOpacity(1.0);

    // 镜像动画：incoming opacity ↑ 1，outgoing opacity ↓ 0
    inAnim ->setStartValue(inFx ->opacity());
    inAnim ->setEndValue(1.0);
    outAnim->setStartValue(outFx->opacity());
    outAnim->setEndValue(0.0);
    inAnim ->setEasingCurve(QEasingCurve::OutCubic);
    outAnim->setEasingCurve(QEasingCurve::OutCubic);
    inAnim ->start();
    outAnim->start();

    // 状态翻转 + SideBar 按钮 checked 同步（防止漂移）
    m_configPanelExpanded   = toConfig;
    m_settingsPanelExpanded = !toConfig;
    if (m_sideBar->configBtn()  ->isChecked() != m_configPanelExpanded)
        m_sideBar->configBtn()  ->setChecked(m_configPanelExpanded);
    if (m_sideBar->settingsBtn()->isChecked() != m_settingsPanelExpanded)
        m_sideBar->settingsBtn()->setChecked(m_settingsPanelExpanded);

    m_viewer->update();
}

void MainWindow::onSettingsApplied() {
    // 已由 SettingsPanel 内部完成 ConfigLoader::updateGeneralSettings + save。
    // 这里只需重启正在运行的接收线程，让新的串口口型 / UDP 端口立刻生效。
    auto& cfg = ConfigLoader::instance();
    SerialConfig sCfg = cfg.getSerialConfig();
    UdpConfig    uCfg = cfg.getUdpConfig();

    if (m_dataReceiver->isSerialRunning()) {
        m_dataReceiver->stopSerial();
        m_dataReceiver->startSerial(sCfg.port, sCfg.baudrate, sCfg.protocol);
    }
    if (m_dataReceiver->isUdpRunning()) {
        m_dataReceiver->stopUdp();
        m_dataReceiver->startUdp(uCfg.ip, uCfg.port);
    }

    m_hasQuaternionSensor = cfg.hasSensorPurpose(PointPurpose::Quaternion);
    onViewerLog("\u8bbe\u7f6e\u5df2\u5e94\u7528\uff0c\u5df2\u91cd\u542f\u63a5\u6536\u7ebf\u7a0b");  // "设置已应用，已重启接收线程"
}

void MainWindow::onSettingsConfirmed() {
    // SettingsPanel 已写盘；这里仅关闭面板并提示下次启动生效
    onViewerLog("\u8bbe\u7f6e\u5df2\u4fdd\u5b58\uff0c\u4e0b\u6b21\u542f\u52a8\u751f\u6548");  // "设置已保存，下次启动生效"
    if (m_settingsPanelExpanded)
        toggleSettingsPanel();
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

