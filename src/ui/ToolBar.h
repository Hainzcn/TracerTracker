#pragma once
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QHBoxLayout>

// ============================================================
// ToolBar.h — 顶部工具栏：串口 + UDP 连接控制
// 固定高度 32px，提供串口端口选择、刷新、开关及 UDP 端口/开关
// ============================================================

// 自定义 ComboBox：弹出菜单向上收紧圆角，避免间隙
class SeamlessComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit SeamlessComboBox(QWidget* parent = nullptr);
    void showPopup() override;
private:
    static constexpr int RADIUS = 4;
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
    // 刷新串口列表
    void refreshSerialPorts();
    // 切换串口连接状态
    void toggleSerial();
    // 切换 UDP 接收状态
    void toggleUdp();
    // 串口异步断开（由 DataReceiver::serialStopped 触发）
    void onSerialStopped();

private:
    // 将串口按钮置为空闲状态
    void setSerialUiIdle();

    DataReceiver* m_recv = nullptr;

    SeamlessComboBox* m_serialCombo   = nullptr;
    QPushButton*      m_refreshBtn    = nullptr;
    QPushButton*      m_serialToggle  = nullptr;
    QSpinBox*         m_udpPortSpin   = nullptr;
    QPushButton*      m_udpToggle     = nullptr;
};
