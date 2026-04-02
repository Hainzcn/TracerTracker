#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QThread>
#include <QUdpSocket>
#ifdef HAVE_QT_SERIAL_PORT
#  include <QSerialPort>
#endif
#include "Ms901mStreamParser.h"
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

    // CSV 解析结果（供 SerialWorker 引用）
    struct ParseResult { QString prefix; QList<double> values; };

public slots:
    // 绑定指定 IP:Port 并开始接收数据
    void startReceiving(const QString& ip, int port);
    // 停止接收并关闭套接字
    void stopReceiving();

signals:
    // 解析成功的数据包（source=udp, prefix 可为空字符串, data=浮点数组）
    void dataReceived(const QString& source, const QString& prefix,
                      const QList<double>& data);
    // 原始 UDP 文本（用于调试控制台显示）
    void rawDataReceived(const QString& source, const QString& raw);

private slots:
    // QUdpSocket::readyRead 信号的处理槽
    void onReadyRead();

private:
    QUdpSocket* m_socket = nullptr; // UDP 套接字（事件驱动）
    bool        m_running = false;  // 是否处于接收状态

    // 解析 CSV 文本行（支持可选前缀 "G:1,2,3" 格式）
    // 返回 (prefix, values) 或 nullopt 若解析失败
    static std::optional<ParseResult> parseCsvLine(const QString& text);
};

// ── 串口工作者对象（运行在后台线程）────────────────────────────
// 当未安装 Qt6SerialPort 时，串口功能被禁用（方法为空实现）

class SerialWorker : public QObject {
    Q_OBJECT
public:
    explicit SerialWorker(QObject* parent = nullptr);

public slots:
    // 打开指定串口并开始接收（CSV 协议）
    void startCsv(const QString& port, int baudrate, int accFsr, int gyroFsr);
    // 打开指定串口并开始接收（ATK-MS901M 二进制协议）
    void startBinary(const QString& port, int baudrate, int accFsr, int gyroFsr);
    // 停止接收并关闭串口
    void stopReceiving();

signals:
    // 解析成功的数据包
    void dataReceived(const QString& source, const QString& prefix,
                      const QList<double>& data);
    // 原始串口文本或十六进制（调试用）
    void rawDataReceived(const QString& source, const QString& raw);
    // ATK 二进制帧摘要（调试用）
    void parsedDataReceived(const QString& source, const QString& summary);
    // 串口异常断开通知（主线程应更新 UI）
    void serialStopped();

private slots:
#ifdef HAVE_QT_SERIAL_PORT
    // QSerialPort::readyRead 的 CSV 处理槽
    void onCsvReadyRead();
    // QSerialPort::readyRead 的二进制处理槽
    void onBinaryReadyRead();
#endif

private:
#ifdef HAVE_QT_SERIAL_PORT
    QSerialPort*       m_serial = nullptr; // 串口对象
#endif
    Ms901mStreamParser m_parser;           // ATK 二进制流解析器
    bool               m_running = false;  // 是否处于接收状态

    // CSV 行缓冲区（跨 readyRead 调用保持行边界）
    QString m_csvLineBuffer;

    // 共用 CSV 解析逻辑
    using ParseResult = UdpWorker::ParseResult;
    static std::optional<ParseResult> parseCsvLine(const QString& text);
};

// ── DataReceiver 主控对象（主线程持有）──────────────────────────

class DataReceiver : public QObject {
    Q_OBJECT
public:
    explicit DataReceiver(QObject* parent = nullptr);
    ~DataReceiver();

    // 查询 UDP/串口是否正在运行
    bool isUdpRunning()    const { return m_udpRunning;    }
    bool isSerialRunning() const { return m_serialRunning; }

    // 启动 UDP 接收（异步，不阻塞主线程）
    void startUdp(const QString& ip, int port);
    // 停止 UDP 接收
    void stopUdp();

    // 启动串口接收（协议由 protocol 参数决定："csv" 或 "atkms901m"）
    void startSerial(const QString& port, int baudrate,
                     const QString& protocol = "csv",
                     int accFsr = 4, int gyroFsr = 2000);
    // 停止串口接收
    void stopSerial();

    // 停止所有接收
    void stopAll();

signals:
    // 已解析的传感器数据（跨线程，QueuedConnection）
    void dataReceived(const QString& source, const QString& prefix,
                      const QList<double>& data);
    // 原始文本数据（调试控制台）
    void rawDataReceived(const QString& source, const QString& raw);
    // ATK 二进制帧摘要（调试控制台）
    void parsedDataReceived(const QString& source, const QString& summary);
    // 串口异常断开通知
    void serialStopped();

    // 内部信号：通知 Worker 线程启动/停止（通过 QueuedConnection 跨线程调用）
    void _startUdpWorker(const QString& ip, int port);
    void _stopUdpWorker();
    void _startSerialCsv(const QString& port, int baud, int accFsr, int gyroFsr);
    void _startSerialBinary(const QString& port, int baud, int accFsr, int gyroFsr);
    void _stopSerialWorker();

private:
    bool         m_udpRunning    = false;
    bool         m_serialRunning = false;

    QThread*     m_udpThread     = nullptr;
    QThread*     m_serialThread  = nullptr;
    UdpWorker*   m_udpWorker     = nullptr;
    SerialWorker* m_serialWorker  = nullptr;

    // 初始化 UDP 工作者线程（首次调用时创建）
    void ensureUdpWorker();
    // 初始化串口工作者线程（首次调用时创建）
    void ensureSerialWorker();
};
