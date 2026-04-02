#include "ToolBar.h"
#include "Styles.h"
#include "../io/DataReceiver.h"
#include "../config/ConfigLoader.h"

#include <QLabel>
#include <QStyledItemDelegate>
#include <QAbstractItemView>
#include <QListView>
#ifdef HAVE_QT_SERIAL_PORT
#  include <QSerialPortInfo>
#endif
#include <QHBoxLayout>

// ============================================================
// ToolBar.cpp — 顶部工具栏实现
// ============================================================

// ── SeamlessComboBox ─────────────────────────────────────────

SeamlessComboBox::SeamlessComboBox(QWidget* parent) : QComboBox(parent) {}

// 弹出时向上移动 RADIUS 像素，使下拉框紧贴按钮底边圆角
void SeamlessComboBox::showPopup() {
    QComboBox::showPopup();
    QWidget* popup = view()->window();
    QRect geo = popup->geometry();
    geo.setTop(geo.top() - RADIUS);
    popup->setGeometry(geo);
}

// ── CompactItemDelegate ───────────────────────────────────────

class CompactItemDelegate : public QStyledItemDelegate {
public:
    explicit CompactItemDelegate(int height, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_height(height) {}

    // 强制每行高度为固定值
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(m_height);
        return s;
    }
private:
    int m_height;
};

// ── ToolBar ───────────────────────────────────────────────────

ToolBar::ToolBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(32);
    setStyleSheet(Styles::TOP_BAR_STYLE());

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(8);

    const int ctrlH = 24;

    // ── 串口区段 ──
    auto* serialLabel = new QLabel("串口 ");
    serialLabel->setStyleSheet(Styles::STYLE_LABEL());
    layout->addWidget(serialLabel);

    m_serialCombo = new SeamlessComboBox(this);
    m_serialCombo->setFixedHeight(ctrlH);
    m_serialCombo->setStyleSheet(Styles::STYLE_COMBO());
    m_serialCombo->setItemDelegate(new CompactItemDelegate(20, m_serialCombo));
    m_serialCombo->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_serialCombo->view()->setContentsMargins(0, 0, 0, 0);
    // setSpacing 是 QListView 的成员，需要向下转型
    if (auto* lv = qobject_cast<QListView*>(m_serialCombo->view()))
        lv->setSpacing(0);
    layout->addWidget(m_serialCombo);

    m_refreshBtn = new QPushButton("↻", this);
    m_refreshBtn->setFixedSize(ctrlH, ctrlH);
    m_refreshBtn->setToolTip("刷新串口列表");
    m_refreshBtn->setStyleSheet(
        Styles::STYLE_BTN_IDLE() +
        "QPushButton { font-size: 14px; font-weight: bold; padding: 0px; }"
    );
    connect(m_refreshBtn, &QPushButton::clicked, this, &ToolBar::refreshSerialPorts);
    layout->addWidget(m_refreshBtn);

    m_serialToggle = new QPushButton("打开串口", this);
    m_serialToggle->setFixedHeight(ctrlH);
    m_serialToggle->setStyleSheet(Styles::STYLE_BTN_IDLE());
    connect(m_serialToggle, &QPushButton::clicked, this, &ToolBar::toggleSerial);
    layout->addWidget(m_serialToggle);

    layout->addSpacing(16);

    // ── UDP 区段 ──
    auto* udpLabel = new QLabel("UDP端口 ");
    udpLabel->setStyleSheet(Styles::STYLE_LABEL());
    layout->addWidget(udpLabel);

    auto& cfg = ConfigLoader::instance();
    UdpConfig udpCfg = cfg.getUdpConfig();

    m_udpPortSpin = new QSpinBox(this);
    m_udpPortSpin->setRange(1, 65535);
    m_udpPortSpin->setValue(udpCfg.port);
    m_udpPortSpin->setFixedHeight(ctrlH);
    m_udpPortSpin->setFixedWidth(72);
    m_udpPortSpin->setStyleSheet(Styles::STYLE_SPINBOX());
    layout->addWidget(m_udpPortSpin);

    m_udpToggle = new QPushButton("接收UDP", this);
    m_udpToggle->setFixedHeight(ctrlH);
    m_udpToggle->setStyleSheet(Styles::STYLE_BTN_IDLE());
    connect(m_udpToggle, &QPushButton::clicked, this, &ToolBar::toggleUdp);
    layout->addWidget(m_udpToggle);

    layout->addStretch();

    // 初始化串口下拉列表
    refreshSerialPorts();
}

// 绑定数据接收器，连接异步断开信号
void ToolBar::bindDataReceiver(DataReceiver* recv) {
    m_recv = recv;
    if (m_recv) {
        connect(m_recv, &DataReceiver::serialStopped, this, &ToolBar::onSerialStopped);
    }
}

// 刷新串口下拉列表（枚举当前系统中的串口）
void ToolBar::refreshSerialPorts() {
    QString current = m_serialCombo->currentData().toString();
    m_serialCombo->clear();

    auto& cfg = ConfigLoader::instance();
    SerialConfig sCfg = cfg.getSerialConfig();
    QString cfgPort = sCfg.port;

#ifdef HAVE_QT_SERIAL_PORT
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    std::sort(ports.begin(), ports.end(), [](const QSerialPortInfo& a, const QSerialPortInfo& b){
        return a.portName() < b.portName();
    });

    int selectIdx = 0;
    for (int i = 0; i < ports.size(); ++i) {
        const auto& info = ports[i];
        QString label = (!info.description().isEmpty() && info.description() != "n/a")
            ? QString("%1  %2").arg(info.portName(), info.description())
            : info.portName();
        m_serialCombo->addItem(label, info.portName());
        if (info.portName() == current ||
            (current.isEmpty() && info.portName() == cfgPort)) {
            selectIdx = i;
        }
    }
    if (m_serialCombo->count() > 0)
        m_serialCombo->setCurrentIndex(selectIdx);
#else
    // Qt6SerialPort 未安装，显示提示条目
    m_serialCombo->addItem("(SerialPort 模块未安装)", QString());
    m_serialCombo->setEnabled(false);
    m_serialToggle->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    Q_UNUSED(cfgPort); Q_UNUSED(current);
#endif
}

// 切换串口连接状态
void ToolBar::toggleSerial() {
    if (!m_recv) return;

    if (m_recv->isSerialRunning()) {
        // 已连接 → 断开
        m_recv->stopSerial();
        setSerialUiIdle();
    } else {
        if (m_serialCombo->count() == 0) return;
        QString port = m_serialCombo->currentData().toString();
        auto& cfg = ConfigLoader::instance();
        SerialConfig sCfg = cfg.getSerialConfig();
        m_recv->startSerial(port, sCfg.baudrate, sCfg.protocol,
                            sCfg.accFsr, sCfg.gyroFsr);
        m_serialToggle->setText("关闭串口");
        m_serialToggle->setStyleSheet(Styles::STYLE_BTN_ACTIVE());
        m_serialCombo->setEnabled(false);
        m_refreshBtn->setEnabled(false);
    }
}

// 切换 UDP 接收状态
void ToolBar::toggleUdp() {
    if (!m_recv) return;

    if (m_recv->isUdpRunning()) {
        m_recv->stopUdp();
        m_udpToggle->setText("接收UDP");
        m_udpToggle->setStyleSheet(Styles::STYLE_BTN_IDLE());
        m_udpPortSpin->setEnabled(true);
    } else {
        auto& cfg = ConfigLoader::instance();
        UdpConfig uCfg = cfg.getUdpConfig();
        int port = m_udpPortSpin->value();
        m_recv->startUdp(uCfg.ip, port);
        m_udpToggle->setText("停止接收");
        m_udpToggle->setStyleSheet(Styles::STYLE_BTN_ACTIVE());
        m_udpPortSpin->setEnabled(false);
    }
}

// 串口异步断开时恢复 UI 为空闲状态
void ToolBar::onSerialStopped() {
    setSerialUiIdle();
}

// 将串口相关控件恢复为未连接状态
void ToolBar::setSerialUiIdle() {
    m_serialToggle->setText("打开串口");
    m_serialToggle->setStyleSheet(Styles::STYLE_BTN_IDLE());
    m_serialCombo->setEnabled(true);
    m_refreshBtn->setEnabled(true);
    emit serialStopRequested();
}
