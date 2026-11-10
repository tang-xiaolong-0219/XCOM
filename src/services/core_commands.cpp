#include "services/core_commands.h"

#include <QVariantList>

namespace xcom {

bool parseHexBytes(const QString &text, QByteArray *out, QString *error)
{
    if (!out) {
        return false;
    }
    QString digits;
    digits.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch.isSpace() || ch == QLatin1Char(',') || ch == QLatin1Char(';')
            || ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            continue;
        }
        if (ch == QLatin1Char('0') && i + 1 < text.size()
            && (text.at(i + 1) == QLatin1Char('x') || text.at(i + 1) == QLatin1Char('X'))) {
            ++i;
            continue;
        }
        const QChar upper = ch.toUpper();
        const bool isHexDigit =
            ch.isDigit() || (upper >= QLatin1Char('A') && upper <= QLatin1Char('F'));
        if (!isHexDigit) {
            if (error) {
                *error = QStringLiteral("包含非法字符 %1").arg(ch);
            }
            return false;
        }
        digits.append(ch);
    }
    if (digits.size() % 2 != 0) {
        if (error) {
            *error = QStringLiteral("十六进制数字个数为奇数");
        }
        return false;
    }
    out->clear();
    out->reserve(digits.size() / 2);
    for (int i = 0; i < digits.size(); i += 2) {
        bool ok = false;
        const int value = digits.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("无法解析 %1").arg(digits.mid(i, 2));
            }
            return false;
        }
        out->append(static_cast<char>(value));
    }
    return true;
}

CommandSpec DataSendHandler::spec()
{
    CommandSpec spec;
    spec.id = QStringLiteral("data.send");
    spec.title = QStringLiteral("发送数据");
    spec.category = QStringLiteral("发送");
    spec.args = {CommandArg{QStringLiteral("session"), CommandArgType::SessionId, false, {},
                            QStringLiteral("会话 id 或名称，留空表示当前会话")},
                 CommandArg{QStringLiteral("text"), CommandArgType::String, true, {},
                            QStringLiteral("要发送的内容")},
                 CommandArg{QStringLiteral("hex"), CommandArgType::Bool, false, false,
                            QStringLiteral("按十六进制解析")},
                 CommandArg{QStringLiteral("newline"), CommandArgType::Bool, false, false,
                            QStringLiteral("追加换行")}};
    return spec;
}

CommandResult DataSendHandler::execute(const CommandInvocation &invocation,
                                       ICommandProgress *progress)
{
    Q_UNUSED(progress)
    if (!m_sessions) {
        return CommandResult::failure(QStringLiteral("会话服务不可用"));
    }

    const QString reference = invocation.args.value(QStringLiteral("session")).toString();
    ISession *session = m_sessions->find(reference.isEmpty() ? QStringLiteral("active") : reference);
    if (!session) {
        return CommandResult::failure(
            QStringLiteral("未找到会话：%1")
                .arg(reference.isEmpty() ? QStringLiteral("当前会话") : reference));
    }

    const QString text = invocation.args.value(QStringLiteral("text")).toString();
    if (text.isEmpty()) {
        return CommandResult::failure(QStringLiteral("发送内容为空"));
    }

    QByteArray payload;
    if (invocation.args.value(QStringLiteral("hex")).toBool()) {
        QString error;
        if (!parseHexBytes(text, &payload, &error)) {
            return CommandResult::failure(QStringLiteral("HEX 格式错误：%1").arg(error));
        }
    } else {
        payload = text.toUtf8();
    }
    if (invocation.args.value(QStringLiteral("newline")).toBool()) {
        payload.append("\r\n", 2);
    }
    if (payload.isEmpty()) {
        return CommandResult::failure(QStringLiteral("发送内容为空"));
    }

    Error error;
    if (!session->send(payload, &error)) {
        return CommandResult::failure(error.message);
    }

    QVariantMap data;
    data.insert(QStringLiteral("session"), session->id());
    data.insert(QStringLiteral("bytes"), payload.size());
    return CommandResult::success(QStringLiteral("已发送 %1 字节").arg(payload.size()), data);
}

CommandSpec SessionListHandler::spec()
{
    CommandSpec spec;
    spec.id = QStringLiteral("session.list");
    spec.title = QStringLiteral("列出会话");
    spec.category = QStringLiteral("会话");
    return spec;
}

CommandResult SessionListHandler::execute(const CommandInvocation &invocation,
                                          ICommandProgress *progress)
{
    Q_UNUSED(invocation)
    Q_UNUSED(progress)
    QStringList lines;
    QVariantList items;
    if (m_sessions) {
        for (const SessionInfo &info : m_sessions->list()) {
            lines.append(QStringLiteral("%1 %2  %3  %4  RX %5 / TX %6")
                             .arg(sessionStateIcon(info.state), info.name, info.id,
                                  sessionStateName(info.state))
                             .arg(info.rxBytes)
                             .arg(info.txBytes));
            QVariantMap item;
            item.insert(QStringLiteral("id"), info.id);
            item.insert(QStringLiteral("name"), info.name);
            item.insert(QStringLiteral("state"), int(info.state));
            item.insert(QStringLiteral("port"), info.config.portName);
            item.insert(QStringLiteral("baud"), info.config.baudRate);
            item.insert(QStringLiteral("rxBytes"), QVariant::fromValue(info.rxBytes));
            item.insert(QStringLiteral("txBytes"), QVariant::fromValue(info.txBytes));
            items.append(item);
        }
    }
    if (lines.isEmpty()) {
        lines.append(QStringLiteral("(没有会话)"));
    }
    QVariantMap data;
    data.insert(QStringLiteral("text"), lines.join(QLatin1Char('\n')));
    data.insert(QStringLiteral("sessions"), items);
    return CommandResult::success(lines.join(QLatin1Char('\n')), data);
}

} // namespace xcom
