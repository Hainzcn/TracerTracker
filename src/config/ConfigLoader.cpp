#include "ConfigLoader.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <QDir>
#include <QSet>
#include <QVariantMap>

// ============================================================
// ConfigLoader.cpp — 配置加载器实现
// ============================================================

// ── 单例 ────────────────────────────────────────────────────

// 返回全局唯一实例（静态局部变量，首次调用时初始化）
ConfigLoader& ConfigLoader::instance() {
    static ConfigLoader inst;
    return inst;
}

// 构造函数：读取 config.json，与默认值合并
ConfigLoader::ConfigLoader() {
    reload();
}

// ── 文件路径 ─────────────────────────────────────────────────

// 查找 config.json 的路径（与可执行文件同目录）
QString ConfigLoader::configFilePath() {
    // 优先在应用程序所在目录查找
    QString appDir = QCoreApplication::applicationDirPath();
    QString path   = appDir + "/config.json";
    if (QFile::exists(path)) {
        return path;
    }
    // 开发模式：从工作目录向上查找
    QDir dir = QDir::current();
    for (int i = 0; i < 4; ++i) {
        QString candidate = dir.absoluteFilePath("config.json");
        if (QFile::exists(candidate)) {
            return candidate;
        }
        if (!dir.cdUp()) break;
    }
    // 找不到时返回 appDir 下的路径（将创建新文件）
    return path;
}

// 查找 protocols/<name>.json 的路径
QString ConfigLoader::protocolFilePath(const QString& protocolName)
{
    // 如果是绝对路径或带目录分隔符，直接当文件路径使用
    if (protocolName.contains('/') || protocolName.contains('\\')) {
        if (QFile::exists(protocolName)) return protocolName;
    }

    QString fileName = protocolName;
    if (!fileName.endsWith(".json")) fileName += ".json";

    // 在应用程序目录的 protocols/ 下查找
    QString appDir = QCoreApplication::applicationDirPath();
    QString path = appDir + "/protocols/" + fileName;
    if (QFile::exists(path)) return path;

    // 开发模式：从工作目录向上查找
    QDir dir = QDir::current();
    for (int i = 0; i < 4; ++i) {
        QString candidate = dir.absoluteFilePath("protocols/" + fileName);
        if (QFile::exists(candidate)) return candidate;
        if (!dir.cdUp()) break;
    }

    return QString();
}

// ── 默认 JSON 构造 ────────────────────────────────────────────

// 构建包含所有默认值的 JSON 对象（与 Python DEFAULT_CONFIG 对应）
QJsonObject ConfigLoader::buildDefaultJson() {
    QJsonObject root;
    root["gravity_reference"] = 9.80;

    // UDP 默认配置
    QJsonObject udp;
    udp["enabled"] = true;
    udp["ip"]      = "127.0.0.1";
    udp["port"]    = 8888;
    root["udp"]    = udp;

    // 串口默认配置
    QJsonObject serial;
    serial["enabled"]  = true;
    serial["port"]     = "COM5";
    serial["baudrate"] = 115200;
    serial["timeout"]  = 1;
    serial["protocol"] = "atkms901m";
    serial["acc_fsr"]  = 4;
    serial["gyro_fsr"] = 2000;
    root["serial"]     = serial;

    // 渲染调试默认配置
    QJsonObject rd;
    rd["enabled"]               = false;
    rd["verbose_point_updates"] = false;
    root["render_debug"]        = rd;

    // INS 默认配置
    QJsonObject ins;
    QJsonObject kalman;
    kalman["enabled"]              = true;
    kalman["process_noise_sigma"]  = 0.5;
    kalman["measurement_noise_R"]  = 0.5;
    ins["kalman"] = kalman;

    QJsonObject zupt;
    zupt["enabled"]                = true;
    zupt["acc_variance_threshold"] = 0.5;
    zupt["gyro_variance_threshold"]= 0.1;
    zupt["window_size"]            = 40;
    ins["zupt"] = zupt;

    ins["baro_lpf_alpha"]        = 0.1;

    QJsonObject madgwick;
    madgwick["beta"] = 0.05;
    ins["madgwick"]  = madgwick;

    QJsonObject mahony;
    mahony["kp"] = 1.0;
    mahony["ki"] = 0.0;
    ins["mahony"] = mahony;

    ins["filter_yaw_offset_deg"] = 90.0;
    root["ins"] = ins;

    // 数据点默认配置列表（传感器 points 使用 field 名引用）
    auto makeField = [](const QString& field, double mult = 1.0) -> QJsonObject {
        QJsonObject a;
        a["field"]      = field;
        a["multiplier"] = mult;
        return a;
    };
    auto makeAxis = [](int idx, double mult) -> QJsonObject {
        QJsonObject a;
        a["index"]      = idx;
        a["multiplier"] = mult;
        return a;
    };

    QJsonArray points;
    {
        QJsonObject p;
        p["name"] = "ACC"; p["source"] = "any"; p["purpose"] = "accelerometer";
        p["x"] = makeField("ax"); p["y"] = makeField("ay"); p["z"] = makeField("az");
        points.append(p);
    }
    {
        QJsonObject p;
        p["name"] = "GYR"; p["source"] = "any"; p["purpose"] = "gyroscope";
        p["x"] = makeField("gx"); p["y"] = makeField("gy"); p["z"] = makeField("gz");
        points.append(p);
    }
    {
        QJsonObject p;
        p["name"] = "QUAT"; p["source"] = "any"; p["purpose"] = "quaternion";
        p["w"] = makeField("q0"); p["x"] = makeField("q1");
        p["y"] = makeField("q2"); p["z"] = makeField("q3");
        points.append(p);
    }
    {
        QJsonObject p;
        p["name"] = "MAG"; p["source"] = "any"; p["purpose"] = "magnetic_field";
        p["x"] = makeField("mx"); p["y"] = makeField("my"); p["z"] = makeField("mz");
        points.append(p);
    }
    {
        QJsonObject p;
        p["name"] = "BARO"; p["source"] = "any"; p["purpose"] = "barometer";
        p["altitude"] = makeField("altitude"); p["pressure"] = makeField("pressure");
        points.append(p);
    }
    root["points"] = points;

    return root;
}

// ── 深度合并 ──────────────────────────────────────────────────

// 将 loaded（文件值）与 defaults（默认值）深度合并
// 规则：文件中存在的字段优先；缺失字段取默认值
QJsonObject ConfigLoader::mergeWithDefaults(const QJsonObject& loaded,
                                             const QJsonObject& defaults) {
    QJsonObject result = defaults;
    for (auto it = loaded.begin(); it != loaded.end(); ++it) {
        const QString& key = it.key();
        QJsonValue loadedVal = it.value();
        if (defaults.contains(key)) {
            QJsonValue defaultVal = defaults[key];
            if (defaultVal.isObject() && loadedVal.isObject()) {
                // 递归合并嵌套对象
                result[key] = mergeWithDefaults(loadedVal.toObject(),
                                                defaultVal.toObject());
            } else {
                // 非对象类型：文件值优先
                result[key] = loadedVal;
            }
        } else {
            // 默认中不存在的额外字段原样保留
            result[key] = loadedVal;
        }
    }
    return result;
}

// ── 加载与保存 ─────────────────────────────────────────────────

// 从磁盘重新加载配置文件
void ConfigLoader::reload() {
    QString path = configFilePath();
    QJsonObject defaults = buildDefaultJson();

    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            m_config = mergeWithDefaults(doc.object(), defaults);
            loadProtocol();
            rebuildPointsCache();
            qDebug() << "ConfigLoader: 已从" << path << "加载配置";
            if (m_config != doc.object()) {
                save();
            }
            return;
        } else {
            qWarning() << "ConfigLoader: JSON 解析失败:" << err.errorString();
        }
    } else {
        qDebug() << "ConfigLoader: 配置文件不存在，使用默认配置并创建文件";
    }

    m_config = defaults;
    loadProtocol();
    rebuildPointsCache();
    save();
}

// 将当前配置写回 config.json
void ConfigLoader::save() const {
    QString path = configFilePath();
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonDocument doc(m_config);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "ConfigLoader: 配置已保存到" << path;
    } else {
        qWarning() << "ConfigLoader: 无法写入配置文件" << path;
    }
}

// ── 辅助解析 ──────────────────────────────────────────────────

// 从 JSON 对象解析轴映射（index / field + multiplier）
AxisMapping ConfigLoader::parseAxisMapping(const QJsonObject& obj,
                                            int defaultIndex,
                                            double defaultMult) {
    AxisMapping m;
    m.index      = obj.value("index").toInt(defaultIndex);
    m.multiplier = obj.value("multiplier").toDouble(defaultMult);
    m.field      = obj.value("field").toString();
    return m;
}

// 从 JSON 对象解析单个数据点配置
PointConfig ConfigLoader::parsePointConfig(const QJsonObject& obj) {
    PointConfig p;
    p.name    = obj.value("name").toString();
    p.source  = obj.value("source").toString("any");
    p.purpose = obj.value("purpose").toString();

    // 解析可选前缀（null 或空字符串均视为无前缀）
    QJsonValue prefixVal = obj.value("prefix");
    if (!prefixVal.isNull() && prefixVal.isString() && !prefixVal.toString().isEmpty()) {
        p.prefix = prefixVal.toString();
    }

    // XYZ 轴映射
    p.x = parseAxisMapping(obj.value("x").toObject(), 0);
    p.y = parseAxisMapping(obj.value("y").toObject(), 1);
    p.z = parseAxisMapping(obj.value("z").toObject(), 2);
    // 四元数 W 分量（仅 purpose=quaternion）
    p.w = parseAxisMapping(obj.value("w").toObject(), 0);
    // 气压计映射（仅 purpose=barometer）
    p.altitude = parseAxisMapping(obj.value("altitude").toObject(), 0);
    p.pressure = parseAxisMapping(obj.value("pressure").toObject(), 0);

    // 颜色
    QJsonArray colorArr = obj.value("color").toArray();
    if (colorArr.size() == 4) {
        p.color = QColor(colorArr[0].toInt(255), colorArr[1].toInt(0),
                         colorArr[2].toInt(0), colorArr[3].toInt(255));
    }

    // 点大小
    p.size = obj.value("size").toInt(10);

    return p;
}

// ── 公有 Getter ───────────────────────────────────────────────

// 获取重力参考值（m/s²）
double ConfigLoader::gravityReference() const {
    return m_config.value("gravity_reference").toDouble(9.80);
}

// 获取 UDP 配置
UdpConfig ConfigLoader::getUdpConfig() const {
    QJsonObject obj = m_config.value("udp").toObject();
    UdpConfig c;
    c.enabled = obj.value("enabled").toBool(true);
    c.ip      = obj.value("ip").toString("127.0.0.1");
    c.port    = obj.value("port").toInt(8888);
    return c;
}

// 获取串口配置
SerialConfig ConfigLoader::getSerialConfig() const {
    QJsonObject obj = m_config.value("serial").toObject();
    SerialConfig c;
    c.enabled  = obj.value("enabled").toBool(true);
    c.port     = obj.value("port").toString("COM5");
    c.baudrate = obj.value("baudrate").toInt(115200);
    c.timeout  = obj.value("timeout").toInt(1);
    c.protocol = obj.value("protocol").toString("atkms901m");
    c.accFsr   = obj.value("acc_fsr").toInt(4);
    c.gyroFsr  = obj.value("gyro_fsr").toInt(2000);
    return c;
}

// 获取渲染调试配置
RenderDebugConfig ConfigLoader::getRenderDebugConfig() const {
    QJsonObject obj = m_config.value("render_debug").toObject();
    RenderDebugConfig c;
    c.enabled             = obj.value("enabled").toBool(false);
    c.verbosePointUpdates = obj.value("verbose_point_updates").toBool(false);
    return c;
}

// 获取 INS 配置（含各子模块配置）
InsConfig ConfigLoader::getInsConfig() const {
    QJsonObject ins = m_config.value("ins").toObject();
    InsConfig c;

    // 卡尔曼配置
    QJsonObject kf = ins.value("kalman").toObject();
    c.kalman.enabled           = kf.value("enabled").toBool(true);
    c.kalman.processNoiseSigma = kf.value("process_noise_sigma").toDouble(0.5);
    c.kalman.measurementNoiseR = kf.value("measurement_noise_R").toDouble(0.5);

    // ZUPT 配置
    QJsonObject zupt = ins.value("zupt").toObject();
    c.zupt.enabled               = zupt.value("enabled").toBool(true);
    c.zupt.accVarianceThreshold  = zupt.value("acc_variance_threshold").toDouble(0.5);
    c.zupt.gyroVarianceThreshold = zupt.value("gyro_variance_threshold").toDouble(0.1);
    c.zupt.windowSize            = zupt.value("window_size").toInt(40);

    // 气压低通滤波系数
    c.baroLpfAlpha = ins.value("baro_lpf_alpha").toDouble(0.1);

    // Madgwick 配置
    c.madgwick.beta = ins.value("madgwick").toObject().value("beta").toDouble(0.05);

    // Mahony 配置
    QJsonObject mahony = ins.value("mahony").toObject();
    c.mahony.kp = mahony.value("kp").toDouble(1.0);
    c.mahony.ki = mahony.value("ki").toDouble(0.0);

    // 偏航角修正偏移
    c.filterYawOffsetDeg = ins.value("filter_yaw_offset_deg").toDouble(90.0);

    return c;
}

// ── 协议加载 ─────────────────────────────────────────────────

void ConfigLoader::loadProtocol()
{
    m_protocolDef = QJsonObject();
    m_fieldIndexMap.clear();

    QString protocolName = m_config.value("serial").toObject()
                               .value("protocol").toString();
    if (protocolName.isEmpty() || protocolName == "csv") {
        qDebug() << "ConfigLoader: 协议为 CSV 或空，跳过协议定义加载";
        return;
    }

    QString path = protocolFilePath(protocolName);
    if (path.isEmpty()) {
        qWarning() << "ConfigLoader: 未找到协议文件" << protocolName;
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ConfigLoader: 无法打开协议文件" << path;
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ConfigLoader: 协议文件 JSON 解析失败:" << err.errorString();
        return;
    }

    m_protocolDef = doc.object();

    // 构建 field name → snapshot index 映射
    QJsonArray orderArr = m_protocolDef.value("snapshot_order").toArray();
    for (int i = 0; i < orderArr.size(); ++i) {
        QString name = orderArr[i].toString();
        if (!name.isEmpty())
            m_fieldIndexMap[name] = i;
    }

    qDebug() << "ConfigLoader: 已加载协议" << protocolName
             << "(" << m_fieldIndexMap.size() << "个字段)";
}

QVariantMap ConfigLoader::getProtocolVariableOverrides() const
{
    QVariantMap overrides;
    QJsonObject serial = m_config.value("serial").toObject();

    if (serial.contains("acc_fsr"))
        overrides["acc_fsr"] = serial.value("acc_fsr").toDouble();
    if (serial.contains("gyro_fsr"))
        overrides["gyro_fsr"] = serial.value("gyro_fsr").toDouble();

    return overrides;
}

int ConfigLoader::resolveFieldIndex(const QString& fieldName) const
{
    return m_fieldIndexMap.value(fieldName, -1);
}

// ── Points 缓存 ──────────────────────────────────────────────

void ConfigLoader::rebuildPointsCache() {
    QJsonArray arr = m_config.value("points").toArray();
    m_cachedPoints.clear();
    m_cachedPoints.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (v.isObject()) {
            m_cachedPoints.append(parsePointConfig(v.toObject()));
        }
    }
    resolveFieldNames();
    validatePointsCache();
}

// 将 AxisMapping 中的 field 名解析为 index（通过协议 snapshot_order）
void ConfigLoader::resolveFieldNames()
{
    if (m_fieldIndexMap.isEmpty()) return;

    auto resolveAxis = [this](AxisMapping& axis, const QString& ctx) {
        if (axis.field.isEmpty()) return;
        int idx = m_fieldIndexMap.value(axis.field, -1);
        if (idx < 0) {
            qWarning() << "ConfigLoader:" << ctx
                       << "field '" + axis.field + "' 在协议 snapshot_order 中未找到";
        } else {
            axis.index = idx;
        }
    };

    for (int i = 0; i < m_cachedPoints.size(); ++i) {
        PointConfig& p = m_cachedPoints[i];
        QString ctx = QString("points[%1] name='%2'").arg(i).arg(p.name);

        resolveAxis(p.x, ctx + " x");
        resolveAxis(p.y, ctx + " y");
        resolveAxis(p.z, ctx + " z");
        resolveAxis(p.w, ctx + " w");
        resolveAxis(p.altitude, ctx + " altitude");
        resolveAxis(p.pressure, ctx + " pressure");
    }
}

void ConfigLoader::validatePointsCache() {
    QSet<QString> seenNames;
    // key = "source|prefix|purpose", 检测同一数据源下的 purpose 冲突
    QSet<QString> seenSensorBindings;

    for (int i = 0; i < m_cachedPoints.size(); ++i) {
        const PointConfig& p = m_cachedPoints[i];
        QString ctx = QString("points[%1] name='%2'").arg(i).arg(p.name);

        // 1. name 唯一性
        if (p.name.isEmpty()) {
            qWarning() << "ConfigLoader:" << ctx << "name 为空";
        } else if (seenNames.contains(p.name)) {
            qWarning() << "ConfigLoader:" << ctx << "name 重复";
        } else {
            seenNames.insert(p.name);
        }

        // 2. purpose 白名单
        if (!PointPurpose::isValid(p.purpose)) {
            qWarning() << "ConfigLoader:" << ctx
                       << "purpose '" + p.purpose + "' 不在合法值列表中"
                          " (accelerometer/gyroscope/quaternion/magnetic_field/barometer/空)";
        }

        // 3. index 合法性（>= 0）
        auto checkIndex = [&](const QString& axis, int idx) {
            if (idx < 0)
                qWarning() << "ConfigLoader:" << ctx
                           << axis << "index =" << idx << "不合法（需 >= 0）";
        };
        checkIndex("x", p.x.index);
        checkIndex("y", p.y.index);
        checkIndex("z", p.z.index);
        if (p.purpose == PointPurpose::Quaternion)
            checkIndex("w", p.w.index);
        if (p.purpose == PointPurpose::Barometer) {
            checkIndex("altitude", p.altitude.index);
            checkIndex("pressure", p.pressure.index);
        }

        // 4. 同一 (source, prefix) 下 sensor purpose 唯一性
        if (PointPurpose::isSensor(p.purpose)) {
            QString bindingKey = p.source + "|"
                               + p.prefix.value_or(QString()) + "|"
                               + p.purpose;
            if (seenSensorBindings.contains(bindingKey)) {
                qWarning() << "ConfigLoader:" << ctx
                           << "source='" + p.source + "' prefix='"
                              + p.prefix.value_or(QString()) + "' 下存在重复的"
                           << p.purpose << "映射";
            } else {
                seenSensorBindings.insert(bindingKey);
            }
        }
    }

    // 5. 必需 purpose 存在性检查
    bool hasAcc = false, hasGyro = false;
    for (const PointConfig& p : m_cachedPoints) {
        if (p.purpose == PointPurpose::Accelerometer) hasAcc = true;
        if (p.purpose == PointPurpose::Gyroscope)     hasGyro = true;
    }
    if (!hasAcc)
        qWarning() << "ConfigLoader: points 中缺少 purpose='accelerometer' 的传感器配置，INS 管线将无法工作";
    if (!hasGyro)
        qWarning() << "ConfigLoader: points 中缺少 purpose='gyroscope' 的传感器配置，AHRS 和 ZUPT 将无法工作";
}

// 获取所有数据点配置（返回缓存引用）
const QList<PointConfig>& ConfigLoader::getPoints() const {
    return m_cachedPoints;
}

// 按 purpose + source/prefix 查找第一个匹配的传感器点
const PointConfig* ConfigLoader::findSensorPoint(const QString& purpose,
                                                  const QString& source,
                                                  const QString& prefix) const {
    for (const PointConfig& p : m_cachedPoints) {
        if (p.purpose == purpose && p.matchesSource(source, prefix))
            return &p;
    }
    return nullptr;
}

// 判断是否存在指定 purpose 的传感器点
bool ConfigLoader::hasSensorPurpose(const QString& purpose) const {
    for (const PointConfig& p : m_cachedPoints) {
        if (p.purpose == purpose) return true;
    }
    return false;
}

// ── 协议管理 ──────────────────────────────────────────────────

QStringList ConfigLoader::availableProtocols()
{
    QStringList result;

    auto scanDir = [&](const QString& dirPath) {
        QDir dir(dirPath);
        if (!dir.exists()) return;
        QStringList entries = dir.entryList({"*.json"}, QDir::Files);
        for (const QString& f : entries) {
            QString name = f.chopped(5); // remove ".json"
            if (!result.contains(name))
                result.append(name);
        }
    };

    // 应用程序目录下
    scanDir(QCoreApplication::applicationDirPath() + "/protocols");

    // 开发模式：从工作目录向上查找
    QDir dir = QDir::current();
    for (int i = 0; i < 4; ++i) {
        scanDir(dir.absoluteFilePath("protocols"));
        if (!dir.cdUp()) break;
    }

    result.sort();
    return result;
}

QJsonObject ConfigLoader::loadProtocolDef(const QString& name)
{
    if (name.isEmpty() || name == "csv") return QJsonObject();

    QString path = protocolFilePath(name);
    if (path.isEmpty()) return QJsonObject();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QJsonObject();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();

    return doc.object();
}

void ConfigLoader::updateProtocolAndMappings(const QString& protocol,
                                              const QVariantMap& vars,
                                              const QList<PointConfig>& sensorPoints)
{
    // 更新 serial.protocol 和变量
    QJsonObject serial = m_config.value("serial").toObject();
    serial["protocol"] = protocol;
    for (auto it = vars.begin(); it != vars.end(); ++it)
        serial[it.key()] = QJsonValue::fromVariant(it.value());
    m_config["serial"] = serial;

    // 重建 points 数组：保留非 sensor 的 points，用新的 sensorPoints 替换 sensor 类型的
    QJsonArray newPointsArr;

    // 保留现有的纯可视化 points（purpose 为空）
    QJsonArray oldArr = m_config.value("points").toArray();
    for (const QJsonValue& v : oldArr) {
        QJsonObject obj = v.toObject();
        QString purpose = obj.value("purpose").toString();
        if (!PointPurpose::isSensor(purpose))
            newPointsArr.append(obj);
    }

    // 序列化新的 sensor points
    auto serializeAxis = [](const AxisMapping& axis) -> QJsonObject {
        QJsonObject obj;
        if (!axis.field.isEmpty())
            obj["field"] = axis.field;
        else
            obj["index"] = axis.index;
        if (std::abs(axis.multiplier - 1.0) > 1e-9)
            obj["multiplier"] = axis.multiplier;
        return obj;
    };

    for (const PointConfig& p : sensorPoints) {
        QJsonObject obj;
        obj["name"]    = p.name;
        obj["source"]  = p.source;
        obj["purpose"] = p.purpose;

        if (p.purpose == PointPurpose::Barometer) {
            obj["altitude"] = serializeAxis(p.altitude);
            obj["pressure"] = serializeAxis(p.pressure);
        } else if (p.purpose == PointPurpose::Quaternion) {
            obj["w"] = serializeAxis(p.w);
            obj["x"] = serializeAxis(p.x);
            obj["y"] = serializeAxis(p.y);
            obj["z"] = serializeAxis(p.z);
        } else {
            obj["x"] = serializeAxis(p.x);
            obj["y"] = serializeAxis(p.y);
            obj["z"] = serializeAxis(p.z);
        }

        newPointsArr.prepend(obj);
    }

    m_config["points"] = newPointsArr;

    save();
    loadProtocol();
    rebuildPointsCache();
}
