#include "DataReceiver.h"
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QDebug>

// ============================================================
// DataReceiver.cpp — 数据接收器实现
//
// 文本协议路径 → TextProtocolParser (csv / regex 由 framing.type 分派)
// 二进制协议路径 → GenericFrameParser
// ============================================================

// ── UdpWorker ────────────────────────────────────────────────

UdpWorker::UdpWorker(QObject* parent) : QObject(parent) {}

void UdpWorker::startReceiving(const QString& ip, int port,
                                const QJsonObject& protocolDef) {
    m_running = true;
    m_socket  = new QUdpSocket(this);

    m_textParser = TextProtocolParser::fromJson(protocolDef);
    if (!m_textParser.isValid()) {
        qWarning() << "UdpWorker: 文本协议定义无效，UDP 解析将输出空快照";
    }

    if (!m_socket->bind(QHostAddress(ip), static_cast<quint16>(port))) {
        qWarning() << "UdpWorker: 绑定失败" << ip << ":" << port
                   << m_socket->errorString();
        m_running = false;
        return;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &UdpWorker::onReadyRead);
    qDebug() << "UdpWorker: 开始接收 UDP" << ip << ":" << port
             << "协议:" << protocolDef.value("name").toString()
             << "(" << m_textParser.kind() << ")";
}

void UdpWorker::stopReceiving() {
    m_running = false;
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_textParser.reset();
    qDebug() << "UdpWorker: 已停止";
}

void UdpWorker::onReadyRead() {
    if (!m_socket) return;

    while (m_socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        QByteArray data = datagram.data();

        QString rawText = QString::fromUtf8(data);
        const QString trimmed = rawText.trimmed();
        if (!trimmed.isEmpty()) {
            emit rawDataReceived("udp", trimmed);
        }

        if (!m_textParser.isValid()) continue;

        // UDP 每个 datagram 可能含多行：追加一个换行确保 feed 至少切出一行
        if (!rawText.endsWith('\n')) rawText.append('\n');
        const auto results = m_textParser.feed(rawText);
        for (const auto& r : results) {
            emit dataReceived("udp", r.prefix, r.snapshot);
        }
    }
}

// ── SerialWorker ─────────────────────────────────────────────

SerialWorker::SerialWorker(QObject* parent) : QObject(parent) {}

void SerialWorker::startText(const QString& port, int baudrate,
                              const QJsonObject& protocolDef) {
#ifdef HAVE_QT_SERIAL_PORT
    m_running  = true;
    m_textMode = true;
    m_textParser = TextProtocolParser::fromJson(protocolDef);

    if (!m_textParser.isValid()) {
        qWarning() << "SerialWorker: 文本协议定义无效，无法启动";
        m_running = false;
        emit serialStopped();
        return;
    }

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

    connect(m_serial, &QSerialPort::readyRead, this, &SerialWorker::onTextReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError err) {
        if (err != QSerialPort::NoError && m_running) {
            qWarning() << "SerialWorker: 串口错误:" << err;
            m_running = false;
            emit serialStopped();
        }
    });

    qDebug() << "SerialWorker: 文本协议串口已打开" << port << baudrate
             << "协议:" << protocolDef.value("name").toString()
             << "(" << m_textParser.kind() << ")";
#else
    Q_UNUSED(port); Q_UNUSED(baudrate); Q_UNUSED(protocolDef);
    qWarning() << "SerialWorker: Qt6SerialPort 未安装，串口功能不可用";
    emit serialStopped();
#endif
}

void SerialWorker::startProtocol(const QString& port, int baudrate,
                                  const QJsonObject& protocolDef,
                                  const QVariantMap& variableOverrides) {
#ifdef HAVE_QT_SERIAL_PORT
    m_running  = true;
    m_textMode = false;
    m_genericParser = GenericFrameParser::fromJson(protocolDef, variableOverrides);

    if (!m_genericParser.isValid()) {
        qWarning() << "SerialWorker: 协议定义无效，无法启动";
        m_running = false;
        emit serialStopped();
        return;
    }

    m_serial = new QSerialPort(this);
    m_serial->setPortName(port);
    m_serial->setBaudRate(static_cast<QSerialPort::BaudRate>(baudrate));
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadOnly)) {
        qWarning() << "SerialWorker: 无法打开串口（协议模式）" << port;
        m_running = false;
        emit serialStopped();
        return;
    }

    connect(m_serial, &QSerialPort::readyRead, this, &SerialWorker::onProtocolReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError err) {
        if (err != QSerialPort::NoError && m_running) {
            qWarning() << "SerialWorker: 串口错误（协议模式）:" << err;
            m_running = false;
            emit serialStopped();
        }
    });

    qDebug() << "SerialWorker: 二进制协议模式串口已打开" << port << baudrate
             << "协议:" << protocolDef.value("name").toString();
#else
    Q_UNUSED(port); Q_UNUSED(baudrate);
    Q_UNUSED(protocolDef); Q_UNUSED(variableOverrides);
    qWarning() << "SerialWorker: Qt6SerialPort 未安装，串口功能不可用";
    emit serialStopped();
#endif
}

void SerialWorker::stopReceiving() {
    m_running = false;
#ifdef HAVE_QT_SERIAL_PORT
    if (m_serial) {
        m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
#endif
    m_genericParser.reset();
    m_textParser.reset();
    qDebug() << "SerialWorker: 已停止";
}

#ifdef HAVE_QT_SERIAL_PORT
void SerialWorker::onTextReadyRead() {
    if (!m_serial) return;
    QByteArray raw = m_serial->readAll();
    QString text = QString::fromUtf8(raw);

    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty())
        emit rawDataReceived("serial", trimmed);

    const auto results = m_textParser.feed(text);
    for (const auto& r : results) {
        emit parsedDataReceived("serial", m_textParser.formatDebug(r.snapshot));
        emit dataReceived("serial", r.prefix, r.snapshot);
    }
}

void SerialWorker::onProtocolReadyRead() {
    if (!m_serial) return;
    QByteArray raw = m_serial->readAll();
    emit rawDataReceived("serial", raw.toHex(' '));

    QList<QList<double>> snapshots = m_genericParser.feed(raw);
    for (const QList<double>& snap : snapshots) {
        emit parsedDataReceived("serial", m_genericParser.formatDebug(snap));
        emit dataReceived("serial", QString(), snap);
    }
}
#endif // HAVE_QT_SERIAL_PORT

// ── DataReceiver ─────────────────────────────────────────────

DataReceiver::DataReceiver(QObject* parent) : QObject(parent) {}

DataReceiver::~DataReceiver() {
    stopAll();
}

void DataReceiver::ensureUdpWorker() {
    if (m_udpWorker) return;
    m_udpThread = new QThread(this);
    m_udpWorker = new UdpWorker();
    m_udpWorker->moveToThread(m_udpThread);

    connect(m_udpWorker, &UdpWorker::dataReceived,
            this, &DataReceiver::dataReceived);
    connect(m_udpWorker, &UdpWorker::rawDataReceived,
            this, &DataReceiver::rawDataReceived);

    connect(this, &DataReceiver::_startUdpWorker,
            m_udpWorker, &UdpWorker::startReceiving);
    connect(this, &DataReceiver::_stopUdpWorker,
            m_udpWorker, &UdpWorker::stopReceiving);

    connect(m_udpThread, &QThread::finished,
            m_udpWorker, &QObject::deleteLater);

    m_udpThread->start();
}

void DataReceiver::ensureSerialWorker() {
    if (m_serialWorker) return;
    m_serialThread = new QThread(this);
    m_serialWorker = new SerialWorker();
    m_serialWorker->moveToThread(m_serialThread);

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

    connect(this, &DataReceiver::_startSerialText,
            m_serialWorker, &SerialWorker::startText);
    connect(this, &DataReceiver::_startSerialProtocol,
            m_serialWorker, &SerialWorker::startProtocol);
    connect(this, &DataReceiver::_stopSerialWorker,
            m_serialWorker, &SerialWorker::stopReceiving);

    connect(m_serialThread, &QThread::finished,
            m_serialWorker, &QObject::deleteLater);

    m_serialThread->start();
}

QJsonObject DataReceiver::resolveUdpTextProtocol() {
    auto& cfg = ConfigLoader::instance();
    // 若当前活跃协议为文本类型，UDP 复用之
    if (cfg.isTextProtocol() && !cfg.getProtocolDef().isEmpty())
        return cfg.getProtocolDef();
    // 否则加载 csv.json 作为 UDP 默认文本协议
    QJsonObject csv = ConfigLoader::loadProtocolDef("csv");
    if (csv.isEmpty()) {
        qWarning() << "DataReceiver: protocols/csv.json 不存在，UDP 将以空协议启动";
    }
    return csv;
}

void DataReceiver::startUdp(const QString& ip, int port) {
    if (m_udpRunning) return;
    ensureUdpWorker();
    m_udpRunning = true;
    emit _startUdpWorker(ip, port, resolveUdpTextProtocol());
    qDebug() << "DataReceiver: 启动 UDP" << ip << ":" << port;
}

void DataReceiver::stopUdp() {
    if (!m_udpRunning) return;
    m_udpRunning = false;
    emit _stopUdpWorker();
    qDebug() << "DataReceiver: 停止 UDP";
}

void DataReceiver::startSerial(const QString& port, int baudrate,
                                const QString& protocol) {
    if (m_serialRunning) return;
    ensureSerialWorker();
    m_serialRunning = true;

    auto& cfg = ConfigLoader::instance();

    // 协议定义优先级：显式 protocol 参数 > ConfigLoader 活跃协议
    QJsonObject protocolDef;
    if (!protocol.isEmpty()) {
        protocolDef = ConfigLoader::loadProtocolDef(protocol);
    } else {
        protocolDef = cfg.getProtocolDef();
    }

    if (protocolDef.isEmpty()) {
        qWarning() << "DataReceiver: 协议定义未加载，尝试回退到 csv";
        protocolDef = ConfigLoader::loadProtocolDef("csv");
    }

    const QString framingType = protocolDef.value("framing").toObject()
                                           .value("type").toString();

    if (framingType.startsWith("text_")) {
        emit _startSerialText(port, baudrate, protocolDef);
    } else if (!framingType.isEmpty()) {
        emit _startSerialProtocol(port, baudrate, protocolDef,
                                  cfg.getProtocolVariableOverrides());
    } else {
        qWarning() << "DataReceiver: 无法识别 framing.type，回退为 csv 文本";
        QJsonObject csv = ConfigLoader::loadProtocolDef("csv");
        emit _startSerialText(port, baudrate, csv);
    }

    qDebug() << "DataReceiver: 启动串口" << port << baudrate
             << "协议:" << protocolDef.value("name").toString()
             << "framing:" << framingType;
}

void DataReceiver::stopSerial() {
    if (!m_serialRunning) return;
    m_serialRunning = false;
    emit _stopSerialWorker();
    qDebug() << "DataReceiver: 停止串口";
}

void DataReceiver::stopAll() {
    stopUdp();
    stopSerial();
    if (m_udpThread && m_udpThread->isRunning()) {
        m_udpThread->quit();
        m_udpThread->wait(2000);
    }
    if (m_serialThread && m_serialThread->isRunning()) {
        m_serialThread->quit();
        m_serialThread->wait(2000);
    }
}
