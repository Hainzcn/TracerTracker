#pragma once
#include "ui/common/SidePanel.h"
#include "ui/common/FocusControls.h"
#include "config/ConfigTypes.h"

#include <QLabel>
#include <QFormLayout>
#include <QJsonObject>
#include <QMap>

// ============================================================
// ProtocolConfigPanel — 协议配置侧滑面板
//
// 区域 A（P4）：协议选择、字段预览、变量编辑
// 区域 B（P5）：语义字段映射器（purpose → protocol field）
//
// 继承自 SidePanel：标题栏 / 关闭按钮 / 滚动区 / 动作栏 / QSS 与
// 鼠标穿透屏蔽全部由基类提供，本类只关心业务控件构建与配置回填。
// ============================================================

class ProtocolConfigPanel : public SidePanel {
    Q_OBJECT
public:
    explicit ProtocolConfigPanel(QWidget* parent = nullptr);

    void loadFromConfig();

    // 面板固定宽度（公开供 MainWindow 计算滑入/滑出位移）
    static constexpr int PANEL_WIDTH = SidePanel::DEFAULT_WIDTH;

signals:
    void applied();

private slots:
    void onProtocolChanged(int index);
    void onApply();
    void onReset();

private:
    struct MappingRow {
        QString         axisLabel;
        FocusComboBox*  fieldCombo = nullptr;
        FocusSpinBox*   multSpin   = nullptr;
    };

    struct PurposeGroup {
        QString           purpose;
        QString           displayName;
        QList<MappingRow> rows;
    };

    void buildContent();
    void buildProtocolSection(QVBoxLayout* container);
    void buildMappingSection(QVBoxLayout* container);
    void buildActions();

    void refreshProtocolInfo(const QJsonObject& protoDef);
    void refreshFieldCombos(const QStringList& fieldNames);
    void autoMatchFields(const QStringList& fieldNames);
    void loadMappingsFromConfig();
    void clearVarsLayout();

    QList<PointConfig> collectSensorPoints() const;
    QVariantMap collectVariables() const;

    // 协议选择区
    FocusComboBox* m_protocolCombo  = nullptr;
    QLabel*        m_descLabel      = nullptr;
    QLabel*        m_fieldListLabel = nullptr;
    QFormLayout*   m_varsLayout     = nullptr;
    QMap<QString, FocusSpinBox*> m_varSpins;

    // 文本协议参数区（text_csv / text_regex 时显示；仅只读展示）
    QLabel*        m_varsTitleLabel  = nullptr;   // "量程" 小标题（非文本协议时显示）
    QLabel*        m_textTitleLabel  = nullptr;   // "文本参数" 小标题
    QLabel*        m_textParamsLabel = nullptr;   // 多行只读内容

    // 字段映射区
    QList<PurposeGroup> m_purposeGroups;

    // 当前预览的协议定义（可能与 ConfigLoader 的活跃协议不同）
    QJsonObject m_previewProtoDef;

    static constexpr int SPIN_WIDTH  = 56;
    static constexpr int VAR_SPIN_WIDTH = 96;

    // 统一输入控件高度，保证下拉框与数值框等高对齐
    static constexpr int INPUT_HEIGHT = 24;
};
