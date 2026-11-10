#include "serial/frame_splitter.h"

namespace xcom {
namespace {

constexpr int kScanWindow = 64 * 1024;

void appendFrame(QVector<ParsedFrame> *frames, const QVector<ParsedFrame> &more)
{
    for (const ParsedFrame &frame : more) {
        frames->append(frame);
    }
}

} // namespace

QString frameModeLabel(FrameMode mode)
{
    switch (mode) {
    case FrameMode::Line:
        return QStringLiteral("按行解析 (\\r\\n / \\n / \\r)");
    case FrameMode::Delimiter:
        return QStringLiteral("自定义分隔符");
    case FrameMode::FixedLength:
        return QStringLiteral("固定长度");
    case FrameMode::Idle:
        return QStringLiteral("空闲分帧");
    }
    return QStringLiteral("按行解析");
}

QString frameModeKey(FrameMode mode)
{
    switch (mode) {
    case FrameMode::Line:
        return QStringLiteral("line");
    case FrameMode::Delimiter:
        return QStringLiteral("delimiter");
    case FrameMode::FixedLength:
        return QStringLiteral("fixedLength");
    case FrameMode::Idle:
        return QStringLiteral("idle");
    }
    return QStringLiteral("line");
}

FrameMode frameModeFromKey(const QString &key, bool *ok)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("line") || normalized == QStringLiteral("lines")) {
        if (ok) {
            *ok = true;
        }
        return FrameMode::Line;
    }
    if (normalized == QStringLiteral("delimiter") || normalized == QStringLiteral("separator")) {
        if (ok) {
            *ok = true;
        }
        return FrameMode::Delimiter;
    }
    if (normalized == QStringLiteral("fixedlength") || normalized == QStringLiteral("fixed")) {
        if (ok) {
            *ok = true;
        }
        return FrameMode::FixedLength;
    }
    if (normalized == QStringLiteral("idle") || normalized == QStringLiteral("gap")) {
        if (ok) {
            *ok = true;
        }
        return FrameMode::Idle;
    }
    if (ok) {
        *ok = false;
    }
    return FrameMode::Line;
}

QByteArray FrameSplitter::decodeEscapes(const QString &text)
{
    QByteArray out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch != QLatin1Char('\\') || i + 1 >= text.size()) {
            out.append(ch.toLatin1());
            continue;
        }
        const QChar next = text.at(i + 1);
        switch (next.unicode()) {
        case 'r':
            out.append('\r');
            ++i;
            break;
        case 'n':
            out.append('\n');
            ++i;
            break;
        case 't':
            out.append('\t');
            ++i;
            break;
        case '0':
            out.append('\0');
            ++i;
            break;
        case '\\':
            out.append('\\');
            ++i;
            break;
        case 'x': {
            // \xHH：最多两位十六进制
            int value = 0;
            int consumed = 0;
            while (consumed < 2 && i + 2 + consumed < text.size()) {
                const QChar hex = text.at(i + 2 + consumed);
                bool ok = false;
                const int digit = QString(hex).toInt(&ok, 16);
                if (!ok) {
                    break;
                }
                value = value * 16 + digit;
                ++consumed;
            }
            if (consumed > 0) {
                out.append(static_cast<char>(value));
                i += 1 + consumed;
            } else {
                out.append('\\');
            }
            break;
        }
        default:
            out.append('\\');
            break;
        }
    }
    return out;
}

QString FrameSplitter::encodeEscapes(const QByteArray &bytes)
{
    QString out;
    out.reserve(bytes.size() * 2);
    for (char raw : bytes) {
        const char ch = raw;
        switch (ch) {
        case '\r':
            out.append(QStringLiteral("\\r"));
            break;
        case '\n':
            out.append(QStringLiteral("\\n"));
            break;
        case '\t':
            out.append(QStringLiteral("\\t"));
            break;
        case '\0':
            out.append(QStringLiteral("\\0"));
            break;
        case '\\':
            out.append(QStringLiteral("\\\\"));
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) >= 0x7f) {
                out.append(QStringLiteral("\\x%1")
                               .arg(static_cast<unsigned char>(ch), 2, 16, QLatin1Char('0'))
                               .toUpper()
                               .replace(QStringLiteral("\\X"), QStringLiteral("\\x")));
            } else {
                out.append(QChar::fromLatin1(ch));
            }
            break;
        }
    }
    return out;
}

void FrameSplitter::setSettings(const FrameSettings &settings)
{
    m_settings = settings;
    if (m_settings.fixedLength < 1) {
        m_settings.fixedLength = 1;
    }
    if (m_settings.maxFrameBytes < 16) {
        m_settings.maxFrameBytes = 16;
    }
    if (m_settings.delimiter.isEmpty()) {
        m_settings.delimiter = QByteArrayLiteral("\r\n");
    }
    reset();
}

void FrameSplitter::reset()
{
    m_buffer.clear();
    m_index = 0;
}

void FrameSplitter::emitFrame(QVector<ParsedFrame> *frames, QByteArray bytes, qint64 wallClockMs,
                              bool truncated, bool alreadyTrimmed)
{
    // 分隔符在分帧处（trim 之后）才追加，所以这里不能再 trim 一次，否则会把
    // 刚保留的 \r 删掉。alreadyTrimmed 由调用方声明"内容已按需处理过"。
    if (m_settings.trimWhitespace && !alreadyTrimmed) {
        bytes = bytes.trimmed();
    }
    if (bytes.isEmpty() && m_settings.dropEmpty) {
        return;
    }
    ParsedFrame frame;
    frame.index = ++m_index;
    frame.wallClockMs = wallClockMs;
    frame.bytes = bytes;
    frame.truncated = truncated;
    frames->append(frame);
}

QVector<ParsedFrame> FrameSplitter::drain(qint64 wallClockMs, bool force)
{
    QVector<ParsedFrame> frames;
    if (m_buffer.isEmpty()) {
        return frames;
    }

    switch (m_settings.mode) {
    case FrameMode::Line: {
        int start = 0;
        for (int i = 0; i < m_buffer.size(); ++i) {
            const char ch = m_buffer.at(i);
            if (ch != '\r' && ch != '\n') {
                continue;
            }
            QByteArray payload = m_buffer.mid(start, i - start);
            // \r\n 视为一个分隔符
            if (ch == '\r' && i + 1 < m_buffer.size() && m_buffer.at(i + 1) == '\n') {
                ++i;
            }
            if (m_settings.trimWhitespace) {
                payload = payload.trimmed();
            }
            if (m_settings.keepDelimiter) {
                payload.append(ch == '\r' ? QByteArrayLiteral("\r") : QByteArrayLiteral("\n"));
            }
            emitFrame(&frames, payload, wallClockMs, false, true);
            start = i + 1;
        }
        m_buffer.remove(0, start);
        break;
    }
    case FrameMode::Delimiter: {
        const QByteArray &delimiter = m_settings.delimiter;
        int searchFrom = 0;
        int start = 0;
        while (true) {
            const int index = m_buffer.indexOf(delimiter, searchFrom);
            if (index < 0) {
                break;
            }
            QByteArray payload = m_buffer.mid(start, index - start);
            if (m_settings.trimWhitespace) {
                payload = payload.trimmed();
            }
            if (m_settings.keepDelimiter) {
                payload.append(delimiter);
            }
            emitFrame(&frames, payload, wallClockMs, false, true);
            start = index + delimiter.size();
            searchFrom = start;
        }
        m_buffer.remove(0, start);
        break;
    }
    case FrameMode::FixedLength: {
        const int length = m_settings.fixedLength;
        while (m_buffer.size() >= length) {
            emitFrame(&frames, m_buffer.left(length), wallClockMs, false);
            m_buffer.remove(0, length);
        }
        break;
    }
    case FrameMode::Idle:
        // 空闲分帧依赖 flush()：收到新数据只负责累积。
        break;
    }

    // 防止无分隔符时缓冲区无限增长
    if (m_buffer.size() > m_settings.maxFrameBytes) {
        const int overflow = m_buffer.size() - m_settings.maxFrameBytes;
        m_buffer.remove(0, overflow);
    }

    if (force && !m_buffer.isEmpty()) {
        emitFrame(&frames, m_buffer, wallClockMs, true);
        m_buffer.clear();
    }
    return frames;
}

QVector<ParsedFrame> FrameSplitter::push(const QByteArray &chunk, qint64 wallClockMs)
{
    m_buffer.append(chunk);
    if (m_buffer.size() > kScanWindow) {
        // 单次扫描上限，避免极端情况下界面卡顿
        m_buffer.truncate(kScanWindow);
    }
    return drain(wallClockMs, false);
}

QVector<ParsedFrame> FrameSplitter::flush(qint64 wallClockMs)
{
    return drain(wallClockMs, true);
}

} // namespace xcom
