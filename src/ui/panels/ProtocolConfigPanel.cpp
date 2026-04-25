#include "ui/panels/ProtocolConfigPanel.h"
#include "config/ConfigLoader.h"

#include <QPushButton>
#include <QJsonArray>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QDebug>

// ============================================================
// ProtocolConfigPanel.cpp — 协议配置面板实现
//
// 标题栏 / 关闭按钮 / 滚动区 / 动作栏 / QSS 由 SidePanel 基类
// 提供。本文件只负责具体业务区段（协议定义、字段映射）的构建
// 与 ConfigLoader 之间的双向同步。
// ============================================================

// ── 辅助：把 var key 翻译为带单位的中文标签 ──────────────────

static QString varDisplayLabel(const QString& key) {
    if (key == "acc_fsr")  return "加速度计 (g)";
    if (key == "gyro_fsr") return "陀螺仪 (°/s)";
    return key;
}

// ── 构造 ────────────────────────────────────────────────────

ProtocolConfigPanel::ProtocolConfigPanel(QWidget* parent)
    : SidePanel("协议配置", PANEL_WIDTH, parent)
{
    buildContent();
    buildActions();
    loadFromConfig();
}

// ── UI 构建 ──────────────────────────────────────────────────

void ProtocolConfigPanel::buildContent()
{
    auto* layout = contentLayout();

    buildProtocolSection(layout);
    layout->addWidget(makeSeparator(layout->parentWidget()));
    buildMappingSection(layout);
    layout->addStretch();
}

void ProtocolConfigPanel::buildProtocolSection(QVBoxLayout* container)
{
    // L1 段落标题
    auto* header = new QLabel("协议定义", this);
    header->setObjectName("sectionTitle");
    container->addWidget(header);

    // L2：协议选择行（「协议」+ 下拉框）
    auto* protoRow = new QHBoxLayout();
    protoRow->setContentsMargins(INDENT_L2, 0, 0, 0);
    protoRow->setSpacing(10);

    auto* protoLabel = new QLabel("协议", this);
    protoLabel->setFixedWidth(44);
    protoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    protoRow->addWidget(protoLabel, 0, Qt::AlignVCenter);

    m_protocolCombo = new FocusComboBox(this);
    m_protocolCombo->setFixedHeight(INPUT_HEIGHT);
    m_protocolCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_protocolCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ProtocolConfigPanel::onProtocolChanged);
    protoRow->addWidget(m_protocolCombo, 1, Qt::AlignVCenter);
    container->addLayout(protoRow);

    // L2：描述文字
    m_descLabel = new QLabel(this);
    m_descLabel->setObjectName("descLabel");
    m_descLabel->setWordWrap(true);
    container->addLayout(wrapIndent(m_descLabel, INDENT_L2));

    // L2：字段小标题
    auto* fieldsTitle = new QLabel("字段", this);
    fieldsTitle->setObjectName("subLabel");
    fieldsTitle->setContentsMargins(0, 4, 0, 0);
    container->addLayout(wrapIndent(fieldsTitle, INDENT_L2));

    // L3：字段列表（带边框背景，需通过 HBox 包裹实现缩进）
    m_fieldListLabel = new QLabel(this);
    m_fieldListLabel->setObjectName("fieldListLabel");
    m_fieldListLabel->setWordWrap(true);
    m_fieldListLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    container->addLayout(wrapIndent(m_fieldListLabel, INDENT_L3));

    // L2：量程小标题（仅二进制协议显示）
    m_varsTitleLabel = new QLabel("量程", this);
    m_varsTitleLabel->setObjectName("subLabel");
    m_varsTitleLabel->setContentsMargins(0, 4, 0, 0);
    container->addLayout(wrapIndent(m_varsTitleLabel, INDENT_L2));

    // L3：量程参数编辑表单
    m_varsLayout = new QFormLayout();
    m_varsLayout->setContentsMargins(INDENT_L3, 0, 0, 0);
    m_varsLayout->setSpacing(6);
    m_varsLayout->setHorizontalSpacing(12);
    m_varsLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_varsLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    // 字段列允许扩展，使内部"右贴齐"包装器能将数值框推到右侧
    m_varsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    container->addLayout(m_varsLayout);

    // L2：文本参数小标题（仅 text_csv / text_regex 协议显示）
    m_textTitleLabel = new QLabel("文本参数", this);
    m_textTitleLabel->setObjectName("subLabel");
    m_textTitleLabel->setContentsMargins(0, 4, 0, 0);
    container->addLayout(wrapIndent(m_textTitleLabel, INDENT_L2));

    // L3：文本参数内容（多行只读）
    m_textParamsLabel = new QLabel(this);
    m_textParamsLabel->setObjectName("fieldListLabel");
    m_textParamsLabel->setWordWrap(true);
    m_textParamsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    container->addLayout(wrapIndent(m_textParamsLabel, INDENT_L3));
}

void ProtocolConfigPanel::buildMappingSection(QVBoxLayout* container)
{
    // L1 段落标题
    auto* title = new QLabel("字段映射", this);
    title->setObjectName("sectionTitle");
    container->addWidget(title);

    struct PurposeDef {
        QString purpose;
        QString shortName;     // 大写简称
        QStringList axes;
    };

    const QList<PurposeDef> defs = {
        { PointPurpose::Accelerometer, "ACC",  {"X", "Y", "Z"} },
        { PointPurpose::Gyroscope,     "GYR",  {"X", "Y", "Z"} },
        { PointPurpose::Quaternion,    "QUAT", {"W", "X", "Y", "Z"} },
        { PointPurpose::MagneticField, "MAG",  {"X", "Y", "Z"} },
        { PointPurpose::Barometer,     "BARO", {"altitude", "pressure"} },
    };

    for (const auto& def : defs) {
        PurposeGroup group;
        group.purpose     = def.purpose;
        group.displayName = def.shortName;

        // L2：传感器分组短名（ACC / GYR / ...）
        auto* groupLabel = new QLabel(def.shortName, this);
        groupLabel->setObjectName("groupLabel");
        container->addLayout(wrapIndent(groupLabel, INDENT_L2));

        // L3：该传感器的轴映射表单
        auto* grid = new QFormLayout();
        grid->setContentsMargins(INDENT_L3, 2, 0, 6);
        grid->setSpacing(6);
        grid->setHorizontalSpacing(10);
        grid->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        grid->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        grid->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        for (const QString& axis : def.axes) {
            MappingRow row;
            row.axisLabel = axis;

            auto* rowLayout = new QHBoxLayout();
            rowLayout->setSpacing(6);
            rowLayout->setContentsMargins(0, 0, 0, 0);

            row.fieldCombo = new FocusComboBox(this);
            row.fieldCombo->setFixedHeight(INPUT_HEIGHT);
            row.fieldCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            rowLayout->addWidget(row.fieldCombo, 1, Qt::AlignVCenter);

            auto* multLabel = new QLabel("\u00d7", this);
            multLabel->setObjectName("multSign");
            rowLayout->addWidget(multLabel, 0, Qt::AlignVCenter);

            row.multSpin = new FocusSpinBox(this);
            row.multSpin->setRange(-1e6, 1e6);
            row.multSpin->setDecimals(2);
            row.multSpin->setValue(1.0);
            row.multSpin->setFixedWidth(SPIN_WIDTH);
            row.multSpin->setFixedHeight(INPUT_HEIGHT);
            row.multSpin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            row.multSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            row.multSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rowLayout->addWidget(row.multSpin, 0, Qt::AlignVCenter);

            grid->addRow(axis, rowLayout);
            group.rows.append(row);
        }

        container->addLayout(grid);
        m_purposeGroups.append(group);
    }
}

void ProtocolConfigPanel::buildActions()
{
    auto* bar = actionLayout();

    auto* resetBtn = new QPushButton("重置", this);
    resetBtn->setObjectName("resetBtn");
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(resetBtn, &QPushButton::clicked, this, &ProtocolConfigPanel::onReset);

    auto* applyBtn = new QPushButton("应用", this);
    applyBtn->setObjectName("applyBtn");
    applyBtn->setCursor(Qt::PointingHandCursor);
    connect(applyBtn, &QPushButton::clicked, this, &ProtocolConfigPanel::onApply);

    bar->addWidget(resetBtn);
    bar->addStretch();
    bar->addWidget(applyBtn);
}

// ── 协议切换 ────────────────────────────────────────────────

void ProtocolConfigPanel::onProtocolChanged(int index)
{
    Q_UNUSED(index);
    QString name = m_protocolCombo->currentData().toString();

    // csv 与其他协议走相同路径：若 protocols/<name>.json 不存在则
    // loadProtocolDef 返回空对象，refreshProtocolInfo 自然显示占位文案
    m_previewProtoDef = ConfigLoader::loadProtocolDef(name);
    refreshProtocolInfo(m_previewProtoDef);

    QStringList fields;
    QJsonArray orderArr = m_previewProtoDef.value("snapshot_order").toArray();
    for (const QJsonValue& v : orderArr)
        fields.append(v.toString());

    refreshFieldCombos(fields);
    autoMatchFields(fields);
}

void ProtocolConfigPanel::clearVarsLayout()
{
    // 注意：QLayout 本身就继承自 QLayoutItem，所以对 layout 行
    // takeAt 返回的指针 == item->layout()。不能再额外 delete item，
    // 否则会触发 double-free 导致崩溃。
    while (m_varsLayout->count() > 0) {
        QLayoutItem* item = m_varsLayout->takeAt(0);
        if (!item) break;

        if (QWidget* w = item->widget()) {
            w->deleteLater();
            delete item;                       // QWidgetItem 包装器
        } else if (QLayout* sub = item->layout()) {
            // 递归清理一层（spinWrap 内部只有 stretch + 单个 spin）
            while (sub->count() > 0) {
                QLayoutItem* ci = sub->takeAt(0);
                if (!ci) break;
                if (QWidget* cw = ci->widget())
                    cw->deleteLater();
                delete ci;                      // 安全：spacer / 包装器
            }
            delete sub;                         // item 与 sub 同一指针，仅删一次
        } else {
            delete item;                        // spacer 等其他类型
        }
    }
    m_varSpins.clear();
}

void ProtocolConfigPanel::refreshProtocolInfo(const QJsonObject& protoDef)
{
    clearVarsLayout();

    if (protoDef.isEmpty()) {
        m_descLabel->setText("(未找到协议定义文件)");
        m_fieldListLabel->setText("—");
        // 默认隐藏文本参数与量程区
        if (m_varsTitleLabel)  m_varsTitleLabel->setVisible(false);
        if (m_textTitleLabel)  m_textTitleLabel->setVisible(false);
        if (m_textParamsLabel) m_textParamsLabel->setVisible(false);
        return;
    }

    m_descLabel->setText(protoDef.value("description").toString());

    QJsonArray orderArr = protoDef.value("snapshot_order").toArray();
    QStringList fieldNames;
    for (const QJsonValue& v : orderArr)
        fieldNames.append(v.toString());
    m_fieldListLabel->setText(fieldNames.join(", "));

    // 按 framing.type 切换「量程参数」与「文本参数」子区的显示
    const QString framingType = protoDef.value("framing").toObject()
                                        .value("type").toString();
    const bool isText = framingType.startsWith("text_");

    if (m_varsTitleLabel)  m_varsTitleLabel->setVisible(!isText);
    if (m_textTitleLabel)  m_textTitleLabel->setVisible(isText);
    if (m_textParamsLabel) m_textParamsLabel->setVisible(isText);

    if (isText) {
        // 填充只读展示（本次改动不支持 UI 写回，编辑请直接改 JSON 文件）
        const QJsonObject t = protoDef.value("text").toObject();
        QStringList lines;

        if (framingType == "text_csv") {
            const QJsonObject pfx = t.value("prefix").toObject();
            const QString pEnabled = pfx.value("enabled").toBool(false) ? "开启" : "关闭";
            lines << QString("分隔符: %1").arg(t.value("delimiter").toString(","));
            lines << QString("前缀: %1").arg(pEnabled);
            if (pfx.value("enabled").toBool(false)) {
                lines << QString("  · 分隔字符: %1").arg(pfx.value("delimiter").toString(":"));
                lines << QString("  · 最大长度: %1").arg(pfx.value("max_length").toInt(16));
            }
            lines << QString("解析错误策略: %1").arg(t.value("on_parse_error").toString("strip"));
            lines << QString("数字字符集: %1").arg(t.value("numeric_strip_charset").toString("0-9.+\\-eE"));
        } else if (framingType == "text_regex") {
            lines << QString("pattern: %1").arg(t.value("pattern").toString());
            const QJsonValue pg = t.value("prefix_group");
            if (pg.isString())       lines << QString("prefix_group: %1").arg(pg.toString());
            else if (pg.isDouble())  lines << QString("prefix_group: %1").arg(pg.toInt());
            const QJsonArray caps = t.value("captures").toArray();
            lines << QString("captures: %1 项").arg(caps.size());
            for (const QJsonValue& cv : caps) {
                const QJsonObject co = cv.toObject();
                const QJsonValue g = co.value("group");
                const QString gStr = g.isString() ? g.toString() : QString::number(g.toInt());
                lines << QString("  · %1 ← group %2 × %3")
                            .arg(co.value("field").toString())
                            .arg(gStr)
                            .arg(co.value("multiplier").toDouble(1.0));
            }
            lines << QString("解析错误策略: %1").arg(t.value("on_parse_error").toString("strip"));
        }

        m_textParamsLabel->setText(lines.join("\n"));
        return;  // 文本协议无 variables 表单需要填充
    }

    QJsonObject varsObj = protoDef.value("variables").toObject();
    SerialConfig sc = ConfigLoader::instance().getSerialConfig();

    for (auto it = varsObj.begin(); it != varsObj.end(); ++it) {
        auto* spin = new FocusSpinBox(this);
        spin->setRange(-1e9, 1e9);
        spin->setDecimals(2);
        spin->setFixedWidth(VAR_SPIN_WIDTH);
        spin->setFixedHeight(INPUT_HEIGHT);
        spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // 当预览协议与当前活跃协议一致时，优先从 serial 节读取覆盖值
        bool sameProto = (sc.protocol == m_protocolCombo->currentData().toString());
        if (sameProto && it.key() == "acc_fsr")
            spin->setValue(sc.accFsr);
        else if (sameProto && it.key() == "gyro_fsr")
            spin->setValue(sc.gyroFsr);
        else
            spin->setValue(it.value().toDouble());

        // 用 HBox 包装：左侧 stretch 把 spin 推到表单字段列的右端
        auto* spinWrap = new QHBoxLayout();
        spinWrap->setContentsMargins(0, 0, 0, 0);
        spinWrap->setSpacing(0);
        spinWrap->addStretch();
        spinWrap->addWidget(spin, 0, Qt::AlignVCenter);

        m_varsLayout->addRow(varDisplayLabel(it.key()), spinWrap);
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
            row.fieldCombo->addItem("(未绑定)", QString());
            for (const QString& f : fieldNames)
                row.fieldCombo->addItem(f, f);

            int idx = row.fieldCombo->findData(currentText);
            if (idx >= 0)
                row.fieldCombo->setCurrentIndex(idx);

            row.fieldCombo->blockSignals(false);
        }
    }
}

void ProtocolConfigPanel::autoMatchFields(const QStringList& fieldNames)
{
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
    m_protocolCombo->blockSignals(true);
    m_protocolCombo->clear();
    // 协议列表完全来自 protocols/*.json 扫描；csv.json / regex.json 也在其中
    QStringList protocols = ConfigLoader::availableProtocols();
    for (const QString& name : protocols) {
        m_protocolCombo->addItem(name, name);
    }

    SerialConfig sc = ConfigLoader::instance().getSerialConfig();
    int idx = m_protocolCombo->findData(sc.protocol);
    if (idx >= 0)
        m_protocolCombo->setCurrentIndex(idx);
    m_protocolCombo->blockSignals(false);

    onProtocolChanged(m_protocolCombo->currentIndex());
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

    static const QMap<QString, QString> defaultNames = {
        { PointPurpose::Accelerometer, "ACC" },
        { PointPurpose::Gyroscope,     "GYR" },
        { PointPurpose::Quaternion,    "QUAT" },
        { PointPurpose::MagneticField, "MAG" },
        { PointPurpose::Barometer,     "BARO" },
    };

    for (const auto& group : m_purposeGroups) {
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
