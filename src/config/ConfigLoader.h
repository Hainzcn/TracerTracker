#pragma once
#include "ConfigTypes.h"
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QList>
#include <QVariantMap>

// ============================================================
// ConfigLoader.h — 配置加载器（单例）
// 负责读取/写入 config.json，并与硬编码默认值深度合并
// 同时加载协议定义文件（protocols/*.json），提供字段名→索引解析
// ============================================================

class ConfigLoader {
public:
    // 获取全局单例实例（懒加载，线程不安全，仅在主线程使用）
    static ConfigLoader& instance();

    // 禁止拷贝与移动
    ConfigLoader(const ConfigLoader&)            = delete;
    ConfigLoader& operator=(const ConfigLoader&) = delete;

    // ── 配置读取 ────────────────────────────────────────────

    UdpConfig         getUdpConfig() const;
    SerialConfig      getSerialConfig() const;
    RenderDebugConfig getRenderDebugConfig() const;
    InsConfig         getInsConfig() const;

    // 获取全部数据点配置列表（缓存，仅 reload 时重新解析）
    const QList<PointConfig>& getPoints() const;

    // 按 purpose + source/prefix 查找第一个匹配的传感器点
    const PointConfig* findSensorPoint(const QString& purpose,
                                       const QString& source,
                                       const QString& prefix) const;

    // 判断是否存在指定 purpose 的传感器点（不考虑 source/prefix）
    bool hasSensorPurpose(const QString& purpose) const;

    double gravityReference() const;

    // ── 协议定义 ────────────────────────────────────────────

    // 获取当前已加载的协议定义（完整 JSON，可传给 GenericFrameParser::fromJson）
    const QJsonObject& getProtocolDef() const { return m_protocolDef; }

    // 获取协议变量覆盖（serial.acc_fsr / gyro_fsr 等）
    QVariantMap getProtocolVariableOverrides() const;

    // 字段名 → snapshot_order 中的数组下标（-1 表示未找到）
    int resolveFieldIndex(const QString& fieldName) const;

    // 是否已成功加载协议定义
    bool hasProtocol() const { return !m_protocolDef.isEmpty(); }

    // ── 协议管理 ────────────────────────────────────────────

    // 扫描 protocols/ 目录，返回可用协议名列表（不含 .json 后缀）
    static QStringList availableProtocols();

    // 加载指定协议定义 JSON（仅返回，不改变当前活跃协议状态）
    static QJsonObject loadProtocolDef(const QString& name);

    // 批量更新协议选择 + 变量覆盖 + 传感器 points，然后 save + reload
    void updateProtocolAndMappings(const QString& protocol,
                                   const QVariantMap& vars,
                                   const QList<PointConfig>& sensorPoints);

    // ── 持久化 ──────────────────────────────────────────────

    void save() const;
    void reload();

private:
    ConfigLoader();

    static QString configFilePath();
    static QString protocolFilePath(const QString& protocolName);

    static QJsonObject mergeWithDefaults(const QJsonObject& loaded,
                                         const QJsonObject& defaults);

    static AxisMapping parseAxisMapping(const QJsonObject& obj,
                                        int defaultIndex = 0,
                                        double defaultMult = 1.0);
    static PointConfig parsePointConfig(const QJsonObject& obj);
    static QJsonObject buildDefaultJson();

    QJsonObject m_config;

    // 协议定义（从 protocols/<name>.json 加载）
    QJsonObject m_protocolDef;
    QMap<QString, int> m_fieldIndexMap;
    void loadProtocol();

    // 缓存的点配置列表
    QList<PointConfig> m_cachedPoints;
    void rebuildPointsCache();
    void validatePointsCache();
    void resolveFieldNames();
};
