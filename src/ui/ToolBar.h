#pragma once
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QSet>

class QTimer;

// ============================================================
// ToolBar.h — 顶部工具栏：串口 + UDP 连接控制
// 嵌入 FramelessWindow 顶栏左侧，提供串口端口选择（定时刷新）、开关
// 及 UDP 端口/开关
// ============================================================

// 自定义 ComboBox：弹出列表齐平于选框底部，无重叠。
class SeamlessComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit SeamlessComboBox(QWidget* parent = nullptr);
    void showPopup() override;
};

class ConfigLoader;
class DataReceiver;

class ToolBar : public QWidget {
    Q_OBJECT
public:
    explicit ToolBar(QWidget* parent = nullptr);

    // 绑定数据接收器（用于驱动串口/UDP 启停）
    void bindDataReceiver(DataReceiver* recv);

signals:
    // 串口停止请求（停止后清空场景）
    void serialStopRequested();

private slots:
    // 定时轮询系统串口并更新列表（空闲时）；新接入端口时自动选中
    void onSerialPollTimer();
    // 切换串口连接状态
    void toggleSerial();
    // 切换 UDP 接收状态
    void toggleUdp();
    // 串口异步断开（由 DataReceiver::serialStopped 触发）
    void onSerialStopped();

private:
    // 将串口按钮置为空闲状态
    void setSerialUiIdle();
    // 启动后依据当前内容固定串口下拉框尺寸，避免顶栏跳动
    void lockSerialComboSize();
    // 计算容纳当前下拉内容所需的固定宽度
    int serialComboLockedWidth() const;
    // 枚举串口并刷新下拉框（保留/恢复选中；若检测到新端口则优先选中）
    void updateSerialPortList();

    DataReceiver* m_recv = nullptr;

    SeamlessComboBox* m_serialCombo   = nullptr;
    QPushButton*      m_serialToggle  = nullptr;
    QSpinBox*         m_udpPortSpin   = nullptr;
    QPushButton*      m_udpToggle     = nullptr;

    QTimer*           m_serialPollTimer = nullptr;
    QSet<QString>     m_serialPortsKnown;
    bool              m_serialComboSizeLocked = false;
};
