#include "serial/frame_splitter.h"

#include <QCoreApplication>
#include <QVector>

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    } else {
        std::printf("ok  : %s\n", what);
    }
}

QByteArray join(const QVector<xcom::ParsedFrame> &frames)
{
    QByteArray out;
    for (const xcom::ParsedFrame &frame : frames) {
        if (!out.isEmpty()) {
            out.append('|');
        }
        out.append(frame.bytes);
    }
    return out;
}

xcom::FrameSettings lineSettings()
{
    xcom::FrameSettings settings;
    settings.mode = xcom::FrameMode::Line;
    settings.keepDelimiter = false;
    return settings;
}

void testLineFraming()
{
    xcom::FrameSplitter splitter;
    splitter.setSettings(lineSettings());

    // \r\n 视为一个分隔符
    QVector<xcom::ParsedFrame> frames = splitter.push("AT+PING\r\nAT+OK\r\n", 1000);
    check(frames.size() == 2, "line: 两帧");
    check(frames.value(0).bytes == QByteArrayLiteral("AT+PING"), "line: 第一帧内容");
    check(frames.value(1).bytes == QByteArrayLiteral("AT+OK"), "line: 第二帧内容");
    check(frames.value(0).index == 1 && frames.value(1).index == 2, "line: 序号递增");
    check(frames.value(0).wallClockMs == 1000, "line: 时间戳透传");

    // \n 与 \r 单独出现也能切分
    frames = splitter.push("a\nb\rc\r\n", 1100);
    check(join(frames) == QByteArrayLiteral("a|b|c"), "line: \\n 与 \\r 都能切分");

    // 半帧跨块：先来一半，不应出帧
    frames = splitter.push("HEL", 1200);
    check(frames.isEmpty(), "line: 半帧不出帧");
    frames = splitter.push("LO\r\n", 1300);
    check(frames.size() == 1, "line: 补齐后出帧");
    check(frames.value(0).bytes == QByteArrayLiteral("HELLO"), "line: 跨块拼接正确");
}

void testKeepDelimiter()
{
    xcom::FrameSettings settings = lineSettings();
    settings.keepDelimiter = true;
    xcom::FrameSplitter splitter;
    splitter.setSettings(settings);

    const QVector<xcom::ParsedFrame> frames = splitter.push("AT\r\nOK\r\n", 1000);
    check(frames.size() == 2, "keepDelimiter: 两帧");
    check(frames.value(0).bytes.endsWith('\r'), "keepDelimiter: 保留 \\r");
    check(frames.value(0).bytes == QByteArrayLiteral("AT\r"),
          "keepDelimiter: \\r\\n 只保留一个字符");
}

void testCustomDelimiter()
{
    xcom::FrameSettings settings;
    settings.mode = xcom::FrameMode::Delimiter;
    settings.delimiter = QByteArrayLiteral(";");
    settings.trimWhitespace = true;

    xcom::FrameSplitter splitter;
    splitter.setSettings(settings);

    QVector<xcom::ParsedFrame> frames = splitter.push("AA;BB;CC", 1000);
    check(join(frames) == QByteArrayLiteral("AA|BB"), "delimiter: 分号切分");
    check(splitter.bufferedBytes() == 2, "delimiter: 尾帧留在缓冲");
    frames = splitter.flush(1100);
    check(join(frames) == QByteArrayLiteral("CC"), "delimiter: flush 取出尾帧");
    check(frames.value(0).truncated, "delimiter: flush 出帧标记截断");

    // 多字节分隔符 + 转义解码
    xcom::FrameSettings crlf;
    crlf.mode = xcom::FrameMode::Delimiter;
    crlf.delimiter = xcom::FrameSplitter::decodeEscapes(QStringLiteral("\\x03"));
    xcom::FrameSplitter binary;
    binary.setSettings(crlf);
    frames = binary.push(QByteArray::fromHex("01020304"), 1200);
    check(frames.size() == 1, "delimiter: \\x03 分隔符出帧");
    check(frames.value(0).bytes == QByteArray::fromHex("0102"), "delimiter: 二进制内容");
}

void testFixedLength()
{
    xcom::FrameSettings settings;
    settings.mode = xcom::FrameMode::FixedLength;
    settings.fixedLength = 4;
    settings.trimWhitespace = false;

    xcom::FrameSplitter splitter;
    splitter.setSettings(settings);

    QVector<xcom::ParsedFrame> frames = splitter.push(QByteArray::fromHex("0102030405060708"), 1000);
    check(frames.size() == 2, "fixed: 8 字节切成 2 帧");
    check(frames.value(0).bytes == QByteArray::fromHex("01020304"), "fixed: 第一帧");
    frames = splitter.push(QByteArray::fromHex("090a"), 1100);
    check(frames.isEmpty(), "fixed: 不足长度不出帧");
    frames = splitter.push(QByteArray::fromHex("0b0c"), 1200);
    check(frames.size() == 1, "fixed: 补足后出帧");
    check(frames.value(0).bytes == QByteArray::fromHex("090a0b0c"), "fixed: 跨块拼接");
}

void testIdleMode()
{
    xcom::FrameSettings settings;
    settings.mode = xcom::FrameMode::Idle;

    xcom::FrameSplitter splitter;
    splitter.setSettings(settings);

    QVector<xcom::ParsedFrame> frames = splitter.push("PART1", 1000);
    check(frames.isEmpty(), "idle: push 不出帧");
    frames = splitter.push("PART2", 1100);
    check(frames.isEmpty(), "idle: 持续累积");
    frames = splitter.flush(1200);
    check(frames.size() == 1, "idle: flush 出一帧");
    check(frames.value(0).bytes == QByteArrayLiteral("PART1PART2"), "idle: 整块内容");
}

void testDropEmptyAndTrim()
{
    xcom::FrameSettings settings = lineSettings();
    xcom::FrameSplitter splitter;
    splitter.setSettings(settings);

    QVector<xcom::ParsedFrame> frames = splitter.push("\r\n\r\n  AT  \r\n", 1000);
    check(frames.size() == 1, "dropEmpty: 空行被丢弃");
    check(frames.value(0).bytes == QByteArrayLiteral("AT"), "trim: 两端空白被去掉");

    settings.dropEmpty = false;
    settings.trimWhitespace = false;
    xcom::FrameSplitter keepAll;
    keepAll.setSettings(settings);
    frames = keepAll.push("  AT  \r\n\r\n", 1000);
    check(frames.size() == 2, "dropEmpty=false: 保留空帧");
    check(frames.value(0).bytes == QByteArrayLiteral("  AT  "), "trim=false: 保留空白");
}

void testOverflowGuard()
{
    xcom::FrameSettings settings;
    settings.mode = xcom::FrameMode::Delimiter;
    settings.delimiter = QByteArrayLiteral("\n");
    settings.maxFrameBytes = 64;

    xcom::FrameSplitter splitter;
    splitter.setSettings(settings);

    splitter.push(QByteArray(400, 'x'), 1000);
    check(splitter.bufferedBytes() <= 64, "overflow: 缓冲被截断到上限");
    const QVector<xcom::ParsedFrame> frames = splitter.flush(1100);
    check(frames.size() == 1, "overflow: flush 仍能出帧");
    check(frames.value(0).bytes.size() <= 64, "overflow: 帧长受上限约束");
}

void testModeKeys()
{
    bool ok = false;
    check(xcom::frameModeFromKey(QStringLiteral("line"), &ok) == xcom::FrameMode::Line && ok,
          "mode: line");
    check(xcom::frameModeFromKey(QStringLiteral("delimiter"), &ok) == xcom::FrameMode::Delimiter
              && ok,
          "mode: delimiter");
    check(xcom::frameModeFromKey(QStringLiteral("fixedLength"), &ok)
              == xcom::FrameMode::FixedLength
              && ok,
          "mode: fixedLength");
    check(xcom::frameModeFromKey(QStringLiteral("idle"), &ok) == xcom::FrameMode::Idle && ok,
          "mode: idle");
    check(xcom::frameModeFromKey(QStringLiteral("nope"), &ok) == xcom::FrameMode::Line && !ok,
          "mode: 未知回落 line");

    for (xcom::FrameMode mode : {xcom::FrameMode::Line, xcom::FrameMode::Delimiter,
                                 xcom::FrameMode::FixedLength, xcom::FrameMode::Idle}) {
        bool roundTrip = false;
        const xcom::FrameMode parsed = xcom::frameModeFromKey(xcom::frameModeKey(mode), &roundTrip);
        check(parsed == mode && roundTrip, "mode: key 往返一致");
    }
}

void testEscapes()
{
    check(xcom::FrameSplitter::decodeEscapes(QStringLiteral("\\r\\n")) == QByteArrayLiteral("\r\n"),
          "escape: \\r\\n");
    check(xcom::FrameSplitter::decodeEscapes(QStringLiteral("\\x03")) == QByteArray(1, '\x03'),
          "escape: \\x03");
    check(xcom::FrameSplitter::decodeEscapes(QStringLiteral("A\\\\B")) == QByteArrayLiteral("A\\B"),
          "escape: 反斜杠");
    check(xcom::FrameSplitter::encodeEscapes(QByteArrayLiteral("\r\n")) == QStringLiteral("\\r\\n"),
          "escape: encode \\r\\n");
    check(xcom::FrameSplitter::decodeEscapes(xcom::FrameSplitter::encodeEscapes(
              QByteArray::fromHex("00ff")))
              == QByteArray::fromHex("00ff"),
          "escape: encode/decode 往返");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testLineFraming();
    testKeepDelimiter();
    testCustomDelimiter();
    testFixedLength();
    testIdleMode();
    testDropEmptyAndTrim();
    testOverflowGuard();
    testModeKeys();
    testEscapes();

    std::printf("=== %s（%d 项检查，失败 %d 项）===\n",
                g_failures == 0 ? "全部通过" : "存在失败", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
