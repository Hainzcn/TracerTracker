#pragma once
#include "ConfigTypes.h"
#include <QJsonObject>
#include <QString>
#include <QList>

// ============================================================
// ConfigLoader.h — 配置加载器（单例）
// 负责读取/写入 config.json，并与硬编码默认值深度合并
// ============================================================

class ConfigLoader {
public:
    // 获取全局单例实例（懒加载，线程不安全，仅在主线程使用）
    static ConfigLoader& instance();

    // 禁止拷贝与移动
    ConfigLoader(const ConfigLoader&)            = delete;
    ConfigLoader& operator=(const ConfigLoader&) = delete;

    // ── 配置读取 ────────────────────────────────────────────

    // 获取 UDP 配置
    UdpConfig         getUdpConfig() const;

    // 获取串口配置
    SerialConfig      getSerialConfig() const;

    // 获取渲染调试配置
    RenderDebugConfig getRenderDebugConfig() const;

    // 获取 INS（惯性导航系统）配置
    InsConfig         getInsConfig() const;

    // 获取全部数据点配置列表
    QList<PointConfig> getPoints() const;

    // 获取重力参考值（m/s²）
    double gravityReference() const;

    // ── 持久化 ──────────────────────────────────────────────

    // 将当前配置回写到 config.json
    void save() const;

    // 重新从磁盘加载配置（会覆盖当前内存配置）
    void reload();

private:
    // 私有构造函数（单例模式）
    ConfigLoader();

    // 查找 config.json 的绝对路径（exe 同目录）
    static QString configFilePath();

    // 将从文件读取的 JSON 对象与默认值深度合并
    static QJsonObject mergeWithDefaults(const QJsonObject& loaded,
                                         const QJsonObject& defaults);

    // 解析单个 AxisMapping（包含 index 与 multiplier）
    static AxisMapping parseAxisMapping(const QJsonObject& obj,
                                        int defaultIndex = 0,
                                        double defaultMult = 1.0);

    // 解析单个 PointConfig 条目
    static PointConfig parsePointConfig(const QJsonObject& obj);

    // 构造包含所有默认值的 QJsonObject
    static QJsonObject buildDefaultJson();

    // 当前配置（已与默认值合并的 JSON 根对象）
    QJsonObject m_config;
};
