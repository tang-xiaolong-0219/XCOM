#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace xcom {

// 分帧策略：接收字节流如何切分成一条条「命令」。
enum class FrameMode {
    Line,        // 按行解析：自动识别 \r\n、\n、\r
    Delimiter,   // 自定义分隔符：任意字节序列，例如 ';' 或 0x03
    FixedLength, // 固定长度：每 N 字节一帧
    Idle         // 空闲分帧：字节流出现间隔后才算一帧结束
};

QString frameModeLabel(FrameMode mode);
QString frameModeKey(FrameMode mode);
FrameMode frameModeFromKey(const QString &key, bool *ok = nullptr);

struct FrameSettings {
    FrameMode mode = FrameMode::Line;
    QByteArray delimiter = QByteArrayLiteral("\r\n");
    bool keepDelimiter = false;
    int fixedLength = 8;
    int maxFrameBytes = 4096;
    bool dropEmpty = true;
    bool trimWhitespace = true;
};

struct ParsedFrame {
    quint64 index = 0;
    qint64 wallClockMs = 0;
    QByteArray bytes;
    bool truncated = false;
};

// 与界面无关的流式分帧器：可脱离 QtWidgets 单测。
// 用法：push() 喂入接收块，返回已经完整的帧；flush() 收尾（空闲分帧/关闭会话）。
class FrameSplitter
{
public:
    void setSettings(const FrameSettings &settings);
    const FrameSettings &settings() const { return m_settings; }

    void reset();

    QVector<ParsedFrame> push(const QByteArray &chunk, qint64 wallClockMs);
    QVector<ParsedFrame> flush(qint64 wallClockMs);

    int bufferedBytes() const { return m_buffer.size(); }
    quint64 emittedCount() const { return m_index; }

    // "\r\n" / "\x03" 这类转义写法与原始字节互转，供界面输入框使用。
    static QByteArray decodeEscapes(const QString &text);
    static QString encodeEscapes(const QByteArray &bytes);

private:
    void emitFrame(QVector<ParsedFrame> *frames, QByteArray bytes, qint64 wallClockMs,
                   bool truncated, bool alreadyTrimmed = false);
    QVector<ParsedFrame> drain(qint64 wallClockMs, bool force);

    FrameSettings m_settings;
    QByteArray m_buffer;
    quint64 m_index = 0;
};

} // namespace xcom
