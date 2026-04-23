#include "TextProtocolParser.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <cmath>
#include <limits>

// ============================================================
// TextProtocolParser.cpp — 文本协议解析器实现
// ============================================================

// ── 辅助 ────────────────────────────────────────────────────

TextProtocolParser::OnError TextProtocolParser::parseOnError(const QString& s)
{
    const QString lower = s.toLower();
    if (lower == "skip") return OnError::Skip;
    if (lower == "nan")  return OnError::Nan;
    return OnError::Strip;   // 默认 strip
}

// 将用户在 JSON 中填的 "0-9.+\\-eE" 这类字符集构造成 "[^0-9.+\\-eE]" 的排除正则
QString TextProtocolParser::buildStripCharClass(const QString& charset)
{
    QString cs = charset;
    if (cs.isEmpty()) cs = "0-9.+\\-eE";
    return "[^" + cs + "]";
}

QString TextProtocolParser::kind() const
{
    return m_kind == Kind::Csv ? "text_csv" : "text_regex";
}

// ── fromJson ────────────────────────────────────────────────

TextProtocolParser TextProtocolParser::fromJson(const QJsonObject& protocolDef)
{
    TextProtocolParser p;

    const QString type = protocolDef.value("framing").toObject()
                                    .value("type").toString();
    if (type == "text_csv") {
        p.m_kind = Kind::Csv;
    } else if (type == "text_regex") {
        p.m_kind = Kind::Regex;
    } else {
        qWarning() << "TextProtocolParser: 不支持的 framing.type =" << type;
        return p;
    }

    const QJsonObject textObj = protocolDef.value("text").toObject();

    // 行终止符
    p.m_lineTerminator = textObj.value("line_terminator").toString("\n");
    if (p.m_lineTerminator.isEmpty()) p.m_lineTerminator = "\n";

    // on_parse_error（Regex 路径也沿用）
    p.m_onError = parseOnError(textObj.value("on_parse_error").toString("strip"));

    // snapshot_order → field → index 映射
    const QJsonArray orderArr = protocolDef.value("snapshot_order").toArray();
    for (int i = 0; i < orderArr.size(); ++i) {
        const QString name = orderArr[i].toString();
        if (!name.isEmpty()) {
            p.m_snapshotOrder.append(name);
            p.m_fieldIndexMap[name] = i;
        }
    }

    if (p.m_kind == Kind::Csv) {
        // 分隔符（取首字符，默认 ','）
        const QString delimStr = textObj.value("delimiter").toString(",");
        p.m_csvDelim = delimStr.isEmpty() ? QChar(',') : delimStr.at(0);

        // 前缀
        const QJsonObject pfx = textObj.value("prefix").toObject();
        p.m_csvPrefixEnabled = pfx.value("enabled").toBool(false);
        const QString pdelim = pfx.value("delimiter").toString(":");
        p.m_csvPrefixDelim   = pdelim.isEmpty() ? QChar(':') : pdelim.at(0);
        p.m_csvPrefixMaxLen  = pfx.value("max_length").toInt(16);

        // strip 策略的字符集
        const QString charset = textObj.value("numeric_strip_charset")
                                       .toString("0-9.+\\-eE");
        p.m_csvStripRe = QRegularExpression(buildStripCharClass(charset));

        p.m_valid = !p.m_snapshotOrder.isEmpty();
        if (!p.m_valid)
            qWarning() << "TextProtocolParser(csv): snapshot_order 为空";
    } else {
        // Regex
        const QString pattern = textObj.value("pattern").toString();
        if (pattern.isEmpty()) {
            qWarning() << "TextProtocolParser(regex): 缺少 text.pattern";
            return p;
        }
        p.m_regex = QRegularExpression(pattern);
        if (!p.m_regex.isValid()) {
            qWarning() << "TextProtocolParser(regex): pattern 非法:"
                       << p.m_regex.errorString();
            return p;
        }

        // 前缀组：可为整数或命名
        const QJsonValue prefGrp = textObj.value("prefix_group");
        if (prefGrp.isDouble()) {
            p.m_prefixGroupIdx = prefGrp.toInt(-1);
        } else if (prefGrp.isString()) {
            p.m_prefixGroupName = prefGrp.toString();
        }

        // captures
        const QJsonArray capArr = textObj.value("captures").toArray();
        for (const QJsonValue& v : capArr) {
            const QJsonObject o = v.toObject();
            CaptureSpec c;
            c.field      = o.value("field").toString();
            c.multiplier = o.value("multiplier").toDouble(1.0);
            const QJsonValue g = o.value("group");
            if (g.isDouble()) {
                c.groupIdx = g.toInt(-1);
            } else if (g.isString()) {
                c.groupName = g.toString();
            }
            if (c.field.isEmpty()) {
                qWarning() << "TextProtocolParser(regex): capture 缺少 field";
                continue;
            }
            p.m_captures.append(c);
        }

        p.m_valid = !p.m_snapshotOrder.isEmpty() && !p.m_captures.isEmpty();
        if (!p.m_valid)
            qWarning() << "TextProtocolParser(regex): snapshot_order 或 captures 为空";
    }

    return p;
}

// ── 重置 ────────────────────────────────────────────────────

void TextProtocolParser::reset()
{
    m_lineBuffer.clear();
}

// ── feed：按行切分后逐行解析 ────────────────────────────────

QList<TextProtocolParser::Result> TextProtocolParser::feed(const QString& chunk)
{
    QList<Result> out;
    if (!m_valid) return out;

    m_lineBuffer += chunk;

    while (true) {
        const int pos = m_lineBuffer.indexOf(m_lineTerminator);
        if (pos < 0) break;
        QString line = m_lineBuffer.left(pos);
        m_lineBuffer = m_lineBuffer.mid(pos + m_lineTerminator.size());

        // 去除潜在 \r
        if (!line.isEmpty() && line.endsWith('\r'))
            line.chop(1);

        if (line.trimmed().isEmpty()) continue;

        auto r = parseLine(line);
        if (r.has_value()) out.append(*r);
    }

    // 防止 buffer 无限增长（未遇到换行的超长垃圾）
    if (m_lineBuffer.size() > 8192)
        m_lineBuffer = m_lineBuffer.right(4096);

    return out;
}

// ── parseLine：单行分派 ─────────────────────────────────────

std::optional<TextProtocolParser::Result>
TextProtocolParser::parseLine(const QString& line) const
{
    if (!m_valid) return std::nullopt;
    return m_kind == Kind::Csv ? parseCsv(line) : parseRegex(line);
}

// ── CSV 解析 ────────────────────────────────────────────────

std::optional<TextProtocolParser::Result>
TextProtocolParser::parseCsv(const QString& rawLine) const
{
    QString line = rawLine.trimmed();
    if (line.isEmpty()) return std::nullopt;

    QString prefix;

    // 前缀识别
    if (m_csvPrefixEnabled) {
        const int delimPos = line.indexOf(m_csvPrefixDelim);
        if (delimPos > 0 && delimPos <= m_csvPrefixMaxLen) {
            QString candidate = line.left(delimPos).trimmed();
            if (!candidate.isEmpty()) {
                prefix = candidate;
                line   = line.mid(delimPos + 1).trimmed();
            }
        }
    }

    if (line.isEmpty()) return std::nullopt;

    const QStringList parts = line.split(m_csvDelim);
    QList<double> values;
    values.reserve(parts.size());

    for (const QString& rawTok : parts) {
        QString tok = rawTok.trimmed();
        bool ok = false;
        double v = tok.toDouble(&ok);

        if (!ok) {
            switch (m_onError) {
            case OnError::Strip: {
                QString cleaned = tok;
                cleaned.remove(m_csvStripRe);
                v = cleaned.toDouble(&ok);
                if (!ok)
                    v = std::numeric_limits<double>::quiet_NaN();
                values.append(v);
                break;
            }
            case OnError::Skip:
                // 跳过该列：不追加任何值，接受可能的列错位
                break;
            case OnError::Nan:
                values.append(std::numeric_limits<double>::quiet_NaN());
                break;
            }
        } else {
            values.append(v);
        }
    }

    if (values.isEmpty()) return std::nullopt;
    return Result{prefix, values};
}

// ── Regex 解析 ──────────────────────────────────────────────

std::optional<TextProtocolParser::Result>
TextProtocolParser::parseRegex(const QString& rawLine) const
{
    const QString line = rawLine.trimmed();
    if (line.isEmpty()) return std::nullopt;

    const QRegularExpressionMatch m = m_regex.match(line);
    if (!m.hasMatch()) return std::nullopt;

    // 前缀
    QString prefix;
    if (!m_prefixGroupName.isEmpty()) {
        prefix = m.captured(m_prefixGroupName);
    } else if (m_prefixGroupIdx >= 0) {
        prefix = m.captured(m_prefixGroupIdx);
    }

    // 收集字段值
    QMap<QString,double> latest;
    for (const CaptureSpec& c : m_captures) {
        QString rawTok;
        if (!c.groupName.isEmpty())
            rawTok = m.captured(c.groupName);
        else if (c.groupIdx >= 0)
            rawTok = m.captured(c.groupIdx);

        const QString tok = rawTok.trimmed();
        bool ok = false;
        double v = tok.toDouble(&ok);

        if (!ok) {
            switch (m_onError) {
            case OnError::Strip: {
                QString cleaned = tok;
                static QRegularExpression s_defaultStrip("[^0-9.+\\-eE]");
                cleaned.remove(s_defaultStrip);
                v = cleaned.toDouble(&ok);
                if (!ok) v = std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case OnError::Skip:
                continue;  // 该字段缺省 → 不写入 latest
            case OnError::Nan:
                v = std::numeric_limits<double>::quiet_NaN();
                break;
            }
        }

        // 应用 multiplier（但 NaN 不要乘）
        if (!std::isnan(v)) v *= c.multiplier;
        latest[c.field] = v;
    }

    if (latest.isEmpty()) return std::nullopt;
    return Result{prefix, buildSnapshot(latest)};
}

// ── 快照构建 ────────────────────────────────────────────────

QList<double>
TextProtocolParser::buildSnapshot(const QMap<QString,double>& values) const
{
    QList<double> snap;
    snap.reserve(m_snapshotOrder.size());
    for (const QString& name : m_snapshotOrder)
        snap.append(values.value(name, 0.0));
    return snap;
}

// ── 调试输出 ────────────────────────────────────────────────

QString TextProtocolParser::formatDebug(const QList<double>& snapshot) const
{
    QStringList parts;
    const int n = std::min(snapshot.size(), m_snapshotOrder.size());
    for (int i = 0; i < n; ++i)
        parts << QString("%1=%2")
                    .arg(m_snapshotOrder[i])
                    .arg(snapshot[i], 0, 'f', 3);
    return parts.join(' ');
}
