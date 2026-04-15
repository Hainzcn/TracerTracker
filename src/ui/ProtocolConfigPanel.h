#pragma once
#include <QWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QJsonObject>
#include <QMap>
#include "config/ConfigTypes.h"

// ============================================================
// ProtocolConfigPanel — 协议配置侧滑面板
//
// 区域 A（P4）：协议选择、字段预览、变量编辑
// 区域 B（P5）：语义字段映射器（purpose → protocol field）
// ============================================================

class ProtocolConfigPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProtocolConfigPanel(QWidget* parent = nullptr);

    void loadFromConfig();

signals:
    void applied();
    void closeRequested();

private slots:
    void onProtocolChanged(int index);
    void onApply();
    void onReset();

private:
    // 每种 purpose 的一行映射 UI
    struct MappingRow {
        QString  axisLabel;   // "X", "Y", "Z", "W", "altitude", "pressure"
        QComboBox*      fieldCombo = nullptr;
        QDoubleSpinBox* multSpin   = nullptr;
    };

    struct PurposeGroup {
        QString          purpose;
        QString          displayName;
        QList<MappingRow> rows;
    };

    void buildUI();
    void buildProtocolSection(QVBoxLayout* container);
    void buildMappingSection(QVBoxLayout* container);
    void buildActionBar(QVBoxLayout* container);

    void refreshProtocolInfo(const QJsonObject& protoDef);
    void refreshFieldCombos(const QStringList& fieldNames);
    void autoMatchFields(const QStringList& fieldNames);
    void loadMappingsFromConfig();

    QList<PointConfig> collectSensorPoints() const;
    QVariantMap collectVariables() const;

    // 协议选择区
    QComboBox*   m_protocolCombo  = nullptr;
    QLabel*      m_descLabel      = nullptr;
    QLabel*      m_fieldListLabel = nullptr;
    QFormLayout* m_varsLayout     = nullptr;
    QMap<QString, QDoubleSpinBox*> m_varSpins;

    // 字段映射区
    QList<PurposeGroup> m_purposeGroups;

    // 当前预览的协议定义（可能与 ConfigLoader 的活跃协议不同）
    QJsonObject m_previewProtoDef;

    static constexpr int PANEL_WIDTH = 360;
};
