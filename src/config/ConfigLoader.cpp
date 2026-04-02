#include "ConfigLoader.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <QDir>

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

    // 数据点默认配置列表
    auto makeAxis = [](int idx, double mult) -> QJsonObject {
        QJsonObject a;
        a["index"]      = idx;
        a["multiplier"] = mult;
        return a;
    };

    QJsonArray points;
    {
        QJsonObject p;
        p["name"] = "ACC"; p["source"] = "serial"; p["purpose"] = "accelerometer";
        p["x"] = makeAxis(0,1); p["y"] = makeAxis(1,1); p["z"] = makeAxis(2,1);
        points.append(p);
    }
    {
        QJsonObject p;
        p["name"] = "GYR"; p["source"] = "serial"; p["purpose"] = "gyroscope";
        p["x"] = makeAxis(3,1); p["y"] = makeAxis(4,1); p["z"] = makeAxis(5,1);
        points.append(p);
    }
    {
        QJsonObject p;
        p["name"] = "QUAT"; p["source"] = "serial"; p["purpose"] = "quaternion";
        p["w"] = makeAxis(6,1); p["x"] = makeAxis(7,1);
        p["y"] = makeAxis(8,1); p["z"] = makeAxis(9,1);
        points.append(p);
    }
    {
        QJsonObject p;
        p["name"] = "MAG"; p["source"] = "serial"; p["purpose"] = "magnetic_field";
        p["x"] = makeAxis(10,1); p["y"] = makeAxis(11,1); p["z"] = makeAxis(12,1);
        points.append(p);
    }
    {
        QJsonObject p;
        p["name"] = "BARO"; p["source"] = "serial"; p["purpose"] = "barometer";
        p["altitude"] = makeAxis(18,1); p["pressure"] = makeAxis(17,1);
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
            qDebug() << "ConfigLoader: 已从" << path << "加载配置";
            // 若合并后与文件内容不同（补充了缺失字段），则自动保存
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

// 从 JSON 对象解析轴映射（index + multiplier）
AxisMapping ConfigLoader::parseAxisMapping(const QJsonObject& obj,
                                            int defaultIndex,
                                            double defaultMult) {
    AxisMapping m;
    m.index      = obj.value("index").toInt(defaultIndex);
    m.multiplier = obj.value("multiplier").toDouble(defaultMult);
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

// 获取所有数据点配置
QList<PointConfig> ConfigLoader::getPoints() const {
    QJsonArray arr = m_config.value("points").toArray();
    QList<PointConfig> result;
    result.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (v.isObject()) {
            result.append(parsePointConfig(v.toObject()));
        }
    }
    return result;
}
