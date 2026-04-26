#include "ui/panels/SettingsPanel.h"
#include "ui/common/Styles.h"
#include "config/ConfigLoader.h"
#include "config/ConfigTypes.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QDebug>

#ifdef HAVE_QT_SERIAL_PORT
#  include <QSerialPortInfo>
#endif

// ============================================================
// SettingsPanel.cpp — 通用设置面板实现
//
// 标题 / 关闭按钮 / 滚动区 / 动作栏 / QSS 由 SidePanel 基类提供，
// 本文件只关心 5 个段的控件构建与 ConfigLoader 之间的双向同步。
// ============================================================

// ── 公用枚举（与 QSerialPort 枚举值一一对应，写盘存原始整数避免
//             本地化漂移）──────────────────────────────────────

namespace {

const QList<SettingsPanel::ComboItem>& baudrateItems() {
    static const QList<SettingsPanel::ComboItem> items = {
        {"9600",   9600  }, {"19200",  19200 }, {"38400",  38400 },
        {"57600",  57600 }, {"115200", 115200}, {"230400", 230400},
        {"460800", 460800}, {"921600", 921600},
    };
    return items;
}

const QList<SettingsPanel::ComboItem>& dataBitsItems() {
    static const QList<SettingsPanel::ComboItem> items = {
        {"5", 5}, {"6", 6}, {"7", 7}, {"8", 8},
    };
    return items;
}

const QList<SettingsPanel::ComboItem>& parityItems() {
    // QSerialPort::Parity: NoParity=0 EvenParity=2 OddParity=3 MarkParity=4 SpaceParity=5
    static const QList<SettingsPanel::ComboItem> items = {
        {"None (\u65e0)",  0},
        {"Even (\u5076)",  2},
        {"Odd (\u5947)",   3},
        {"Mark",            4},
        {"Space",           5},
    };
    return items;
}

const QList<SettingsPanel::ComboItem>& stopBitsItems() {
    // QSerialPort::StopBits: OneStop=1 TwoStop=2 OneAndHalfStop=3
    static const QList<SettingsPanel::ComboItem> items = {
        {"1",   1},
        {"1.5", 3},
        {"2",   2},
    };
    return items;
}

const QList<SettingsPanel::ComboItem>& flowControlItems() {
    // QSerialPort::FlowControl: NoFlowControl=0 HardwareControl=1 SoftwareControl=2
    static const QList<SettingsPanel::ComboItem> items = {
        {"None (\u65e0)",            0},
        {"Hardware (\u786c\u4ef6)",  1},
        {"Software (Xon-Xoff)",      2},
    };
    return items;
}

// 内嵌 QLineEdit 的极简样式，匹配面板深色主题（QSS 全局没有专门为
// 面板内部 QLineEdit 设的规则，这里就近兜底）
QString lineEditStyle() {
    return R"(
QLineEdit {
    color: #e0e0e0; font-size: 12px; font-family: 'Microsoft YaHei', sans-serif;
    background: #2b2d30; border: 1px solid #4d4d4d; border-radius: 6px;
    padding: 1px 8px; min-height: 18px; max-height: 20px;
}
QLineEdit:hover  { border-color: #666666; background: #333538; }
QLineEdit:focus  { border-color: #0e639c; background: #333538; }
QLineEdit:disabled { color: #666666; border-color: #333333; background: #2a2a2a; }
)";
}

// 把任一控件包到「左 stretch + 控件靠右」的 HBox 里，让其在 form 字段
// 列右贴齐（与 ProtocolConfigPanel::refreshProtocolInfo 中 spinWrap 同形）
QHBoxLayout* rightAlignWrap(QWidget* w) {
    auto* h = new QHBoxLayout();
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addStretch();
    h->addWidget(w, 0, Qt::AlignVCenter);
    return h;
}

} // namespace

// ── 构造 ────────────────────────────────────────────────────

SettingsPanel::SettingsPanel(QWidget* parent)
    : SidePanel("\u8bbe\u7f6e", PANEL_WIDTH, parent)  // "设置"
{
    buildContent();
    buildActions();
    loadFromConfig();
}

// ── UI 构建 ──────────────────────────────────────────────────

void SettingsPanel::buildContent()
{
    auto* layout = contentLayout();

    buildGravitySection(layout);
    layout->addWidget(makeSeparator(layout->parentWidget()));

    buildSerialSection(layout);
    layout->addWidget(makeSeparator(layout->parentWidget()));

    buildUdpSection(layout);
    layout->addWidget(makeSeparator(layout->parentWidget()));

    buildInsSection(layout);
    layout->addWidget(makeSeparator(layout->parentWidget()));

    buildRenderSection(layout);
    layout->addStretch();
}

// ── 1. 重力 ─────────────────────────────────────────────────

void SettingsPanel::buildGravitySection(QVBoxLayout* container)
{
    addSectionTitle(container, "\u91cd\u529b");  // "重力"

    auto* form = addFormBlock(container, INDENT_L2);
    addDoubleRow(form, "gravity_reference", "\u91cd\u529b\u53c2\u8003",  // "重力参考"
                 0.0, 30.0, 4, 0.01, " m/s\u00b2");
}

// ── 2. 串口 ─────────────────────────────────────────────────

void SettingsPanel::buildSerialSection(QVBoxLayout* container)
{
    addSectionTitle(container, "\u4e32\u53e3");  // "串口"

    addCheckBox(container, "serial.enabled",
                "\u542f\u7528\u4e32\u53e3\u63a5\u6536", INDENT_L2);  // "启用串口接收"

    auto* form = addFormBlock(container, INDENT_L2);

    auto* portCombo = addComboRow(form, "serial.port", "\u7aef\u53e3", {}, true);  // "端口"
    Q_UNUSED(portCombo);
    populateSerialPortCombo(QString());

    addComboRow(form, "serial.baudrate",
                "\u6ce2\u7279\u7387", baudrateItems(), true);  // "波特率"

    addComboRow(form, "serial.data_bits",
                "\u6570\u636e\u4f4d", dataBitsItems());  // "数据位"

    addComboRow(form, "serial.parity",
                "\u6821\u9a8c\u4f4d", parityItems());  // "校验位"

    addComboRow(form, "serial.stop_bits",
                "\u505c\u6b62\u4f4d", stopBitsItems());  // "停止位"

    addComboRow(form, "serial.flow_control",
                "\u6d41\u63a7", flowControlItems());  // "流控"

    addIntRow(form, "serial.timeout",
              "\u8d85\u65f6", 0, 60, " s");  // "超时"
}

// ── 3. UDP ──────────────────────────────────────────────────

void SettingsPanel::buildUdpSection(QVBoxLayout* container)
{
    addSectionTitle(container, "UDP");

    addCheckBox(container, "udp.enabled",
                "\u542f\u7528 UDP \u63a5\u6536", INDENT_L2);  // "启用 UDP 接收"

    auto* form = addFormBlock(container, INDENT_L2);
    addLineEditRow(form, "udp.ip", "\u7ed1\u5b9a IP");        // "绑定 IP"
    addIntRow    (form, "udp.port", "\u7aef\u53e3", 1, 65535);  // "端口"
}

// ── 4. INS ──────────────────────────────────────────────────

void SettingsPanel::buildInsSection(QVBoxLayout* container)
{
    addSectionTitle(container, "INS \u6ee4\u6ce2");  // "INS 滤波"

    // 4.1 标量
    auto* scalarForm = addFormBlock(container, INDENT_L2);
    addDoubleRow(scalarForm, "ins.baro_lpf_alpha",
                 "\u6c14\u538b\u4f4e\u901a \u03b1",  // "气压低通 α"
                 0.0, 1.0, 4, 0.01);
    addDoubleRow(scalarForm, "ins.filter_yaw_offset_deg",
                 "\u504f\u822a\u504f\u79fb",  // "偏航偏移"
                 -360.0, 360.0, 2, 0.5, " \u00b0");

    // 4.2 Kalman
    addSubTitle(container, "Kalman");
    addCheckBox(container, "ins.kalman.enabled",
                "\u542f\u7528 Kalman", INDENT_L3);  // "启用 Kalman"
    auto* kFm = addFormBlock(container, INDENT_L3);
    addDoubleRow(kFm, "ins.kalman.process_noise_sigma",
                 "\u8fc7\u7a0b\u566a\u58f0 \u03c3",  // "过程噪声 σ"
                 0.0, 100.0, 4, 0.01);
    addDoubleRow(kFm, "ins.kalman.measurement_noise_R",
                 "\u6d4b\u91cf\u566a\u58f0 R",  // "测量噪声 R"
                 0.0, 100.0, 4, 0.01);

    // 4.3 ZUPT
    addSubTitle(container, "ZUPT");
    addCheckBox(container, "ins.zupt.enabled",
                "\u542f\u7528 ZUPT", INDENT_L3);  // "启用 ZUPT"
    auto* zFm = addFormBlock(container, INDENT_L3);
    addDoubleRow(zFm, "ins.zupt.acc_variance_threshold",
                 "\u52a0\u901f\u5ea6\u9608\u503c",  // "加速度阈值"
                 0.0, 10.0, 4, 0.01);
    addDoubleRow(zFm, "ins.zupt.gyro_variance_threshold",
                 "\u9640\u87ba\u4eea\u9608\u503c",  // "陀螺仪阈值"
                 0.0, 10.0, 4, 0.01);
    addIntRow   (zFm, "ins.zupt.window_size",
                 "\u7a97\u53e3\u5e27\u6570",  // "窗口帧数"
                 1, 1000);

    // 4.4 Madgwick
    addSubTitle(container, "Madgwick");
    auto* mFm = addFormBlock(container, INDENT_L3);
    addDoubleRow(mFm, "ins.madgwick.beta", "\u03b2", 0.0, 10.0, 4, 0.01);

    // 4.5 Mahony
    addSubTitle(container, "Mahony");
    auto* mhFm = addFormBlock(container, INDENT_L3);
    addDoubleRow(mhFm, "ins.mahony.kp", "Kp", 0.0, 100.0, 4, 0.01);
    addDoubleRow(mhFm, "ins.mahony.ki", "Ki", 0.0, 100.0, 4, 0.001);
}

// ── 5. 渲染调试 ─────────────────────────────────────────────

void SettingsPanel::buildRenderSection(QVBoxLayout* container)
{
    addSectionTitle(container, "\u6e32\u67d3\u8c03\u8bd5");  // "渲染调试"
    addCheckBox(container, "render_debug.enabled",
                "\u542f\u7528\u8c03\u8bd5\u8f93\u51fa",  // "启用调试输出"
                INDENT_L2);
    addCheckBox(container, "render_debug.verbose_point_updates",
                "\u8be6\u7ec6\u70b9\u66f4\u65b0\u65e5\u5fd7",  // "详细点更新日志"
                INDENT_L2);
}

// ── 动作栏：重置 / 确定 / 应用 ──────────────────────────────

void SettingsPanel::buildActions()
{
    auto* bar = actionLayout();

    auto* resetBtn = new QPushButton("\u91cd\u7f6e", this);  // "重置"
    resetBtn->setObjectName("resetBtn");
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsPanel::onReset);

    auto* applyBtn = new QPushButton("\u5e94\u7528", this);  // "应用"
    applyBtn->setObjectName("applyBtn");
    applyBtn->setCursor(Qt::PointingHandCursor);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsPanel::onApply);

    auto* okBtn = new QPushButton("\u786e\u5b9a", this);  // "确定"
    okBtn->setObjectName("okBtn");
    okBtn->setCursor(Qt::PointingHandCursor);
    connect(okBtn, &QPushButton::clicked, this, &SettingsPanel::onConfirm);

    // 摆位顺序：重置 …… 确定 应用
    // 「应用」放在最右作为绿色主按钮（与协议配置面板的 applyBtn 位置一致），
    // 「确定」紧靠其左作为次操作；视觉重心 / 视线终点都落在 applyBtn 上。
    bar->addWidget(resetBtn);
    bar->addStretch();
    bar->addWidget(okBtn);
    bar->addWidget(applyBtn);
}

// ── 控件构造助手 ────────────────────────────────────────────

void SettingsPanel::addSectionTitle(QVBoxLayout* container, const QString& text)
{
    auto* lbl = new QLabel(text, this);
    lbl->setObjectName("sectionTitle");
    container->addWidget(lbl);
}

void SettingsPanel::addSubTitle(QVBoxLayout* container, const QString& text)
{
    auto* lbl = new QLabel(text, this);
    lbl->setObjectName("subLabel");
    lbl->setContentsMargins(0, 4, 0, 0);
    container->addLayout(wrapIndent(lbl, INDENT_L2));
}

QFormLayout* SettingsPanel::addFormBlock(QVBoxLayout* container, int leftIndent)
{
    auto* form = new QFormLayout();
    form->setContentsMargins(leftIndent, 0, 0, 0);
    form->setSpacing(6);
    form->setHorizontalSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    // 字段列允许扩展，配合 rightAlignWrap 把控件推到右侧
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    container->addLayout(form);
    return form;
}

QCheckBox* SettingsPanel::addCheckBox(QVBoxLayout* container, const QString& key,
                                       const QString& text, int leftIndent)
{
    auto* cb = new QCheckBox(text, this);
    cb->setStyleSheet(Styles::STYLE_CHECKBOX());
    container->addLayout(wrapIndent(cb, leftIndent));
    m_checks[key] = cb;
    return cb;
}

FocusSpinBox* SettingsPanel::addDoubleRow(QFormLayout* form, const QString& key,
                                           const QString& label,
                                           double minV, double maxV,
                                           int decimals, double step,
                                           const QString& suffix)
{
    auto* spin = new FocusSpinBox(this);
    spin->setRange(minV, maxV);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    if (!suffix.isEmpty()) spin->setSuffix(suffix);
    spin->setFixedWidth(VALUE_COL_WIDTH);
    spin->setFixedHeight(INPUT_HEIGHT);
    spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    form->addRow(label, rightAlignWrap(spin));
    m_doubles[key] = spin;
    return spin;
}

QSpinBox* SettingsPanel::addIntRow(QFormLayout* form, const QString& key,
                                     const QString& label, int minV, int maxV,
                                     const QString& suffix)
{
    auto* spin = new QSpinBox(this);
    spin->setRange(minV, maxV);
    if (!suffix.isEmpty()) spin->setSuffix(suffix);
    spin->setFixedWidth(VALUE_COL_WIDTH);
    spin->setFixedHeight(INPUT_HEIGHT);
    spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    spin->setFocusPolicy(Qt::StrongFocus);

    form->addRow(label, rightAlignWrap(spin));
    m_ints[key] = spin;
    return spin;
}

FocusComboBox* SettingsPanel::addComboRow(QFormLayout* form, const QString& key,
                                           const QString& label,
                                           const QList<ComboItem>& items,
                                           bool editable)
{
    auto* combo = new FocusComboBox(this);
    combo->setEditable(editable);
    combo->setFixedWidth(VALUE_COL_WIDTH);
    combo->setFixedHeight(INPUT_HEIGHT);
    combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    for (const auto& it : items)
        combo->addItem(it.label, it.value);

    form->addRow(label, rightAlignWrap(combo));
    m_combos[key] = combo;
    return combo;
}

QLineEdit* SettingsPanel::addLineEditRow(QFormLayout* form, const QString& key,
                                          const QString& label)
{
    auto* edit = new QLineEdit(this);
    edit->setStyleSheet(lineEditStyle());
    edit->setFixedWidth(VALUE_COL_WIDTH);
    edit->setFixedHeight(INPUT_HEIGHT);
    edit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    edit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    form->addRow(label, rightAlignWrap(edit));
    m_lineEdits[key] = edit;
    return edit;
}

void SettingsPanel::populateSerialPortCombo(const QString& currentPort)
{
    auto* combo = m_combos.value("serial.port", nullptr);
    if (!combo) return;

    combo->blockSignals(true);
    QString prevText = combo->currentText();
    combo->clear();

#ifdef HAVE_QT_SERIAL_PORT
    QStringList seen;
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        const QString name = info.portName();
        const QString desc = info.description();
        const QString label = (!desc.isEmpty() && desc != "n/a")
                              ? QString("%1  %2").arg(name, desc)
                              : name;
        combo->addItem(label, name);
        seen << name;
    }

    // 把当前已配置但当前未连接的端口也保留下来（避免下次启动找不到）
    auto ensurePresent = [&](const QString& p) {
        if (p.isEmpty() || seen.contains(p)) return;
        combo->addItem(p, p);
        seen << p;
    };
    ensurePresent(currentPort);
    ensurePresent(prevText);
#else
    combo->addItem("(SerialPort \u672a\u5b89\u88c5)", QString());
    combo->setEnabled(false);
#endif

    combo->blockSignals(false);
}

// ── 配置 ⇄ UI 同步 ──────────────────────────────────────────

void SettingsPanel::loadFromConfig()
{
    auto& cfg = ConfigLoader::instance();

    // 重力
    if (auto* sp = m_doubles.value("gravity_reference"))
        sp->setValue(cfg.gravityReference());

    // 串口
    SerialConfig sCfg = cfg.getSerialConfig();
    if (auto* cb = m_checks.value("serial.enabled"))      cb->setChecked(sCfg.enabled);
    if (auto* sp = m_ints  .value("serial.timeout"))      sp->setValue(sCfg.timeout);

    populateSerialPortCombo(sCfg.port);

    auto pickComboByValue = [this](const QString& key, int value) {
        auto* c = m_combos.value(key); if (!c) return;
        int idx = c->findData(value);
        if (idx >= 0) {
            c->setCurrentIndex(idx);
        } else if (c->isEditable()) {
            c->setEditText(QString::number(value));
        }
    };
    auto pickComboByText = [this](const QString& key, const QString& text) {
        auto* c = m_combos.value(key); if (!c) return;
        int idx = c->findData(text);
        if (idx >= 0) c->setCurrentIndex(idx);
        else if (c->isEditable()) c->setEditText(text);
    };

    pickComboByText (   "serial.port",         sCfg.port);
    pickComboByValue(   "serial.baudrate",     sCfg.baudrate);
    pickComboByValue(   "serial.data_bits",    sCfg.dataBits);
    pickComboByValue(   "serial.parity",       sCfg.parity);
    pickComboByValue(   "serial.stop_bits",    sCfg.stopBits);
    pickComboByValue(   "serial.flow_control", sCfg.flowControl);

    // UDP
    UdpConfig uCfg = cfg.getUdpConfig();
    if (auto* cb = m_checks   .value("udp.enabled")) cb->setChecked(uCfg.enabled);
    if (auto* le = m_lineEdits.value("udp.ip"))      le->setText(uCfg.ip);
    if (auto* sp = m_ints     .value("udp.port"))    sp->setValue(uCfg.port);

    // INS
    InsConfig iCfg = cfg.getInsConfig();
    if (auto* sp = m_doubles.value("ins.baro_lpf_alpha"))         sp->setValue(iCfg.baroLpfAlpha);
    if (auto* sp = m_doubles.value("ins.filter_yaw_offset_deg"))  sp->setValue(iCfg.filterYawOffsetDeg);

    if (auto* cb = m_checks .value("ins.kalman.enabled"))             cb->setChecked(iCfg.kalman.enabled);
    if (auto* sp = m_doubles.value("ins.kalman.process_noise_sigma")) sp->setValue(iCfg.kalman.processNoiseSigma);
    if (auto* sp = m_doubles.value("ins.kalman.measurement_noise_R")) sp->setValue(iCfg.kalman.measurementNoiseR);

    if (auto* cb = m_checks .value("ins.zupt.enabled"))                cb->setChecked(iCfg.zupt.enabled);
    if (auto* sp = m_doubles.value("ins.zupt.acc_variance_threshold")) sp->setValue(iCfg.zupt.accVarianceThreshold);
    if (auto* sp = m_doubles.value("ins.zupt.gyro_variance_threshold"))sp->setValue(iCfg.zupt.gyroVarianceThreshold);
    if (auto* sp = m_ints   .value("ins.zupt.window_size"))            sp->setValue(iCfg.zupt.windowSize);

    if (auto* sp = m_doubles.value("ins.madgwick.beta")) sp->setValue(iCfg.madgwick.beta);
    if (auto* sp = m_doubles.value("ins.mahony.kp"))     sp->setValue(iCfg.mahony.kp);
    if (auto* sp = m_doubles.value("ins.mahony.ki"))     sp->setValue(iCfg.mahony.ki);

    // 渲染调试
    RenderDebugConfig rCfg = cfg.getRenderDebugConfig();
    if (auto* cb = m_checks.value("render_debug.enabled"))               cb->setChecked(rCfg.enabled);
    if (auto* cb = m_checks.value("render_debug.verbose_point_updates")) cb->setChecked(rCfg.verbosePointUpdates);
}

void SettingsPanel::writeBackAndPersist()
{
    auto comboInt = [this](const QString& key, int fallback) -> int {
        auto* c = m_combos.value(key);
        if (!c) return fallback;
        QVariant v = c->currentData();
        if (v.isValid()) return v.toInt();
        // 可编辑下拉框：尝试把当前文本解析为整数
        bool ok = false;
        int parsed = c->currentText().toInt(&ok);
        return ok ? parsed : fallback;
    };
    auto comboText = [this](const QString& key, const QString& fallback) -> QString {
        auto* c = m_combos.value(key);
        if (!c) return fallback;
        QVariant v = c->currentData();
        if (v.isValid() && !v.toString().isEmpty()) return v.toString();
        QString t = c->currentText().trimmed();
        return t.isEmpty() ? fallback : t;
    };

    // 串口
    SerialConfig sCfg = ConfigLoader::instance().getSerialConfig();  // 保留 protocol/accFsr/gyroFsr
    if (auto* cb = m_checks.value("serial.enabled"))      sCfg.enabled  = cb->isChecked();
    if (auto* sp = m_ints  .value("serial.timeout"))      sCfg.timeout  = sp->value();
    sCfg.port        = comboText("serial.port",         sCfg.port);
    sCfg.baudrate    = comboInt ("serial.baudrate",     sCfg.baudrate);
    sCfg.dataBits    = comboInt ("serial.data_bits",    sCfg.dataBits);
    sCfg.parity      = comboInt ("serial.parity",       sCfg.parity);
    sCfg.stopBits    = comboInt ("serial.stop_bits",    sCfg.stopBits);
    sCfg.flowControl = comboInt ("serial.flow_control", sCfg.flowControl);

    // UDP
    UdpConfig uCfg;
    if (auto* cb = m_checks   .value("udp.enabled")) uCfg.enabled = cb->isChecked();
    if (auto* le = m_lineEdits.value("udp.ip"))      uCfg.ip      = le->text().trimmed();
    if (auto* sp = m_ints     .value("udp.port"))    uCfg.port    = sp->value();

    // INS
    InsConfig iCfg;
    if (auto* sp = m_doubles.value("ins.baro_lpf_alpha"))         iCfg.baroLpfAlpha       = sp->value();
    if (auto* sp = m_doubles.value("ins.filter_yaw_offset_deg"))  iCfg.filterYawOffsetDeg = sp->value();

    if (auto* cb = m_checks .value("ins.kalman.enabled"))             iCfg.kalman.enabled            = cb->isChecked();
    if (auto* sp = m_doubles.value("ins.kalman.process_noise_sigma")) iCfg.kalman.processNoiseSigma  = sp->value();
    if (auto* sp = m_doubles.value("ins.kalman.measurement_noise_R")) iCfg.kalman.measurementNoiseR  = sp->value();

    if (auto* cb = m_checks .value("ins.zupt.enabled"))                iCfg.zupt.enabled              = cb->isChecked();
    if (auto* sp = m_doubles.value("ins.zupt.acc_variance_threshold")) iCfg.zupt.accVarianceThreshold = sp->value();
    if (auto* sp = m_doubles.value("ins.zupt.gyro_variance_threshold"))iCfg.zupt.gyroVarianceThreshold= sp->value();
    if (auto* sp = m_ints   .value("ins.zupt.window_size"))            iCfg.zupt.windowSize           = sp->value();

    if (auto* sp = m_doubles.value("ins.madgwick.beta")) iCfg.madgwick.beta = sp->value();
    if (auto* sp = m_doubles.value("ins.mahony.kp"))     iCfg.mahony.kp     = sp->value();
    if (auto* sp = m_doubles.value("ins.mahony.ki"))     iCfg.mahony.ki     = sp->value();

    // 渲染调试
    RenderDebugConfig rCfg;
    if (auto* cb = m_checks.value("render_debug.enabled"))               rCfg.enabled             = cb->isChecked();
    if (auto* cb = m_checks.value("render_debug.verbose_point_updates")) rCfg.verbosePointUpdates = cb->isChecked();

    // 重力
    double gravity = 9.80;
    if (auto* sp = m_doubles.value("gravity_reference")) gravity = sp->value();

    ConfigLoader::instance().updateGeneralSettings(sCfg, uCfg, iCfg, rCfg, gravity);
}

// ── 槽 ──────────────────────────────────────────────────────

void SettingsPanel::onReset()
{
    loadFromConfig();
}

void SettingsPanel::onConfirm()
{
    writeBackAndPersist();
    qDebug() << "SettingsPanel: \u8bbe\u7f6e\u5df2\u4fdd\u5b58\uff08\u4e0b\u6b21\u542f\u52a8\u751f\u6548\uff09";
    emit confirmed();
}

void SettingsPanel::onApply()
{
    writeBackAndPersist();
    qDebug() << "SettingsPanel: \u8bbe\u7f6e\u5df2\u5e94\u7528\uff08\u91cd\u542f\u63a5\u6536\u4e2d\uff09";
    emit applied();
}
