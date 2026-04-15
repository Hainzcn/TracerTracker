#include "ToolBar.h"
#include "Styles.h"
#include "../io/DataReceiver.h"
#include "../config/ConfigLoader.h"

#include <QLabel>
#include <QStyledItemDelegate>
#include <QAbstractItemView>
#include <QListView>
#include <QTimer>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <algorithm>
#ifdef HAVE_QT_SERIAL_PORT
#  include <QSerialPortInfo>
#endif

// ============================================================
// ToolBar.cpp — 顶部工具栏实现
// ============================================================

// ── SeamlessComboBox ─────────────────────────────────────────

SeamlessComboBox::SeamlessComboBox(QWidget* parent) : QComboBox(parent) {}

// 弹出列表齐平放在选框正下方，无重叠。
void SeamlessComboBox::showPopup() {
    QComboBox::showPopup();
    QWidget* popup = view()->window();
    if (!popup)
        return;

    QRect geo = popup->geometry();
    geo.moveTopLeft(mapToGlobal(QPoint(0, height())));
    geo.setWidth(std::max(geo.width(), width()));
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
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
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
    m_serialCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_serialCombo->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_serialCombo->view()->setContentsMargins(0, 0, 0, 0);
    // setSpacing 是 QListView 的成员，需要向下转型
    if (auto* lv = qobject_cast<QListView*>(m_serialCombo->view()))
        lv->setSpacing(0);
    layout->addWidget(m_serialCombo);

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

#ifdef HAVE_QT_SERIAL_PORT
    m_serialPollTimer = new QTimer(this);
    m_serialPollTimer->setInterval(1000);
    connect(m_serialPollTimer, &QTimer::timeout, this, &ToolBar::onSerialPollTimer);
#endif

    // 初始化串口下拉列表（有模块时随后启动轮询）
    updateSerialPortList();
    QTimer::singleShot(0, this, &ToolBar::lockSerialComboSize);
#ifdef HAVE_QT_SERIAL_PORT
    m_serialPollTimer->start();
#endif
}

// 绑定数据接收器，连接异步断开信号
void ToolBar::bindDataReceiver(DataReceiver* recv) {
    m_recv = recv;
    if (m_recv) {
        connect(m_recv, &DataReceiver::serialStopped, this, &ToolBar::onSerialStopped);
    }
}

void ToolBar::onSerialPollTimer() {
#ifdef HAVE_QT_SERIAL_PORT
    if (m_recv && m_recv->isSerialRunning())
        return;
    updateSerialPortList();
#endif
}

void ToolBar::lockSerialComboSize() {
    if (!m_serialCombo || m_serialComboSizeLocked)
        return;

    const int lockedHeight = std::max(m_serialCombo->height(), m_serialCombo->sizeHint().height());
    m_serialCombo->setFixedSize(serialComboLockedWidth(), lockedHeight);
    m_serialComboSizeLocked = true;
}

int ToolBar::serialComboLockedWidth() const {
    if (!m_serialCombo)
        return 120;

    QString widestText = m_serialCombo->currentText();
    QFontMetrics metrics(m_serialCombo->font());
    int widestTextPx = metrics.horizontalAdvance(widestText);

    for (int i = 0; i < m_serialCombo->count(); ++i) {
        const QString itemText = m_serialCombo->itemText(i);
        const int itemWidth = metrics.horizontalAdvance(itemText);
        if (itemWidth > widestTextPx) {
            widestTextPx = itemWidth;
            widestText = itemText;
        }
    }

    QStyleOptionComboBox opt;
    opt.initFrom(m_serialCombo);
    opt.currentText = widestText;
    opt.editable = m_serialCombo->isEditable();

    const QSize contentsSize(widestTextPx + 28, m_serialCombo->sizeHint().height());
    const int width = m_serialCombo->style()->sizeFromContents(
        QStyle::CT_ComboBox, &opt, contentsSize, m_serialCombo).width();
    return std::max(width, m_serialCombo->minimumSizeHint().width());
}

// 枚举串口并刷新下拉列表；若相对上次轮询出现新端口且非首轮，则自动选中最晚枚举到的新端口。
void ToolBar::updateSerialPortList() {
    auto& cfg = ConfigLoader::instance();
    SerialConfig sCfg = cfg.getSerialConfig();
    const QString cfgPort = sCfg.port;

#ifdef HAVE_QT_SERIAL_PORT
    QList<QSerialPortInfo> portsRaw = QSerialPortInfo::availablePorts();
    QSet<QString> nowNames;
    for (const auto& info : portsRaw)
        nowNames.insert(info.portName());

    // 端口集合未变则不碰下拉框，避免每秒 clear/add 引发整窗重绘与 3D 卡顿。
    if (nowNames == m_serialPortsKnown)
        return;

    const QString current = m_serialCombo->currentData().toString();
    m_serialCombo->clear();

    const QSet<QString> prevKnown = m_serialPortsKnown;

    QString preferredNewPort;
    if (!prevKnown.isEmpty()) {
        // 按 availablePorts() 顺序：同批多次出现的新口时，后以系统枚举顺序为准（通常新插入靠后）
        for (const auto& info : portsRaw) {
            if (!prevKnown.contains(info.portName()))
                preferredNewPort = info.portName();
        }
    }

    QList<QSerialPortInfo> ports = portsRaw;
    std::sort(ports.begin(), ports.end(), [](const QSerialPortInfo& a, const QSerialPortInfo& b){
        return a.portName() < b.portName();
    });

    for (int i = 0; i < ports.size(); ++i) {
        const auto& info = ports[i];
        QString label = (!info.description().isEmpty() && info.description() != "n/a")
            ? QString("%1  %2").arg(info.portName(), info.description())
            : info.portName();
        m_serialCombo->addItem(label, info.portName());
    }

    int targetIdx = 0;
    bool picked = false;
    if (!preferredNewPort.isEmpty()) {
        for (int i = 0; i < m_serialCombo->count(); ++i) {
            if (m_serialCombo->itemData(i).toString() == preferredNewPort) {
                targetIdx = i;
                picked = true;
                break;
            }
        }
    }
    if (!picked) {
        for (int i = 0; i < m_serialCombo->count(); ++i) {
            const QString name = m_serialCombo->itemData(i).toString();
            if (name == current || (current.isEmpty() && name == cfgPort)) {
                targetIdx = i;
                picked = true;
                break;
            }
        }
    }
    if (!picked && m_serialCombo->count() > 0)
        targetIdx = 0;

    if (m_serialCombo->count() > 0)
        m_serialCombo->setCurrentIndex(targetIdx);

    m_serialPortsKnown = nowNames;
#else
    m_serialCombo->clear();
    m_serialCombo->addItem("(SerialPort 模块未安装)", QString());
    m_serialCombo->setEnabled(false);
    m_serialToggle->setEnabled(false);
    Q_UNUSED(cfgPort);
#endif

    if (!m_serialComboSizeLocked)
        lockSerialComboSize();
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
        m_recv->startSerial(port, sCfg.baudrate, sCfg.protocol);
        m_serialToggle->setText("关闭串口");
        m_serialToggle->setStyleSheet(Styles::STYLE_BTN_ACTIVE());
        m_serialCombo->setEnabled(false);
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
    emit serialStopRequested();
}

