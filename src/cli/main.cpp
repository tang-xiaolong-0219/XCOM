#include "foundation/log.h"
#include "serial/session_service.h"
#include "services/command_registry.h"
#include "services/core_commands.h"
#include "services/event_bus.h"
#include "services/logger.h"
#include "services/plugin_host.h"
#include "services/settings_service.h"
#include "xcom/api/version.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSerialPortInfo>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

struct ExitCode {
    enum Value { Ok = 0, Failure = 1, Usage = 2, Serial = 3, Script = 4, Plugin = 5 };
};

// 手写参数解析：避免 QCommandLineParser 的全局选项吞掉插件命令的同名参数。
class Args
{
public:
    void addToken(const QString &token)
    {
        if (token.startsWith(QStringLiteral("--"))) {
            QString name = token.mid(2);
            m_lastOption = name;
            m_pending[name] = QString();
            m_present.append(name);
        } else if (!m_lastOption.isEmpty() && m_pending.value(m_lastOption).isEmpty()) {
            m_pending[m_lastOption] = token;
            m_lastOption.clear();
        } else {
            m_positional.append(token);
        }
    }

    bool has(const QString &name) const { return m_present.contains(name); }

    QString value(const QString &name, const QString &fallback = QString()) const
    {
        const QString value = m_pending.value(name);
        return value.isEmpty() ? fallback : value;
    }

    QStringList positional() const { return m_positional; }

private:
    QHash<QString, QString> m_pending;
    QStringList m_present;
    QStringList m_positional;
    QString m_lastOption;
};

class CliContext
{
public:
    xcom::EventBus *bus = nullptr;
    xcom::SessionService *sessions = nullptr;
    xcom::CommandRegistry *commands = nullptr;
    xcom::SettingsService *settings = nullptr;
    xcom::PluginHost *plugins = nullptr;
    bool json = false;
};

int emitError(int code, const QString &message, bool json)
{
    if (json) {
        QJsonObject error;
        error.insert(QStringLiteral("code"), code);
        error.insert(QStringLiteral("message"), message);
        QJsonObject root;
        root.insert(QStringLiteral("ok"), false);
        root.insert(QStringLiteral("error"), error);
        out() << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)) << '\n';
    } else {
        err() << message << '\n';
    }
    out().flush();
    err().flush();
    return code;
}

void printJson(const QJsonObject &object)
{
    out() << QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) << '\n';
    out().flush();
}

void printLine(const QString &text)
{
    out() << text << '\n';
    out().flush();
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}

int codeForError(xcom::ErrorCode code)
{
    switch (code) {
    case xcom::ErrorCode::InvalidArgument:
        return ExitCode::Usage;
    case xcom::ErrorCode::PortNotFound:
    case xcom::ErrorCode::PortBusy:
    case xcom::ErrorCode::AccessDenied:
    case xcom::ErrorCode::DeviceRemoved:
    case xcom::ErrorCode::OpenTimeout:
    case xcom::ErrorCode::WriteTimeout:
    case xcom::ErrorCode::NotOpen:
        return ExitCode::Serial;
    case xcom::ErrorCode::PluginFailed:
        return ExitCode::Plugin;
    default:
        return ExitCode::Failure;
    }
}

QJsonObject sessionToJson(const xcom::SessionInfo &info)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), info.id);
    object.insert(QStringLiteral("name"), info.name);
    object.insert(QStringLiteral("transport"), info.transport);
    object.insert(QStringLiteral("port"), info.config.portName);
    object.insert(QStringLiteral("baud"), info.config.baudRate);
    object.insert(QStringLiteral("state"), xcom::sessionStateName(info.state));
    object.insert(QStringLiteral("rxBytes"), static_cast<double>(info.rxBytes));
    object.insert(QStringLiteral("txBytes"), static_cast<double>(info.txBytes));
    return object;
}

int cmdPorts(CliContext &context)
{
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if (context.json) {
        QJsonArray array;
        for (const QSerialPortInfo &info : ports) {
            QJsonObject object;
            object.insert(QStringLiteral("port"), info.portName());
            object.insert(QStringLiteral("description"), info.description());
            object.insert(QStringLiteral("manufacturer"), info.manufacturer());
            object.insert(QStringLiteral("serialNumber"), info.serialNumber());
            object.insert(QStringLiteral("vendorId"),
                          info.hasVendorIdentifier() ? int(info.vendorIdentifier()) : 0);
            object.insert(QStringLiteral("productId"),
                          info.hasProductIdentifier() ? int(info.productIdentifier()) : 0);
            array.append(object);
        }
        QJsonObject root;
        root.insert(QStringLiteral("ok"), true);
        root.insert(QStringLiteral("count"), array.size());
        root.insert(QStringLiteral("ports"), array);
        printJson(root);
        return ExitCode::Ok;
    }

    if (ports.isEmpty()) {
        printLine(QStringLiteral("(未检测到串口)"));
        return ExitCode::Ok;
    }
    for (const QSerialPortInfo &info : ports) {
        printLine(QStringLiteral("%1  %2  %3")
                      .arg(info.portName().leftJustified(10), info.description(),
                           info.manufacturer()));
    }
    return ExitCode::Ok;
}

int cmdSessions(CliContext &context)
{
    const QVector<xcom::SessionInfo> sessions = context.sessions->list();
    if (context.json) {
        QJsonArray array;
        for (const xcom::SessionInfo &info : sessions) {
            array.append(sessionToJson(info));
        }
        QJsonObject root;
        root.insert(QStringLiteral("ok"), true);
        root.insert(QStringLiteral("count"), array.size());
        root.insert(QStringLiteral("sessions"), array);
        printJson(root);
        return ExitCode::Ok;
    }
    if (sessions.isEmpty()) {
        printLine(QStringLiteral("(当前进程没有会话)"));
    }
    for (const xcom::SessionInfo &info : sessions) {
        printLine(QStringLiteral("%1 %2  %3  %4")
                      .arg(info.id, info.name, info.config.portName,
                           xcom::sessionStateName(info.state)));
    }
    return ExitCode::Ok;
}

bool openTransient(CliContext &context, const Args &args, QString *sessionId)
{
    xcom::SessionDescriptor descriptor;
    const QString port = args.value(QStringLiteral("port"));
    const bool loopback = args.has(QStringLiteral("loopback"))
                          || port.compare(QStringLiteral("loopback"), Qt::CaseInsensitive) == 0;
    if (loopback) {
        descriptor.transport = QStringLiteral("loopback");
        descriptor.config.portName = QStringLiteral("LOOPBACK");
        descriptor.name = QStringLiteral("cli-loopback");
    } else {
        if (port.isEmpty()) {
            return false;
        }
        descriptor.transport = QStringLiteral("serial");
        descriptor.config.portName = port;
        descriptor.name = port;
    }

    descriptor.config.baudRate = args.value(QStringLiteral("baud")).toInt();
    if (descriptor.config.baudRate <= 0) {
        descriptor.config.baudRate = 115200;
    }

    xcom::Error error;
    if (!context.sessions->open(descriptor, sessionId, &error)) {
        return false;
    }
    return waitFor(
        [&] {
            xcom::ISession *session = context.sessions->find(*sessionId);
            return !session || session->state() == xcom::SessionState::Open
                   || session->state() == xcom::SessionState::Failed;
        },
        5000);
}

int cmdSend(CliContext &context, const Args &args)
{
    const QString text = args.value(QStringLiteral("text"));
    const QString hex = args.value(QStringLiteral("hex"));
    if (text.isEmpty() && hex.isEmpty()) {
        return emitError(ExitCode::Usage, QStringLiteral("需要 --text 或 --hex"), context.json);
    }

    QString sessionId;
    if (!openTransient(context, args, &sessionId)) {
        return emitError(ExitCode::Serial,
                         QStringLiteral("无法打开会话：请检查 --port 或改用 --loopback"),
                         context.json);
    }

    xcom::ISession *session = context.sessions->find(sessionId);
    if (!session || session->state() != xcom::SessionState::Open) {
        context.sessions->close(sessionId);
        return emitError(ExitCode::Serial, QStringLiteral("会话未能打开"), context.json);
    }

    QByteArray payload;
    if (!hex.isEmpty()) {
        QString parseError;
        if (!xcom::parseHexBytes(hex, &payload, &parseError) || payload.isEmpty()) {
            context.sessions->close(sessionId);
            return emitError(ExitCode::Usage,
                             QStringLiteral("--hex 格式错误：%1").arg(parseError),
                             context.json);
        }
    } else {
        payload = text.toUtf8();
    }
    if (args.has(QStringLiteral("newline"))) {
        payload.append("\r\n", 2);
    }

    xcom::Error error;
    const bool ok = session->send(payload, &error);
    const qint64 written = ok ? payload.size() : 0;

    // 回环模式下等数据回来，方便脚本断言
    if (ok && args.has(QStringLiteral("loopback"))) {
        waitFor([&] { return session->stats().rxBytes > 0; }, 1000);
    }

    const xcom::SessionStats stats = session->stats();
    context.sessions->close(sessionId);

    if (context.json) {
        QJsonObject root;
        root.insert(QStringLiteral("ok"), ok);
        root.insert(QStringLiteral("sentBytes"), static_cast<double>(written));
        root.insert(QStringLiteral("echoedBytes"), static_cast<double>(stats.rxBytes));
        root.insert(QStringLiteral("message"),
                    ok ? QStringLiteral("已发送") : error.message);
        printJson(root);
    } else if (ok) {
        printLine(QStringLiteral("已发送 %1 字节").arg(written));
    } else {
        err() << error.message << '\n';
        err().flush();
    }
    return ok ? ExitCode::Ok : codeForError(error.code);
}

int cmdMonitor(CliContext &context, const Args &args)
{
    QString sessionId;
    if (!openTransient(context, args, &sessionId)) {
        return emitError(ExitCode::Serial,
                         QStringLiteral("无法打开会话：请检查 --port 或改用 --loopback"),
                         context.json);
    }

    xcom::ISession *session = context.sessions->find(sessionId);
    if (!session || session->state() != xcom::SessionState::Open) {
        context.sessions->close(sessionId);
        return emitError(ExitCode::Serial, QStringLiteral("会话未能打开"), context.json);
    }

    class Sink : public xcom::ISessionDataSink
    {
    public:
        bool hex = false;
        bool json = false;

        void onData(const xcom::DataChunk &chunk) override
        {
            if (chunk.direction != xcom::Direction::Rx) {
                return;
            }
            if (json) {
                QJsonObject object;
                object.insert(QStringLiteral("type"), QStringLiteral("data"));
                object.insert(QStringLiteral("timestampMs"),
                              static_cast<double>(chunk.wallClockMs));
                object.insert(QStringLiteral("hex"), QString::fromLatin1(chunk.bytes.toHex()));
                object.insert(QStringLiteral("text"), QString::fromUtf8(chunk.bytes));
                printJson(object);
                return;
            }
            if (hex) {
                printLine(QString::fromLatin1(chunk.bytes.toHex(' ')).toUpper());
            } else {
                out() << QString::fromUtf8(chunk.bytes);
                out().flush();
            }
        }
    };

    Sink sink;
    sink.hex = args.has(QStringLiteral("hex"));
    sink.json = context.json;
    xcom::ISubscription *subscription = session->subscribe(&sink);

    const QString durationText = args.value(QStringLiteral("duration"), QStringLiteral("10s"));
    QString normalized = durationText.trimmed().toLower();
    double factor = 1.0;
    if (normalized.endsWith(QStringLiteral("ms"))) {
        normalized.chop(2);
    } else if (normalized.endsWith(QLatin1Char('s'))) {
        normalized.chop(1);
        factor = 1000.0;
    } else if (normalized.endsWith(QLatin1Char('m'))) {
        normalized.chop(1);
        factor = 60000.0;
    }
    bool ok = false;
    const double value = normalized.toDouble(&ok);
    if (!ok || value <= 0) {
        subscription->unsubscribe();
        context.sessions->close(sessionId);
        return emitError(ExitCode::Usage, QStringLiteral("--duration 格式错误"), context.json);
    }
    const int durationMs = static_cast<int>(value * factor);

    // --hex 在没有 --text 时作为「显示 HEX」开关使用；有 --text 时先发送一次再监听。
    const QString text = args.value(QStringLiteral("text"));
    if (!text.isEmpty()) {
        QByteArray payload;
        if (args.has(QStringLiteral("hex-send"))) {
            QString parseError;
            xcom::parseHexBytes(text, &payload, &parseError);
        } else {
            payload = text.toUtf8();
        }
        if (args.has(QStringLiteral("newline"))) {
            payload.append("\r\n", 2);
        }
        if (!payload.isEmpty()) {
            session->send(payload, nullptr);
        }
    }

    QEventLoop loop;
    QTimer::singleShot(durationMs, &loop, &QEventLoop::quit);
    loop.exec();

    subscription->unsubscribe();
    const xcom::SessionStats stats = session->stats();
    context.sessions->close(sessionId);

    if (context.json) {
        QJsonObject root;
        root.insert(QStringLiteral("ok"), true);
        root.insert(QStringLiteral("type"), QStringLiteral("summary"));
        root.insert(QStringLiteral("rxBytes"), static_cast<double>(stats.rxBytes));
        root.insert(QStringLiteral("txBytes"), static_cast<double>(stats.txBytes));
        root.insert(QStringLiteral("durationMs"), durationMs);
        printJson(root);
    } else {
        printLine(QStringLiteral("\n[接收 %1 字节 · 发送 %2 字节]")
                      .arg(stats.rxBytes)
                      .arg(stats.txBytes));
    }
    return ExitCode::Ok;
}

int cmdPlugins(CliContext &context, const QString &action)
{
    if (!action.isEmpty() && action != QStringLiteral("list")) {
        return emitError(ExitCode::Usage,
                         QStringLiteral("本版本只支持 plugins list；启停请使用图形界面"),
                         context.json);
    }

    const QVector<xcom::PluginInfo> plugins = context.plugins->plugins();
    if (context.json) {
        QJsonArray array;
        for (const xcom::PluginInfo &info : plugins) {
            QJsonObject object;
            object.insert(QStringLiteral("id"), info.id);
            object.insert(QStringLiteral("name"), info.name);
            object.insert(QStringLiteral("version"), info.version);
            object.insert(QStringLiteral("apiVersion"), info.apiVersion);
            object.insert(QStringLiteral("state"), info.state);
            object.insert(QStringLiteral("enabled"), info.enabled);
            object.insert(QStringLiteral("error"), info.error);
            object.insert(QStringLiteral("contributions"), info.contributions);
            array.append(object);
        }
        QJsonObject root;
        root.insert(QStringLiteral("ok"), true);
        root.insert(QStringLiteral("count"), array.size());
        root.insert(QStringLiteral("plugins"), array);
        printJson(root);
        return ExitCode::Ok;
    }

    if (plugins.isEmpty()) {
        printLine(QStringLiteral("(没有发现插件)"));
    }
    for (const xcom::PluginInfo &info : plugins) {
        printLine(QStringLiteral("%1  %2  %3  %4")
                      .arg(info.id.leftJustified(28), info.version.leftJustified(8),
                           info.state.leftJustified(12),
                           info.error.isEmpty() ? info.name : info.error));
    }
    return ExitCode::Ok;
}

int cmdDoctor(CliContext &context)
{
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();

    if (context.json) {
        QJsonObject root;
        root.insert(QStringLiteral("ok"), true);
        root.insert(QStringLiteral("hostVersion"), QCoreApplication::applicationVersion());
        root.insert(QStringLiteral("apiVersion"), xcom::apiVersion());
        root.insert(QStringLiteral("apiHash"), xcom::apiHash());
        root.insert(QStringLiteral("qtVersion"), QString::fromLatin1(qVersion()));
        root.insert(QStringLiteral("dataDir"), xcom::appDataDir());
        root.insert(QStringLiteral("logDir"), xcom::appLogDir());
        root.insert(QStringLiteral("settingsFile"), context.settings->filePath());
        root.insert(QStringLiteral("portCount"), ports.size());
        root.insert(QStringLiteral("activePlugins"), context.plugins->activeCount());
        QJsonArray errors;
        for (const QString &message : context.plugins->errors()) {
            errors.append(message);
        }
        root.insert(QStringLiteral("pluginErrors"), errors);
        printJson(root);
        return ExitCode::Ok;
    }

    printLine(QStringLiteral("宿主版本   : %1").arg(QCoreApplication::applicationVersion()));
    printLine(QStringLiteral("API 版本   : %1 (%2)").arg(xcom::apiVersion(), xcom::apiHash()));
    printLine(QStringLiteral("Qt 版本    : %1").arg(QString::fromLatin1(qVersion())));
    printLine(QStringLiteral("数据目录   : %1").arg(xcom::appDataDir()));
    printLine(QStringLiteral("日志目录   : %1").arg(xcom::appLogDir()));
    printLine(QStringLiteral("串口数量   : %1").arg(ports.size()));
    printLine(QStringLiteral("活动插件   : %1").arg(context.plugins->activeCount()));
    const QStringList errors = context.plugins->errors();
    if (!errors.isEmpty()) {
        printLine(QStringLiteral("插件错误   :"));
        for (const QString &message : errors) {
            printLine(QStringLiteral("  - %1").arg(message));
        }
    }
    return ExitCode::Ok;
}

int cmdPluginCommand(CliContext &context, const QString &commandId, const Args &args)
{
    xcom::CommandSpec spec;
    bool found = false;
    for (const xcom::CommandSpec &candidate : context.commands->commands()) {
        if (candidate.id == commandId) {
            spec = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        return emitError(ExitCode::Usage, QStringLiteral("未知命令：%1").arg(commandId),
                         context.json);
    }

    // 按 schema 把位置参数映射到未赋值的参数上，并做基本类型转换。
    QVariantMap values;
    QStringList positionals = args.positional();
    for (const xcom::CommandArg &arg : spec.args) {
        if (args.has(arg.name)) {
            QVariant value = args.value(arg.name);
            if (arg.type == xcom::CommandArgType::Int) {
                value = value.toInt();
            } else if (arg.type == xcom::CommandArgType::Bool) {
                value = true;
            }
            values.insert(arg.name, value);
        } else if (!positionals.isEmpty()) {
            values.insert(arg.name, positionals.takeFirst());
        } else if (arg.required) {
            return emitError(ExitCode::Usage,
                             QStringLiteral("缺少必填参数 --%1").arg(arg.name), context.json);
        }
    }

    xcom::CommandInvocation invocation;
    invocation.id = commandId;
    invocation.args = values;
    invocation.source = QStringLiteral("cli");

    xcom::CommandResult result;
    const bool ok = context.commands->execute(invocation, &result);

    if (context.json) {
        QJsonObject root;
        root.insert(QStringLiteral("ok"), ok);
        root.insert(QStringLiteral("message"), result.message);
        root.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(result.data));
        printJson(root);
    } else if (!result.message.isEmpty()) {
        (ok ? out() : err()) << result.message << '\n';
        out().flush();
        err().flush();
    }
    return ok ? ExitCode::Ok : ExitCode::Failure;
}

void printUsage()
{
    printLine(QStringLiteral(
        "X-COM 无界面串口工具\n"
        "\n"
        "用法: xcom-cli <命令> [参数...]\n"
        "\n"
        "命令:\n"
        "  ports                     列出串口（--json）\n"
        "  send --text <文本>        打开会话、发送、退出\n"
        "  send --hex <十六进制>     按十六进制发送\n"
        "  monitor --duration 10s    监听接收数据\n"
        "  sessions                  列出当前进程会话\n"
        "  plugins list              列出插件\n"
        "  doctor                    环境诊断\n"
        "  <插件命令 id> [--arg v]   执行插件注册的命令\n"
        "\n"
        "通用参数:\n"
        "  --json                    以 JSON 输出\n"
        "  --port COM3               目标串口\n"
        "  --baud 115200             波特率\n"
        "  --loopback                使用回环虚拟串口（无需硬件）\n"
        "  --newline                 发送时追加 \\r\\n\n"
        "\n"
        "退出码: 0 成功 / 1 通用失败 / 2 参数错误 / 3 串口错误 / 5 插件错误"));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("X-COM"));
    QCoreApplication::setApplicationName(QStringLiteral("X-COM"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    xcom::initLogging(xcom::appLogDir());

    QStringList raw;
    for (int i = 1; i < argc; ++i) {
        raw.append(QString::fromLocal8Bit(argv[i]));
    }
    if (raw.isEmpty() || raw.first() == QStringLiteral("--help") || raw.first() == QStringLiteral("-h")) {
        printUsage();
        return ExitCode::Ok;
    }

    const QString command = raw.takeFirst();
    Args args;
    for (const QString &token : raw) {
        args.addToken(token);
    }

    CliContext context;
    context.json = args.has(QStringLiteral("json"));

    xcom::EventBus bus;
    xcom::SettingsService settings(xcom::appDataDir() + QStringLiteral("/settings.json"));
    xcom::Logger logger(QStringLiteral("cli"), &bus);
    xcom::CommandRegistry commands;
    xcom::SessionService sessions(&bus);

    context.bus = &bus;
    context.settings = &settings;
    context.commands = &commands;
    context.sessions = &sessions;

    // CLI 与图形界面共用同一份核心命令实现（data.send / session.list）。
    xcom::DataSendHandler dataSendHandler(&sessions);
    xcom::SessionListHandler sessionListHandler(&sessions);
    QVector<xcom::ISubscription *> coreSubscriptions;
    coreSubscriptions << commands.registerCommand(xcom::DataSendHandler::spec(), &dataSendHandler)
                      << commands.registerCommand(xcom::SessionListHandler::spec(),
                                                  &sessionListHandler);

    xcom::PluginHost::Environment environment;
    environment.commands = &commands;
    environment.sessions = &sessions;
    environment.settings = &settings;
    environment.events = &bus;
    environment.logger = &logger;
    environment.notifications = nullptr;
    environment.ui = nullptr;

    xcom::PluginHost plugins(environment);
    plugins.setSearchPaths({QCoreApplication::applicationDirPath() + QStringLiteral("/plugins")});
    plugins.scanAndLoad();
    context.plugins = &plugins;

    const QString lowered = command.toLower();
    int exitCode = ExitCode::Ok;

    if (lowered == QStringLiteral("ports")) {
        exitCode = cmdPorts(context);
    } else if (lowered == QStringLiteral("sessions")) {
        exitCode = cmdSessions(context);
    } else if (lowered == QStringLiteral("send")) {
        exitCode = cmdSend(context, args);
    } else if (lowered == QStringLiteral("monitor")) {
        exitCode = cmdMonitor(context, args);
    } else if (lowered == QStringLiteral("plugins")) {
        exitCode = cmdPlugins(context, args.positional().value(0).toLower());
    } else if (lowered == QStringLiteral("doctor")) {
        exitCode = cmdDoctor(context);
    } else if (lowered == QStringLiteral("help")) {
        printUsage();
    } else {
        exitCode = cmdPluginCommand(context, command, args);
    }

    plugins.shutdownAll();
    for (xcom::ISubscription *subscription : coreSubscriptions) {
        if (subscription) {
            subscription->unsubscribe();
        }
    }
    settings.sync();
    return exitCode;
}
