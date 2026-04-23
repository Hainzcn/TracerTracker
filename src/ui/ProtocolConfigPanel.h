#pragma once
#include <QWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QJsonObject>
#include <QMap>
#include "config/ConfigTypes.h"

// ============================================================
// ProtocolConfigPanel — 协议配置侧滑面板
//
// 区域 A（P4）：协议选择、字段预览、变量编辑
// 区域 B（P5）：语义字段映射器（purpose → protocol field）
//
// 内含两个辅助子类：
//   FocusComboBox  / FocusSpinBox —— 仅在获得焦点后才响应滚轮事件；
//   未获焦点时滚轮事件忽略并向上传递，由外层滚动区消费。
// ============================================================

// 仅在焦点态响应滚轮的 QComboBox
class FocusComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit FocusComboBox(QWidget* parent = nullptr) : QComboBox(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }
protected:
    void wheelEvent(QWheelEvent* ev) override {
        if (hasFocus()) QComboBox::wheelEvent(ev);
        else            ev->ignore();
    }
};

// 仅在焦点态响应滚轮的 QDoubleSpinBox
class FocusSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit FocusSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }
protected:
    void wheelEvent(QWheelEvent* ev) override {
        if (hasFocus()) QDoubleSpinBox::wheelEvent(ev);
        else            ev->ignore();
    }
};

class ProtocolConfigPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProtocolConfigPanel(QWidget* parent = nullptr);

    void loadFromConfig();

    // 面板固定宽度（公开供 MainWindow 计算滑入/滑出位移）
    static constexpr int PANEL_WIDTH = 360;

signals:
    void applied();
    void closeRequested();

protected:
    // 禁止鼠标事件穿透到父 Viewer3D
    void mousePressEvent(QMouseEvent* ev)   override { ev->accept(); }
    void mouseReleaseEvent(QMouseEvent* ev) override { ev->accept(); }
    void mouseMoveEvent(QMouseEvent* ev)    override { ev->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* ev) override { ev->accept(); }
    void wheelEvent(QWheelEvent* ev)        override { ev->accept(); }

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

    void buildUI();
    void buildProtocolSection(QVBoxLayout* container);
    void buildMappingSection(QVBoxLayout* container);
    void buildActionBar(QVBoxLayout* container);

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
