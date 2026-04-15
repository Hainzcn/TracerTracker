#pragma once
#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QList>
#include <QThread>
#include <QUdpSocket>
#include <QVariantMap>
#ifdef HAVE_QT_SERIAL_PORT
#  include <QSerialPort>
#endif
#include "GenericFrameParser.h"
#include "config/ConfigLoader.h"

// ============================================================
// DataReceiver.h — 数据接收器（后台线程 + Qt 信号槽）
//
// 架构：
//   - UdpWorker  运行在独立 QThread 中，使用事件驱动 QUdpSocket
//   - SerialWorker 运行在独立 QThread 中，使用 QSerialPort
//   - DataReceiver 作为主控对象，提供公共接口和信号
//
// 所有信号均通过 Qt::QueuedConnection 跨线程发送到主线程
// ============================================================

// ── UDP 工作者对象（运行在后台线程）────────────────────────────

class UdpWorker : public QObject {
    Q_OBJECT
public:
    explicit UdpWorker(QObject* parent = nullptr);

    struct ParseResult { QString prefix; QList<double> values; };

public slots:
    void startReceiving(const QString& ip, int port);
    void stopReceiving();

signals:
    void dataReceived(const QString& source, const QString& prefix,
                      const QList<double>& data);
    void rawDataReceived(const QString& source, const QString& raw);

private slots:
    void onReadyRead();

private:
    QUdpSocket* m_socket = nullptr;
    bool        m_running = false;

    static std::optional<ParseResult> parseCsvLine(const QString& text);
};

// ── 串口工作者对象（运行在后台线程）────────────────────────────

class SerialWorker : public QObject {
    Q_OBJECT
public:
    explicit SerialWorker(QObject* parent = nullptr);

public slots:
    void startCsv(const QString& port, int baudrate);
    void startProtocol(const QString& port, int baudrate,
                       const QJsonObject& protocolDef,
                       const QVariantMap& variableOverrides);
    void stopReceiving();

signals:
    void dataReceived(const QString& source, const QString& prefix,
                      const QList<double>& data);
    void rawDataReceived(const QString& source, const QString& raw);
    void parsedDataReceived(const QString& source, const QString& summary);
    void serialStopped();

private slots:
#ifdef HAVE_QT_SERIAL_PORT
    void onCsvReadyRead();
    void onProtocolReadyRead();
#endif

private:
#ifdef HAVE_QT_SERIAL_PORT
    QSerialPort*       m_serial = nullptr;
#endif
    GenericFrameParser m_genericParser;
    bool               m_running = false;

    QString m_csvLineBuffer;

    using ParseResult = UdpWorker::ParseResult;
    static std::optional<ParseResult> parseCsvLine(const QString& text);
};

// ── DataReceiver 主控对象（主线程持有）──────────────────────────

class DataReceiver : public QObject {
    Q_OBJECT
public:
    explicit DataReceiver(QObject* parent = nullptr);
    ~DataReceiver();

    bool isUdpRunning()    const { return m_udpRunning;    }
    bool isSerialRunning() const { return m_serialRunning; }

    void startUdp(const QString& ip, int port);
    void stopUdp();

    // 启动串口接收（自动从 ConfigLoader 获取协议定义）
    void startSerial(const QString& port, int baudrate,
                     const QString& protocol = "csv");
    void stopSerial();
    void stopAll();

signals:
    void dataReceived(const QString& source, const QString& prefix,
                      const QList<double>& data);
    void rawDataReceived(const QString& source, const QString& raw);
    void parsedDataReceived(const QString& source, const QString& summary);
    void serialStopped();

    // 内部信号
    void _startUdpWorker(const QString& ip, int port);
    void _stopUdpWorker();
    void _startSerialCsv(const QString& port, int baud);
    void _startSerialProtocol(const QString& port, int baud,
                               const QJsonObject& protocolDef,
                               const QVariantMap& variableOverrides);
    void _stopSerialWorker();

private:
    bool         m_udpRunning    = false;
    bool         m_serialRunning = false;

    QThread*     m_udpThread     = nullptr;
    QThread*     m_serialThread  = nullptr;
    UdpWorker*   m_udpWorker     = nullptr;
    SerialWorker* m_serialWorker  = nullptr;

    void ensureUdpWorker();
    void ensureSerialWorker();
};
