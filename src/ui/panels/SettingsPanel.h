#pragma once
#include "ui/common/SidePanel.h"
#include "ui/common/FocusControls.h"

#include <QMap>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QFormLayout;
class QVBoxLayout;

// ============================================================
// SettingsPanel — 通用设置侧滑面板
//
// 覆盖 config.json 中除「协议 / 字段映射」之外的全部段：
//   - 重力参考       gravity_reference
//   - 串口口型       serial.{enabled, port, baudrate, data_bits, parity,
//                            stop_bits, flow_control, timeout}
//   - UDP            udp.{enabled, ip, port}
//   - INS            ins.{kalman.*, zupt.*, madgwick.*, mahony.*,
//                          baro_lpf_alpha, filter_yaw_offset_deg}
//   - 渲染调试       render_debug.{enabled, verbose_point_updates}
//
// 控件按 key（如 "serial.baudrate" / "ins.kalman.enabled"）注册到 4 个
// QMap，统一在 loadFromConfig / collectAndPersist 中查表读写，避免散落
// 一堆成员指针。
//
// 三个动作按钮的语义（与 ProtocolConfigPanel 不同）：
//   重置  ：丢弃 UI 改动，从 config.json 重新加载
//   确定  ：写盘并通过 confirmed() 通知 MainWindow 关闭面板（下次启动生效）
//   应用  ：写盘并通过 applied()  通知 MainWindow 立刻重启接收线程
// ============================================================

class SettingsPanel : public SidePanel {
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget* parent = nullptr);

    void loadFromConfig();

    // 与 ProtocolConfigPanel::PANEL_WIDTH 同值，便于 MainWindow 复用动画位移
    static constexpr int PANEL_WIDTH = SidePanel::DEFAULT_WIDTH;

    // ComboItem 暴露为 public 供 .cpp 中匿名命名空间内的静态枚举常量
    // （baudrateItems / dataBitsItems / ... ）使用
    struct ComboItem {
        QString label;
        int     value;
    };

signals:
    void applied();
    void confirmed();

private slots:
    void onReset();
    void onConfirm();
    void onApply();

private:

    void buildContent();
    void buildGravitySection (QVBoxLayout* container);
    void buildSerialSection  (QVBoxLayout* container);
    void buildUdpSection     (QVBoxLayout* container);
    void buildInsSection     (QVBoxLayout* container);
    void buildRenderSection  (QVBoxLayout* container);
    void buildActions();

    // ── 控件构造助手（统一缩进 / 大小 / 滚轮屏蔽）──
    void addSectionTitle(QVBoxLayout* container, const QString& text);
    void addSubTitle    (QVBoxLayout* container, const QString& text);
    QFormLayout* addFormBlock(QVBoxLayout* container, int leftIndent);

    QCheckBox*     addCheckBox (QVBoxLayout* container, const QString& key,
                                const QString& text, int leftIndent);
    FocusSpinBox*  addDoubleRow(QFormLayout* form, const QString& key,
                                const QString& label,
                                double minV, double maxV,
                                int decimals, double step,
                                const QString& suffix = QString());
    QSpinBox*      addIntRow   (QFormLayout* form, const QString& key,
                                const QString& label, int minV, int maxV,
                                const QString& suffix = QString());
    FocusComboBox* addComboRow (QFormLayout* form, const QString& key,
                                const QString& label,
                                const QList<ComboItem>& items,
                                bool editable = false);
    QLineEdit*     addLineEditRow(QFormLayout* form, const QString& key,
                                  const QString& label);

    // 串口端口下拉框：枚举系统串口 + 保留 cfg 中已存在但当前未连接的端口
    void populateSerialPortCombo(const QString& currentPort);

    void writeBackAndPersist();

    // ── 控件注册表（按 key 查找）──
    QMap<QString, QCheckBox*>      m_checks;
    QMap<QString, FocusSpinBox*>   m_doubles;
    QMap<QString, QSpinBox*>       m_ints;
    QMap<QString, FocusComboBox*>  m_combos;
    QMap<QString, QLineEdit*>      m_lineEdits;

    static constexpr int INPUT_HEIGHT     = 24;
    static constexpr int VALUE_COL_WIDTH  = 140;
};
