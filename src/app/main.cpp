#include "foundation/log.h"
#include "serial/session_service.h"
#include "services/command_registry.h"
#include "services/core_commands.h"
#include "services/event_bus.h"
#include "services/logger.h"
#include "services/plugin_host.h"
#include "services/settings_service.h"
#include "shell/main_window.h"
#include "shell/notification_hub.h"
#include "shell/theme.h"
#include "shell/ui_context.h"
#include "xcom/api/version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("X-COM"));
    QApplication::setOrganizationDomain(QStringLiteral("xcom.local"));
    QApplication::setApplicationName(QStringLiteral("X-COM"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setApplicationDisplayName(QStringLiteral("X-COM 串口工作台"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("X-COM 串口工作台"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption portOption(QStringLiteral("port"),
                                        QStringLiteral("启动后打开指定串口"), QStringLiteral("port"));
    const QCommandLineOption baudOption(QStringLiteral("baud"),
                                        QStringLiteral("波特率，默认 115200"),
                                        QStringLiteral("baud"), QStringLiteral("115200"));
    const QCommandLineOption loopbackOption(QStringLiteral("loopback"),
                                            QStringLiteral("启动后打开一个回环会话（无需硬件）"));
    parser.addOption(portOption);
    parser.addOption(baudOption);
    parser.addOption(loopbackOption);
    parser.process(app);

    const QString dataDir = xcom::appDataDir();
    xcom::initLogging(xcom::appLogDir());

    // 插件与主程序同目录部署 xcom_api.dll，把应用目录加入 PATH 便于插件解析依赖。
    QByteArray searchPath = QCoreApplication::applicationDirPath().toLocal8Bit();
    searchPath += ';';
    searchPath += qgetenv("PATH");
    qputenv("PATH", searchPath);

    xcom::applyTheme();

    xcom::EventBus bus;
    xcom::SettingsService settings(dataDir + QStringLiteral("/settings.json"));
    xcom::Logger logger(QStringLiteral("app"), &bus);
    xcom::CommandRegistry commands;
    xcom::SessionService sessions(&bus);
    xcom::UiContext ui;
    xcom::NotificationHub notifications;

    // 核心命令先注册，插件随后可以覆盖/复用同名命令前的查询。
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
    environment.notifications = &notifications;
    environment.ui = &ui;

    xcom::PluginHost plugins(environment);
    plugins.setSearchPaths({QCoreApplication::applicationDirPath() + QStringLiteral("/plugins")});
    plugins.scanAndLoad();
    logger.info(QStringLiteral("API %1，已激活插件 %2 个")
                    .arg(xcom::apiVersion())
                    .arg(plugins.activeCount()));

    xcom::MainWindow window(&commands, &sessions, &settings, &bus, &plugins, &ui,
                            &notifications);
    window.show();

    // 启动参数：在窗口就绪后开会话，复现 IDE 的「从命令行打开目标」体验。
    const bool wantLoopback = parser.isSet(loopbackOption);
    const QString wantPort = parser.value(portOption);
    if (wantLoopback || !wantPort.isEmpty()) {
        QVariantMap args;
        args.insert(QStringLiteral("port"), wantPort);
        args.insert(QStringLiteral("baud"), parser.value(baudOption));
        const QString commandId = wantLoopback ? QStringLiteral("session.openLoopback")
                                               : QStringLiteral("session.open");
        QTimer::singleShot(0, &window, [&window, commandId, args] {
            window.runCommand(commandId, args);
        });
    }

    const int exitCode = app.exec();

    // 先停用插件并撤销其视图/命令，再让窗口析构，避免注册表持有已销毁控件。
    plugins.shutdownAll();
    for (xcom::ISubscription *subscription : coreSubscriptions) {
        if (subscription) {
            subscription->unsubscribe();
        }
    }
    settings.sync();
    return exitCode;
}
