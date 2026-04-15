#include "ProtocolConfigPanel.h"
#include "Styles.h"
#include "config/ConfigLoader.h"
#include <QJsonArray>
#include <QFrame>
#include <QDebug>

// ============================================================
// ProtocolConfigPanel.cpp — 协议配置面板实现
// ============================================================

ProtocolConfigPanel::ProtocolConfigPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("configPanel");
    setFixedWidth(PANEL_WIDTH);
    setStyleSheet(Styles::CONFIG_PANEL_STYLE());
    buildUI();
    loadFromConfig();
}

// ── UI 构建 ──────────────────────────────────────────────────

void ProtocolConfigPanel::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // 标题栏
    auto* titleBar = new QHBoxLayout();
    titleBar->setContentsMargins(12, 8, 8, 8);
    auto* titleLabel = new QLabel("协议配置", this);
    titleLabel->setObjectName("sectionTitle");
    titleBar->addWidget(titleLabel);
    titleBar->addStretch();
    auto* closeBtn = new QPushButton("\u2715", this);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet(Styles::CONFIG_PANEL_CLOSE_BTN_STYLE());
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &ProtocolConfigPanel::closeRequested);
    titleBar->addWidget(closeBtn);
    outerLayout->addLayout(titleBar);

    // 分隔线
    auto* sep1 = new QFrame(this);
    sep1->setStyleSheet(Styles::CONFIG_PANEL_SEPARATOR_STYLE());
    outerLayout->addWidget(sep1);

    // 滚动区域
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* scrollContent = new QWidget();
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(12, 8, 12, 8);
    scrollLayout->setSpacing(12);

    buildProtocolSection(scrollLayout);

    auto* sep2 = new QFrame(scrollContent);
    sep2->setStyleSheet(Styles::CONFIG_PANEL_SEPARATOR_STYLE());
    scrollLayout->addWidget(sep2);

    buildMappingSection(scrollLayout);
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea, 1);

    // 底部分隔线
    auto* sep3 = new QFrame(this);
    sep3->setStyleSheet(Styles::CONFIG_PANEL_SEPARATOR_STYLE());
    outerLayout->addWidget(sep3);

    buildActionBar(outerLayout);
}

void ProtocolConfigPanel::buildProtocolSection(QVBoxLayout* container)
{
    // 协议选择
    auto* protoLabel = new QLabel("协议", this);
    container->addWidget(protoLabel);

    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->setStyleSheet(Styles::STYLE_COMBO());
    connect(m_protocolCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ProtocolConfigPanel::onProtocolChanged);
    container->addWidget(m_protocolCombo);

    // 描述
    m_descLabel = new QLabel(this);
    m_descLabel->setObjectName("descLabel");
    m_descLabel->setWordWrap(true);
    container->addWidget(m_descLabel);

    // 字段列表
    auto* fieldsTitle = new QLabel("字段", this);
    container->addWidget(fieldsTitle);

    m_fieldListLabel = new QLabel(this);
    m_fieldListLabel->setObjectName("fieldListLabel");
    m_fieldListLabel->setWordWrap(true);
    container->addWidget(m_fieldListLabel);

    // 变量编辑区
    auto* varsTitle = new QLabel("变量", this);
    container->addWidget(varsTitle);

    m_varsLayout = new QFormLayout();
    m_varsLayout->setContentsMargins(0, 0, 0, 0);
    m_varsLayout->setSpacing(6);
    m_varsLayout->setLabelAlignment(Qt::AlignRight);
    container->addLayout(m_varsLayout);
}

void ProtocolConfigPanel::buildMappingSection(QVBoxLayout* container)
{
    auto* title = new QLabel("字段映射", this);
    title->setObjectName("sectionTitle");
    container->addWidget(title);

    struct PurposeDef {
        QString purpose;
        QString displayName;
        QStringList axes;
    };

    const QList<PurposeDef> defs = {
        { PointPurpose::Accelerometer, "加速度计", {"X", "Y", "Z"} },
        { PointPurpose::Gyroscope,     "陀螺仪",   {"X", "Y", "Z"} },
        { PointPurpose::Quaternion,    "四元数",   {"W", "X", "Y", "Z"} },
        { PointPurpose::MagneticField, "磁力计",   {"X", "Y", "Z"} },
        { PointPurpose::Barometer,     "气压计",   {"altitude", "pressure"} },
    };

    for (const auto& def : defs) {
        PurposeGroup group;
        group.purpose     = def.purpose;
        group.displayName = def.displayName;

        auto* groupLabel = new QLabel(
            QString("%1 (%2)").arg(def.displayName, def.purpose), this);
        groupLabel->setStyleSheet("color: #b0b0b0; font-size: 12px; padding-top: 4px;");
        container->addWidget(groupLabel);

        auto* grid = new QFormLayout();
        grid->setContentsMargins(8, 0, 0, 0);
        grid->setSpacing(4);
        grid->setLabelAlignment(Qt::AlignRight);

        for (const QString& axis : def.axes) {
            MappingRow row;
            row.axisLabel = axis;

            auto* rowLayout = new QHBoxLayout();
            rowLayout->setSpacing(4);

            row.fieldCombo = new QComboBox(this);
            row.fieldCombo->setStyleSheet(Styles::STYLE_COMBO());
            row.fieldCombo->setMinimumWidth(120);
            rowLayout->addWidget(row.fieldCombo, 1);

            auto* multLabel = new QLabel("\u00d7", this);
            multLabel->setStyleSheet("color: #888888; font-size: 11px; border: none;");
            rowLayout->addWidget(multLabel);

            row.multSpin = new QDoubleSpinBox(this);
            row.multSpin->setStyleSheet(Styles::STYLE_SPINBOX());
            row.multSpin->setRange(-1e6, 1e6);
            row.multSpin->setDecimals(4);
            row.multSpin->setValue(1.0);
            row.multSpin->setFixedWidth(80);
            rowLayout->addWidget(row.multSpin);

            grid->addRow(axis, rowLayout);
            group.rows.append(row);
        }

        container->addLayout(grid);
        m_purposeGroups.append(group);
    }
}

void ProtocolConfigPanel::buildActionBar(QVBoxLayout* container)
{
    auto* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(12, 8, 12, 8);
    actionLayout->setSpacing(8);

    auto* resetBtn = new QPushButton("重置", this);
    resetBtn->setStyleSheet(Styles::STYLE_BTN_IDLE());
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(resetBtn, &QPushButton::clicked, this, &ProtocolConfigPanel::onReset);

    auto* applyBtn = new QPushButton("应用", this);
    applyBtn->setStyleSheet(Styles::STYLE_BTN_ACTIVE());
    applyBtn->setCursor(Qt::PointingHandCursor);
    connect(applyBtn, &QPushButton::clicked, this, &ProtocolConfigPanel::onApply);

    actionLayout->addWidget(resetBtn);
    actionLayout->addStretch();
    actionLayout->addWidget(applyBtn);
    container->addLayout(actionLayout);
}

// ── 协议切换 ────────────────────────────────────────────────

void ProtocolConfigPanel::onProtocolChanged(int index)
{
    Q_UNUSED(index);
    QString name = m_protocolCombo->currentData().toString();

    if (name == "csv") {
        m_previewProtoDef = QJsonObject();
        refreshProtocolInfo(m_previewProtoDef);
        refreshFieldCombos({});
        return;
    }

    m_previewProtoDef = ConfigLoader::loadProtocolDef(name);
    refreshProtocolInfo(m_previewProtoDef);

    QStringList fields;
    QJsonArray orderArr = m_previewProtoDef.value("snapshot_order").toArray();
    for (const QJsonValue& v : orderArr)
        fields.append(v.toString());

    refreshFieldCombos(fields);
    autoMatchFields(fields);
}

void ProtocolConfigPanel::refreshProtocolInfo(const QJsonObject& protoDef)
{
    if (protoDef.isEmpty()) {
        m_descLabel->setText("CSV 文本协议（无二进制帧定义）");
        m_fieldListLabel->setText("—");

        // 清除变量编辑器
        while (m_varsLayout->count() > 0) {
            auto* item = m_varsLayout->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            if (item->layout()) {
                while (item->layout()->count() > 0) {
                    auto* sub = item->layout()->takeAt(0);
                    if (sub->widget()) sub->widget()->deleteLater();
                    delete sub;
                }
                delete item->layout();
            }
            delete item;
        }
        m_varSpins.clear();
        return;
    }

    m_descLabel->setText(protoDef.value("description").toString());

    QJsonArray orderArr = protoDef.value("snapshot_order").toArray();
    QStringList fieldNames;
    for (const QJsonValue& v : orderArr)
        fieldNames.append(v.toString());
    m_fieldListLabel->setText(fieldNames.join(", "));

    // 重建变量编辑器
    while (m_varsLayout->count() > 0) {
        auto* item = m_varsLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        if (item->layout()) {
            while (item->layout()->count() > 0) {
                auto* sub = item->layout()->takeAt(0);
                if (sub->widget()) sub->widget()->deleteLater();
                delete sub;
            }
            delete item->layout();
        }
        delete item;
    }
    m_varSpins.clear();

    QJsonObject varsObj = protoDef.value("variables").toObject();
    QJsonObject serialObj = ConfigLoader::instance().getProtocolDef()
                                .value("variables").toObject();

    for (auto it = varsObj.begin(); it != varsObj.end(); ++it) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setStyleSheet(Styles::STYLE_SPINBOX());
        spin->setRange(-1e9, 1e9);
        spin->setDecimals(2);

        // 优先使用 config.json serial 节中的覆盖值
        QJsonObject serial = ConfigLoader::instance().getSerialConfig().protocol ==
            m_protocolCombo->currentData().toString()
            ? QJsonObject() : QJsonObject();
        SerialConfig sc = ConfigLoader::instance().getSerialConfig();
        if (it.key() == "acc_fsr")
            spin->setValue(sc.accFsr);
        else if (it.key() == "gyro_fsr")
            spin->setValue(sc.gyroFsr);
        else
            spin->setValue(it.value().toDouble());

        m_varsLayout->addRow(it.key(), spin);
        m_varSpins[it.key()] = spin;
    }
}

void ProtocolConfigPanel::refreshFieldCombos(const QStringList& fieldNames)
{
    for (auto& group : m_purposeGroups) {
        for (auto& row : group.rows) {
            QString currentText = row.fieldCombo->currentData().toString();
            row.fieldCombo->blockSignals(true);
            row.fieldCombo->clear();
            row.fieldCombo->addItem("(无)", QString());
            for (const QString& f : fieldNames)
                row.fieldCombo->addItem(f, f);

            // 恢复先前选择
            int idx = row.fieldCombo->findData(currentText);
            if (idx >= 0)
                row.fieldCombo->setCurrentIndex(idx);

            row.fieldCombo->blockSignals(false);
        }
    }
}

void ProtocolConfigPanel::autoMatchFields(const QStringList& fieldNames)
{
    // 默认的自动匹配规则
    struct AutoMatch { QString purpose; QString axis; QString fieldHint; };
    const QList<AutoMatch> rules = {
        { PointPurpose::Accelerometer, "X", "ax" },
        { PointPurpose::Accelerometer, "Y", "ay" },
        { PointPurpose::Accelerometer, "Z", "az" },
        { PointPurpose::Gyroscope,     "X", "gx" },
        { PointPurpose::Gyroscope,     "Y", "gy" },
        { PointPurpose::Gyroscope,     "Z", "gz" },
        { PointPurpose::Quaternion,    "W", "q0" },
        { PointPurpose::Quaternion,    "X", "q1" },
        { PointPurpose::Quaternion,    "Y", "q2" },
        { PointPurpose::Quaternion,    "Z", "q3" },
        { PointPurpose::MagneticField, "X", "mx" },
        { PointPurpose::MagneticField, "Y", "my" },
        { PointPurpose::MagneticField, "Z", "mz" },
        { PointPurpose::Barometer,     "altitude", "altitude" },
        { PointPurpose::Barometer,     "pressure", "pressure" },
    };

    for (const auto& rule : rules) {
        if (!fieldNames.contains(rule.fieldHint)) continue;

        for (auto& group : m_purposeGroups) {
            if (group.purpose != rule.purpose) continue;
            for (auto& row : group.rows) {
                if (row.axisLabel != rule.axis) continue;
                int idx = row.fieldCombo->findData(rule.fieldHint);
                if (idx >= 0)
                    row.fieldCombo->setCurrentIndex(idx);
            }
        }
    }
}

// ── 从 ConfigLoader 加载状态 ─────────────────────────────────

void ProtocolConfigPanel::loadFromConfig()
{
    // 填充协议下拉框
    m_protocolCombo->blockSignals(true);
    m_protocolCombo->clear();
    m_protocolCombo->addItem("csv (文本)", "csv");
    QStringList protocols = ConfigLoader::availableProtocols();
    for (const QString& name : protocols)
        m_protocolCombo->addItem(name, name);

    // 选中当前活跃协议
    SerialConfig sc = ConfigLoader::instance().getSerialConfig();
    int idx = m_protocolCombo->findData(sc.protocol);
    if (idx >= 0)
        m_protocolCombo->setCurrentIndex(idx);
    m_protocolCombo->blockSignals(false);

    // 手动触发一次协议切换以加载预览
    onProtocolChanged(m_protocolCombo->currentIndex());

    // 从 config 加载现有映射
    loadMappingsFromConfig();
}

void ProtocolConfigPanel::loadMappingsFromConfig()
{
    const auto& points = ConfigLoader::instance().getPoints();

    for (auto& group : m_purposeGroups) {
        const PointConfig* pc = nullptr;
        for (const auto& p : points) {
            if (p.purpose == group.purpose) { pc = &p; break; }
        }
        if (!pc) continue;

        for (auto& row : group.rows) {
            const AxisMapping* mapping = nullptr;

            if (row.axisLabel == "X")             mapping = &pc->x;
            else if (row.axisLabel == "Y")        mapping = &pc->y;
            else if (row.axisLabel == "Z")        mapping = &pc->z;
            else if (row.axisLabel == "W")        mapping = &pc->w;
            else if (row.axisLabel == "altitude") mapping = &pc->altitude;
            else if (row.axisLabel == "pressure") mapping = &pc->pressure;

            if (!mapping) continue;

            // 尝试用 field 名匹配
            if (!mapping->field.isEmpty()) {
                int idx = row.fieldCombo->findData(mapping->field);
                if (idx >= 0)
                    row.fieldCombo->setCurrentIndex(idx);
            }
            row.multSpin->setValue(mapping->multiplier);
        }
    }
}

// ── 收集 UI 状态 ────────────────────────────────────────────

QList<PointConfig> ProtocolConfigPanel::collectSensorPoints() const
{
    QList<PointConfig> result;

    // 为 purpose → name 提供默认名称映射
    static const QMap<QString, QString> defaultNames = {
        { PointPurpose::Accelerometer, "ACC" },
        { PointPurpose::Gyroscope,     "GYR" },
        { PointPurpose::Quaternion,    "QUAT" },
        { PointPurpose::MagneticField, "MAG" },
        { PointPurpose::Barometer,     "BARO" },
    };

    for (const auto& group : m_purposeGroups) {
        // 检查此 group 是否有任何字段被映射
        bool hasAnyMapping = false;
        for (const auto& row : group.rows) {
            if (!row.fieldCombo->currentData().toString().isEmpty()) {
                hasAnyMapping = true;
                break;
            }
        }
        if (!hasAnyMapping) continue;

        PointConfig p;
        p.name    = defaultNames.value(group.purpose, group.purpose.toUpper());
        p.source  = "any";
        p.purpose = group.purpose;

        for (const auto& row : group.rows) {
            QString field = row.fieldCombo->currentData().toString();
            double mult = row.multSpin->value();

            AxisMapping axis;
            axis.field      = field;
            axis.multiplier = mult;

            if (row.axisLabel == "X")             p.x = axis;
            else if (row.axisLabel == "Y")        p.y = axis;
            else if (row.axisLabel == "Z")        p.z = axis;
            else if (row.axisLabel == "W")        p.w = axis;
            else if (row.axisLabel == "altitude") p.altitude = axis;
            else if (row.axisLabel == "pressure") p.pressure = axis;
        }

        result.append(p);
    }

    return result;
}

QVariantMap ProtocolConfigPanel::collectVariables() const
{
    QVariantMap vars;
    for (auto it = m_varSpins.begin(); it != m_varSpins.end(); ++it)
        vars[it.key()] = it.value()->value();
    return vars;
}

// ── 操作槽 ──────────────────────────────────────────────────

void ProtocolConfigPanel::onApply()
{
    QString protocol = m_protocolCombo->currentData().toString();
    QVariantMap vars = collectVariables();
    QList<PointConfig> sensorPoints = collectSensorPoints();

    ConfigLoader::instance().updateProtocolAndMappings(protocol, vars, sensorPoints);

    qDebug() << "ProtocolConfigPanel: 已应用协议配置 -" << protocol
             << "(" << sensorPoints.size() << "个传感器映射)";

    emit applied();
}

void ProtocolConfigPanel::onReset()
{
    loadFromConfig();
}
