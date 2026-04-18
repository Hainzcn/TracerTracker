#include "ProtocolConfigPanel.h"
#include "Styles.h"
#include "WindowIcons.h"
#include "config/ConfigLoader.h"
#include <QJsonArray>
#include <QFrame>
#include <QHBoxLayout>
#include <QDebug>

// ============================================================
// ProtocolConfigPanel.cpp — 协议配置面板实现
// ============================================================

ProtocolConfigPanel::ProtocolConfigPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("configPanel");
    setFixedWidth(PANEL_WIDTH);
    // 显式 resize，避免 QWidget 默认 640×480 在首次显示前导致父级
    // 按错误宽度计算滑入/滑出位移
    resize(PANEL_WIDTH, parent ? parent->height() : 480);

    // 确保背景不透明渲染 + 阻止鼠标事件向父 Viewer3D 传递
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_NoMousePropagation, true);
    setAutoFillBackground(true);

    setStyleSheet(Styles::CONFIG_PANEL_STYLE());
    buildUI();
    loadFromConfig();
}

// ── 分级缩进常量 ─────────────────────────────────────────────
//  L1 段落标题（"协议定义"/"字段映射"）      : 0
//  L2 子标签（"协议" / "变量" / "ACC" ...）  : 10
//  L3 具体内容（字段列表、变量表单、映射表） : 22

static constexpr int INDENT_L2 = 10;
static constexpr int INDENT_L3 = 22;

// ── 辅助：创建带 configSep 对象名的分隔线 ────────────────────

static QFrame* makeSeparator(QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setObjectName("configSep");
    f->setFrameShape(QFrame::NoFrame);
    return f;
}

// ── 辅助：将一个 widget 包进带左缩进的 HBox ──────────────────

static QHBoxLayout* wrapIndent(QWidget* w, int leftIndent) {
    auto* h = new QHBoxLayout();
    h->setContentsMargins(leftIndent, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(w);
    return h;
}

// ── 辅助：把 var key 翻译为带单位的中文标签 ──────────────────

static QString varDisplayLabel(const QString& key) {
    if (key == "acc_fsr")  return "加速度计 (g)";
    if (key == "gyro_fsr") return "陀螺仪 (°/s)";
    return key;
}

// ── UI 构建 ──────────────────────────────────────────────────

void ProtocolConfigPanel::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // 标题栏
    auto* titleBar = new QHBoxLayout();
    titleBar->setContentsMargins(14, 10, 8, 10);
    titleBar->setSpacing(0);

    auto* titleLabel = new QLabel("协议配置", this);
    titleLabel->setObjectName("sectionTitle");
    titleBar->addWidget(titleLabel);
    titleBar->addStretch();

    // 复用 FramelessWindow 顶栏的 1px 像素叉号绘制，确保 UI 风格统一
    auto* closeBtn = new QPushButton(this);
    closeBtn->setObjectName("closeBtn");
    closeBtn->setFixedSize(24, 24);
    closeBtn->setIcon(WindowIcons::makeCloseIcon());
    closeBtn->setIconSize(QSize(16, 16));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &ProtocolConfigPanel::closeRequested);
    titleBar->addWidget(closeBtn);

    outerLayout->addLayout(titleBar);
    outerLayout->addWidget(makeSeparator(this));

    // 滚动区域
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* scrollContent = new QWidget();
    scrollContent->setObjectName("configScrollContent");
    scrollContent->setStyleSheet("QWidget#configScrollContent { background: transparent; }");
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(14, 14, 14, 14);
    scrollLayout->setSpacing(14);

    buildProtocolSection(scrollLayout);
    scrollLayout->addWidget(makeSeparator(scrollContent));
    buildMappingSection(scrollLayout);
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea, 1);

    outerLayout->addWidget(makeSeparator(this));
    buildActionBar(outerLayout);
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

    // L2：量程小标题
    auto* varsTitle = new QLabel("量程", this);
    varsTitle->setObjectName("subLabel");
    varsTitle->setContentsMargins(0, 4, 0, 0);
    container->addLayout(wrapIndent(varsTitle, INDENT_L2));

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

void ProtocolConfigPanel::buildActionBar(QVBoxLayout* container)
{
    auto* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(14, 10, 14, 10);
    actionLayout->setSpacing(8);

    auto* resetBtn = new QPushButton("重置", this);
    resetBtn->setObjectName("resetBtn");
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(resetBtn, &QPushButton::clicked, this, &ProtocolConfigPanel::onReset);

    auto* applyBtn = new QPushButton("应用", this);
    applyBtn->setObjectName("applyBtn");
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
        m_descLabel->setText("CSV 文本协议（无二进制帧定义）");
        m_fieldListLabel->setText("—");
        return;
    }

    m_descLabel->setText(protoDef.value("description").toString());

    QJsonArray orderArr = protoDef.value("snapshot_order").toArray();
    QStringList fieldNames;
    for (const QJsonValue& v : orderArr)
        fieldNames.append(v.toString());
    m_fieldListLabel->setText(fieldNames.join(", "));

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
    m_protocolCombo->addItem("csv (文本)", "csv");
    QStringList protocols = ConfigLoader::availableProtocols();
    for (const QString& name : protocols)
        m_protocolCombo->addItem(name, name);

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
