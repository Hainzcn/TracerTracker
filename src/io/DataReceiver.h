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
#include "TextProtocolParser.h"
#include "config/ConfigLoader.h"

// ============================================================
// DataReceiver.h — 数据接收器（后台线程 + Qt 信号槽）
//
// 架构：
//   - UdpWorker    运行在独立 QThread 中，使用事件驱动 QUdpSocket
//   - SerialWorker 运行在独立 QThread 中，使用 QSerialPort
//   - DataReceiver 作为主控对象，提供公共接口和信号
//
// 文本 / 二进制协议由 TextProtocolParser / GenericFrameParser 两个
// 配置驱动的解析器处理；DataReceiver 按 framing.type 分派到不同路径。
//
// 所有信号均通过 Qt::QueuedConnection 跨线程发送到主线程
// ============================================================

// ── UDP 工作者对象（运行在后台线程）────────────────────────────

class UdpWorker : public QObject {
    Q_OBJECT
public:
    explicit UdpWorker(QObject* parent = nullptr);

public slots:
    // 启动 UDP 接收；protocolDef 为文本协议定义（UDP 只处理文本）
    void startReceiving(const QString& ip, int port,
                        const QJsonObject& protocolDef);
    void stopReceiving();

signals:
    void dataReceived(const QString& source, const QString& prefix,
                      const QList<double>& data);
    void rawDataReceived(const QString& source, const QString& raw);

private slots:
    void onReadyRead();

private:
    QUdpSocket*        m_socket = nullptr;
    bool               m_running = false;
    TextProtocolParser m_textParser;
};

// ── 串口工作者对象（运行在后台线程）────────────────────────────

class SerialWorker : public QObject {
    Q_OBJECT
public:
    explicit SerialWorker(QObject* parent = nullptr);

public slots:
    // 启动文本协议模式（text_csv / text_regex）
    void startText(const QString& port, int baudrate,
                   const QJsonObject& protocolDef);
    // 启动二进制协议模式（header_length 等）
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
    void onTextReadyRead();
    void onProtocolReadyRead();
#endif

private:
#ifdef HAVE_QT_SERIAL_PORT
    QSerialPort*       m_serial = nullptr;
#endif
    GenericFrameParser m_genericParser;
    TextProtocolParser m_textParser;
    bool               m_running = false;

    // 是否使用文本模式（区分 onTextReadyRead / onProtocolReadyRead 行为）
    bool m_textMode = false;
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
    // protocol 参数保留供显式覆盖；默认走 ConfigLoader 的活跃协议
    void startSerial(const QString& port, int baudrate,
                     const QString& protocol = QString());
    void stopSerial();
    void stopAll();

signals:
    void dataReceived(const QString& source, const QString& prefix,
                      const QList<double>& data);
    void rawDataReceived(const QString& source, const QString& raw);
    void parsedDataReceived(const QString& source, const QString& summary);
    void serialStopped();

    // 内部信号
    void _startUdpWorker(const QString& ip, int port,
                         const QJsonObject& protocolDef);
    void _stopUdpWorker();
    void _startSerialText(const QString& port, int baud,
                          const QJsonObject& protocolDef);
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

    // 为 UDP 选择文本协议定义：若活跃协议为 text_* 则复用；否则加载 csv
    static QJsonObject resolveUdpTextProtocol();
};
