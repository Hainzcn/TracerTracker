#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <optional>

// ============================================================
// TextProtocolParser.h — 配置驱动的文本协议解析器
//
// 通过 JSON 协议定义文件驱动：
//   framing.type = "text_csv"   → 按分隔符切列；可选前缀；非数字字符处理策略可配
//   framing.type = "text_regex" → 整行正则匹配，按捕获组映射到字段
//
// 与 GenericFrameParser 并列，共同构成 DataReceiver 的协议分派后端。
// ============================================================

class TextProtocolParser {
public:
    // 单行解析结果
    struct Result {
        QString       prefix;
        QList<double> snapshot;
    };

    TextProtocolParser() = default;

    // 从协议定义 JSON 构造解析器
    static TextProtocolParser fromJson(const QJsonObject& protocolDef);

    // 按块喂入文本（可含多行），内部维护行缓冲并按 line_terminator 切行
    QList<Result> feed(const QString& chunk);

    // 单行直接解析（UDP 整包路径用，不经过行缓冲）
    std::optional<Result> parseLine(const QString& line) const;

    // 重置行缓冲
    void reset();

    bool    isValid() const { return m_valid; }
    QString kind()    const;                       // "text_csv" / "text_regex"
    QStringList fieldNames() const { return m_snapshotOrder; }

    // 将快照格式化为调试字符串（与 GenericFrameParser 对齐）
    QString formatDebug(const QList<double>& snapshot) const;

private:
    enum class Kind    { Csv, Regex };
    enum class OnError { Strip, Skip, Nan };

    bool    m_valid   = false;
    Kind    m_kind    = Kind::Csv;
    OnError m_onError = OnError::Strip;

    // ── 共用 ──
    QStringList        m_snapshotOrder;
    QMap<QString,int>  m_fieldIndexMap;
    QString            m_lineBuffer;
    QString            m_lineTerminator = "\n";

    // ── CSV 字段 ──
    QChar   m_csvDelim         = ',';
    bool    m_csvPrefixEnabled = false;
    QChar   m_csvPrefixDelim   = ':';
    int     m_csvPrefixMaxLen  = 16;
    QRegularExpression m_csvStripRe;   // 匹配「非法字符」的正则（用于 strip 策略）

    // ── Regex 字段 ──
    struct CaptureSpec {
        QString field;
        int     groupIdx  = -1;    // -1 表示按 groupName 取
        QString groupName;
        double  multiplier = 1.0;
    };
    QRegularExpression m_regex;
    int                m_prefixGroupIdx = -1;
    QString            m_prefixGroupName;
    QList<CaptureSpec> m_captures;

    // ── 内部方法 ──
    std::optional<Result> parseCsv(const QString& line) const;
    std::optional<Result> parseRegex(const QString& line) const;
    QList<double>         buildSnapshot(const QMap<QString,double>& values) const;

    static OnError parseOnError(const QString& s);
    static QString buildStripCharClass(const QString& charset);
};
