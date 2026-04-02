#include "DataReceiver.h"
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QDebug>
#include <optional>

// ============================================================
// DataReceiver.cpp — 数据接收器实现
// ============================================================

// ── CSV 解析辅助（UdpWorker & SerialWorker 共用）────────────────

// 解析单行 CSV 文本（支持 "G:1.0,2.0" 格式前缀）
// 返回 nullopt 表示解析失败
static std::optional<UdpWorker::ParseResult> parseCsvLineImpl(const QString& text) {
    if (text.trimmed().isEmpty()) return std::nullopt;

    QString prefix;
    QString csvPart = text.trimmed();

    // 检测冒号分隔的前缀（如 "G:1,2,3"）
    int colonPos = csvPart.indexOf(':');
    if (colonPos > 0 && colonPos < 16) {
        // 前缀候选只允许简短字母数字
        QString prefixCandidate = csvPart.left(colonPos).trimmed();
        if (!prefixCandidate.isEmpty()) {
            prefix  = prefixCandidate;
            csvPart = csvPart.mid(colonPos + 1).trimmed();
        }
    }

    if (csvPart.isEmpty()) return std::nullopt;

    // 分割并转换浮点数
    QStringList parts = csvPart.split(',');
    QList<double> values;
    values.reserve(parts.size());
    bool ok;
    for (const QString& s : parts) {
        double v = s.trimmed().toDouble(&ok);
        if (!ok) return std::nullopt;
        values.append(v);
    }
    if (values.isEmpty()) return std::nullopt;

    return UdpWorker::ParseResult{prefix, values};
}

// ── UdpWorker ────────────────────────────────────────────────

UdpWorker::UdpWorker(QObject* parent) : QObject(parent) {}

// 绑定 IP:Port 并注册 readyRead 信号
void UdpWorker::startReceiving(const QString& ip, int port) {
    m_running = true;
    m_socket  = new QUdpSocket(this);

    // 绑定本地地址和端口
    if (!m_socket->bind(QHostAddress(ip), static_cast<quint16>(port))) {
        qWarning() << "UdpWorker: 绑定失败" << ip << ":" << port
                   << m_socket->errorString();
        m_running = false;
        return;
    }

    // 事件驱动：有数据到达时调用 onReadyRead
    connect(m_socket, &QUdpSocket::readyRead, this, &UdpWorker::onReadyRead);
    qDebug() << "UdpWorker: 开始接收 UDP" << ip << ":" << port;
}

// 停止 UDP 接收并销毁套接字
void UdpWorker::stopReceiving() {
    m_running = false;
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    qDebug() << "UdpWorker: 已停止";
}

// UDP 数据到达处理槽
void UdpWorker::onReadyRead() {
    if (!m_socket) return;

    // 读取所有待处理数据报
    while (m_socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        QByteArray data = datagram.data();

        // 发送原始文本（调试用）
        QString rawText = QString::fromUtf8(data).trimmed();
        if (!rawText.isEmpty()) {
            emit rawDataReceived("udp", rawText);
        }

        // 解析 CSV 数据
        auto result = parseCsvLineImpl(rawText);
        if (result.has_value()) {
            emit dataReceived("udp", result->prefix, result->values);
        }
    }
}

// ── SerialWorker ─────────────────────────────────────────────

SerialWorker::SerialWorker(QObject* parent) : QObject(parent) {}

// 启动 CSV 协议串口接收（未安装 SerialPort 时直接返回）
void SerialWorker::startCsv(const QString& port, int baudrate, int accFsr, int gyroFsr) {
#ifdef HAVE_QT_SERIAL_PORT
    m_running  = true;
    m_serial   = new QSerialPort(this);
    m_serial->setPortName(port);
    m_serial->setBaudRate(static_cast<QSerialPort::BaudRate>(baudrate));
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadOnly)) {
        qWarning() << "SerialWorker: 无法打开串口" << port << m_serial->errorString();
        m_running = false;
        emit serialStopped();
        return;
    }

    connect(m_serial, &QSerialPort::readyRead, this, &SerialWorker::onCsvReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError err) {
        if (err != QSerialPort::NoError && m_running) {
            qWarning() << "SerialWorker: 串口错误:" << err;
            m_running = false;
            emit serialStopped();
        }
    });

    qDebug() << "SerialWorker: CSV 串口已打开" << port << baudrate;
    Q_UNUSED(accFsr); Q_UNUSED(gyroFsr);
#else
    Q_UNUSED(port); Q_UNUSED(baudrate); Q_UNUSED(accFsr); Q_UNUSED(gyroFsr);
    qWarning() << "SerialWorker: Qt6SerialPort 未安装，串口功能不可用";
    emit serialStopped();
#endif
}

// 启动 ATK-MS901M 二进制协议串口接收
void SerialWorker::startBinary(const QString& port, int baudrate, int accFsr, int gyroFsr) {
#ifdef HAVE_QT_SERIAL_PORT
    m_running = true;
    m_parser  = Ms901mStreamParser(accFsr, gyroFsr);
    m_serial  = new QSerialPort(this);
    m_serial->setPortName(port);
    m_serial->setBaudRate(static_cast<QSerialPort::BaudRate>(baudrate));
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadOnly)) {
        qWarning() << "SerialWorker: 无法打开串口（二进制）" << port;
        m_running = false;
        emit serialStopped();
        return;
    }

    connect(m_serial, &QSerialPort::readyRead, this, &SerialWorker::onBinaryReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError err) {
        if (err != QSerialPort::NoError && m_running) {
            qWarning() << "SerialWorker: 串口错误（二进制）:" << err;
            m_running = false;
            emit serialStopped();
        }
    });

    qDebug() << "SerialWorker: 二进制串口已打开" << port << baudrate
             << "accFsr=" << accFsr << "gyroFsr=" << gyroFsr;
#else
    Q_UNUSED(port); Q_UNUSED(baudrate); Q_UNUSED(accFsr); Q_UNUSED(gyroFsr);
    qWarning() << "SerialWorker: Qt6SerialPort 未安装，串口功能不可用";
    emit serialStopped();
#endif
}

// 停止串口接收
void SerialWorker::stopReceiving() {
    m_running = false;
#ifdef HAVE_QT_SERIAL_PORT
    if (m_serial) {
        m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
#endif
    qDebug() << "SerialWorker: 已停止";
}

#ifdef HAVE_QT_SERIAL_PORT
// CSV 串口数据到达处理槽（按 '\n' 分帧）
void SerialWorker::onCsvReadyRead() {
    if (!m_serial) return;
    QByteArray raw = m_serial->readAll();
    emit rawDataReceived("serial", QString::fromUtf8(raw).trimmed());

    m_csvLineBuffer += QString::fromUtf8(raw);
    while (true) {
        int pos = m_csvLineBuffer.indexOf('\n');
        if (pos < 0) break;
        QString line = m_csvLineBuffer.left(pos).trimmed();
        m_csvLineBuffer = m_csvLineBuffer.mid(pos + 1);
        if (line.isEmpty()) continue;
        auto result = parseCsvLineImpl(line);
        if (result.has_value())
            emit dataReceived("serial", result->prefix, result->values);
    }
}

// 二进制串口数据到达处理槽（ATK-MS901M 协议）
void SerialWorker::onBinaryReadyRead() {
    if (!m_serial) return;
    QByteArray raw = m_serial->readAll();
    emit rawDataReceived("serial", raw.toHex(' '));

    QList<Snapshot19> snapshots = m_parser.feed(raw);
    for (const Snapshot19& snap : snapshots) {
        emit parsedDataReceived("serial", Ms901mStreamParser::formatDebug(snap));
        QList<double> data;
        data.reserve(19);
        for (double v : snap) data.append(v);
        emit dataReceived("serial", QString(), data);
    }
}
#endif // HAVE_QT_SERIAL_PORT

// SerialWorker 使用与 UdpWorker 相同的 parseCsvLine
std::optional<SerialWorker::ParseResult> SerialWorker::parseCsvLine(const QString& text) {
    return parseCsvLineImpl(text);
}

std::optional<UdpWorker::ParseResult> UdpWorker::parseCsvLine(const QString& text) {
    return parseCsvLineImpl(text);
}

// ── DataReceiver ─────────────────────────────────────────────

DataReceiver::DataReceiver(QObject* parent) : QObject(parent) {}

DataReceiver::~DataReceiver() {
    stopAll();
}

// 初始化 UDP 工作者线程（惰性创建）
void DataReceiver::ensureUdpWorker() {
    if (m_udpWorker) return;
    m_udpThread = new QThread(this);
    m_udpWorker = new UdpWorker();
    m_udpWorker->moveToThread(m_udpThread);

    // 信号转发：Worker → DataReceiver（QueuedConnection 自动跨线程）
    connect(m_udpWorker, &UdpWorker::dataReceived,
            this, &DataReceiver::dataReceived);
    connect(m_udpWorker, &UdpWorker::rawDataReceived,
            this, &DataReceiver::rawDataReceived);

    // 内部控制信号（跨线程调用 Worker 的槽）
    connect(this, &DataReceiver::_startUdpWorker,
            m_udpWorker, &UdpWorker::startReceiving);
    connect(this, &DataReceiver::_stopUdpWorker,
            m_udpWorker, &UdpWorker::stopReceiving);

    // 线程退出时清理
    connect(m_udpThread, &QThread::finished,
            m_udpWorker, &QObject::deleteLater);

    m_udpThread->start();
}

// 初始化串口工作者线程（惰性创建）
void DataReceiver::ensureSerialWorker() {
    if (m_serialWorker) return;
    m_serialThread = new QThread(this);
    m_serialWorker = new SerialWorker();
    m_serialWorker->moveToThread(m_serialThread);

    // 信号转发
    connect(m_serialWorker, &SerialWorker::dataReceived,
            this, &DataReceiver::dataReceived);
    connect(m_serialWorker, &SerialWorker::rawDataReceived,
            this, &DataReceiver::rawDataReceived);
    connect(m_serialWorker, &SerialWorker::parsedDataReceived,
            this, &DataReceiver::parsedDataReceived);
    connect(m_serialWorker, &SerialWorker::serialStopped,
            this, &DataReceiver::serialStopped);
    connect(m_serialWorker, &SerialWorker::serialStopped, this, [this]() {
        m_serialRunning = false;
    });

    // 内部控制信号
    connect(this, &DataReceiver::_startSerialCsv,
            m_serialWorker, &SerialWorker::startCsv);
    connect(this, &DataReceiver::_startSerialBinary,
            m_serialWorker, &SerialWorker::startBinary);
    connect(this, &DataReceiver::_stopSerialWorker,
            m_serialWorker, &SerialWorker::stopReceiving);

    connect(m_serialThread, &QThread::finished,
            m_serialWorker, &QObject::deleteLater);

    m_serialThread->start();
}

// 启动 UDP 接收
void DataReceiver::startUdp(const QString& ip, int port) {
    if (m_udpRunning) return;
    ensureUdpWorker();
    m_udpRunning = true;
    emit _startUdpWorker(ip, port);
    qDebug() << "DataReceiver: 启动 UDP" << ip << ":" << port;
}

// 停止 UDP 接收
void DataReceiver::stopUdp() {
    if (!m_udpRunning) return;
    m_udpRunning = false;
    emit _stopUdpWorker();
    qDebug() << "DataReceiver: 停止 UDP";
}

// 启动串口接收
void DataReceiver::startSerial(const QString& port, int baudrate,
                                const QString& protocol,
                                int accFsr, int gyroFsr) {
    if (m_serialRunning) return;
    ensureSerialWorker();
    m_serialRunning = true;
    if (protocol == "atkms901m") {
        emit _startSerialBinary(port, baudrate, accFsr, gyroFsr);
    } else {
        emit _startSerialCsv(port, baudrate, accFsr, gyroFsr);
    }
    qDebug() << "DataReceiver: 启动串口" << port << baudrate << "协议:" << protocol;
}

// 停止串口接收
void DataReceiver::stopSerial() {
    if (!m_serialRunning) return;
    m_serialRunning = false;
    emit _stopSerialWorker();
    qDebug() << "DataReceiver: 停止串口";
}

// 停止所有接收
void DataReceiver::stopAll() {
    stopUdp();
    stopSerial();
    // 等待工作者线程退出
    if (m_udpThread && m_udpThread->isRunning()) {
        m_udpThread->quit();
        m_udpThread->wait(2000);
    }
    if (m_serialThread && m_serialThread->isRunning()) {
        m_serialThread->quit();
        m_serialThread->wait(2000);
    }
}
