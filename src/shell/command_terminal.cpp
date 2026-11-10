#include "shell/command_terminal.h"

#include "services/command_registry.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QVBoxLayout>

namespace xcom {
namespace {

QString argTypeName(CommandArgType type)
{
    switch (type) {
    case CommandArgType::Int:
        return QStringLiteral("int");
    case CommandArgType::Bool:
        return QStringLiteral("bool");
    case CommandArgType::Enum:
        return QStringLiteral("enum");
    case CommandArgType::SessionId:
        return QStringLiteral("session");
    case CommandArgType::Hex:
        return QStringLiteral("hex");
    case CommandArgType::Path:
        return QStringLiteral("path");
    case CommandArgType::String:
        return QStringLiteral("string");
    }
    return QStringLiteral("string");
}

} // namespace

CommandTerminal::CommandTerminal(CommandRegistry *commands, QWidget *parent)
    : QWidget(parent)
    , m_commands(commands)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    m_output->setMaximumBlockCount(2000);

    m_input = new QLineEdit(this);
    m_input->setFont(mono);
    m_input->setPlaceholderText(
        QStringLiteral("输入命令，例如：session.open / send --hex \"AA 55\" / sessions / help"));
    m_input->installEventFilter(this);

    layout->addWidget(m_output, 1);
    layout->addWidget(m_input);

    connect(m_input, &QLineEdit::returnPressed, this, &CommandTerminal::submit);

    auto *clearShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(clearShortcut, &QShortcut::activated, m_output, &QPlainTextEdit::clear);

    print(QStringLiteral("X-COM 命令终端。输入 help 查看命令，sessions 查看会话。"));
    print(QStringLiteral("提示：所有命令与命令面板、插件共用同一注册表。"));
}

void CommandTerminal::appendOutput(const QString &text)
{
    print(text);
}

void CommandTerminal::submit()
{
    const QString line = m_input->text().trimmed();
    if (line.isEmpty()) {
        return;
    }
    m_input->clear();
    print(QStringLiteral("> ") + line);

    if (m_history.isEmpty() || m_history.last() != line) {
        m_history.append(line);
    }
    m_historyIndex = m_history.size();

    bool ok = false;
    QStringList tokens = tokenize(line, &ok);
    if (!ok) {
        print(QStringLiteral("参数解析失败：引号未闭合"), true);
        return;
    }
    if (tokens.isEmpty()) {
        return;
    }

    const QString verb = tokens.takeFirst();
    if (verb == QStringLiteral("help")) {
        if (tokens.isEmpty()) {
            printCommandList();
        } else {
            printHelp(tokens.first());
        }
        return;
    }
    if (verb == QStringLiteral("clear")) {
        m_output->clear();
        return;
    }

    QString commandId = verb;
    if (verb == QStringLiteral("open")) {
        commandId = QStringLiteral("session.open");
    } else if (verb == QStringLiteral("close")) {
        commandId = QStringLiteral("session.close");
    } else if (verb == QStringLiteral("send")) {
        commandId = QStringLiteral("data.send");
    } else if (verb == QStringLiteral("sessions")) {
        commandId = QStringLiteral("session.list");
    }

    if (!m_commands || !m_commands->contains(commandId)) {
        print(QStringLiteral("未知命令：%1（输入 help 查看可用命令）").arg(commandId), true);
        return;
    }

    CommandSpec spec;
    for (const CommandSpec &candidate : m_commands->commands()) {
        if (candidate.id == commandId) {
            spec = candidate;
            break;
        }
    }

    QVariantMap args;
    QVector<QString> positionals;
    for (int i = 0; i < tokens.size(); ++i) {
        const QString token = tokens.at(i);
        if (!token.startsWith(QStringLiteral("--"))) {
            positionals.append(token);
            continue;
        }
        QString name = token.mid(2);
        bool boolValue = true;
        if (name.startsWith(QStringLiteral("no-"))) {
            name = name.mid(3);
            boolValue = false;
        }
        QVariant value = boolValue;
        if (i + 1 < tokens.size() && !tokens.at(i + 1).startsWith(QStringLiteral("--"))
            && !(spec.id == QStringLiteral("data.send") && name == QStringLiteral("text"))) {
            value = tokens.at(++i);
        }
        for (const CommandArg &arg : spec.args) {
            if (arg.name == name) {
                if (arg.type == CommandArgType::Bool) {
                    args.insert(name, boolValue);
                } else {
                    args.insert(name, coerce(value.toString(), arg));
                }
                name.clear();
                break;
            }
        }
        if (!name.isEmpty()) {
            args.insert(name, value);
        }
    }

    for (const QString &positional : positionals) {
        for (const CommandArg &arg : spec.args) {
            if (!args.contains(arg.name)) {
                args.insert(arg.name, coerce(positional, arg));
                break;
            }
        }
    }

    CommandInvocation invocation;
    invocation.id = commandId;
    invocation.args = args;
    invocation.source = QStringLiteral("terminal");

    CommandResult result;
    const bool success = m_commands->execute(invocation, &result);
    if (!result.message.isEmpty()) {
        print(result.message, !success);
    } else if (success) {
        print(QStringLiteral("完成"));
    }
    const QString text = result.data.value(QStringLiteral("text")).toString();
    if (!text.isEmpty()) {
        print(text);
    }
}

void CommandTerminal::print(const QString &text, bool error)
{
    const QString color = error ? QStringLiteral("#f48771") : QStringLiteral("#d4d4d4");
    QString escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    m_output->appendHtml(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, escaped));
}

void CommandTerminal::printCommandList()
{
    if (!m_commands) {
        return;
    }
    for (const CommandSpec &spec : m_commands->commands()) {
        if (spec.hidden) {
            continue;
        }
        print(QStringLiteral("%1  —  %2").arg(spec.id, spec.title));
    }
}

void CommandTerminal::printHelp(const QString &commandId)
{
    if (!m_commands) {
        return;
    }
    for (const CommandSpec &spec : m_commands->commands()) {
        if (spec.id != commandId) {
            continue;
        }
        print(QStringLiteral("%1（%2）").arg(spec.title, spec.id));
        if (!spec.category.isEmpty()) {
            print(QStringLiteral("分类：%1").arg(spec.category));
        }
        if (spec.args.isEmpty()) {
            print(QStringLiteral("无参数"));
        }
        for (const CommandArg &arg : spec.args) {
            print(QStringLiteral("  --%1 <%2>%3  %4")
                      .arg(arg.name, argTypeName(arg.type),
                           arg.required ? QStringLiteral("（必填）") : QString(),
                           arg.description));
        }
        return;
    }
    print(QStringLiteral("命令不存在：%1").arg(commandId), true);
}

QStringList CommandTerminal::tokenize(const QString &line, bool *ok) const
{
    QStringList tokens;
    QString current;
    bool inQuotes = false;
    QChar quote;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('\\') && i + 1 < line.size()) {
                const QChar next = line.at(i + 1);
                if (next == quote || next == QLatin1Char('\\')) {
                    current.append(next);
                    ++i;
                    continue;
                }
            }
            if (ch == quote) {
                inQuotes = false;
            } else {
                current.append(ch);
            }
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            inQuotes = true;
            quote = ch;
            continue;
        }
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                tokens.append(current);
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }

    if (inQuotes) {
        if (ok) {
            *ok = false;
        }
        return {};
    }
    if (!current.isEmpty()) {
        tokens.append(current);
    }
    if (ok) {
        *ok = true;
    }
    return tokens;
}

QVariant CommandTerminal::coerce(const QString &value, const CommandArg &arg) const
{
    switch (arg.type) {
    case CommandArgType::Int: {
        bool ok = false;
        const int number = value.toInt(&ok);
        return ok ? QVariant(number) : QVariant(value);
    }
    case CommandArgType::Bool:
        return value == QStringLiteral("true") || value == QStringLiteral("1");
    default:
        return value;
    }
}

bool CommandTerminal::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down) {
            if (m_history.isEmpty()) {
                return true;
            }
            if (keyEvent->key() == Qt::Key_Up) {
                m_historyIndex = qMax(0, m_historyIndex - 1);
            } else {
                m_historyIndex = qMin(m_history.size(), m_historyIndex + 1);
            }
            m_input->setText(m_historyIndex < m_history.size()
                                 ? m_history.at(m_historyIndex)
                                 : QString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace xcom
