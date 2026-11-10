#pragma once

#include "xcom/api/commands.h"
#include "xcom/api/events.h"

#include <QMainWindow>
#include <QHash>
#include <QPoint>
#include <QRect>
#include <QVector>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QAction;
class QDockWidget;
class QEvent;
class QKeySequence;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QShowEvent;
class QTabWidget;
class QTimer;
class QToolBar;
QT_END_NAMESPACE

namespace xcom {

class CommandPalette;
class CommandLineView;
class CommandRegistry;
class ConsoleView;
class EventBus;
class FrameParserView;
class IViewFactory;
class ISession;
class NotificationHub;
class PluginHost;
class SessionService;
class SessionWorkspace;
class SettingsService;
class UiContext;
class ViewHost;
class SessionDockWidget;

class MainWindow : public QMainWindow, public IEventHandler
{
    Q_OBJECT

public:
    MainWindow(CommandRegistry *commands, SessionService *sessions, SettingsService *settings,
               EventBus *bus, PluginHost *plugins, UiContext *ui, NotificationHub *notifications,
               QWidget *parent = nullptr);
    ~MainWindow() override;

    void onEvent(const Event &event) override;

    // 供 app/cli 通过命令系统打开会话，保持「一切皆命令」。
    CommandResult runCommand(const QString &commandId,
                             const QVariantMap &args = QVariantMap());

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void buildUi();
    void buildStatusBar();
    void registerCoreViews();
    void registerCommands();
    void buildMenus();
    void subscribeEvents();
    void restoreSettings();
    void saveSettings();

    bool addViewToUi(const QString &viewId);
    void removeViewFromUi(const QString &viewId);
    void showSidebarView(const QString &viewId);
    void toggleSidebar();

    void openSessionDialog();
    void openLoopbackSession(const QString &preferredName = QString());
    void closeSession(const QString &sessionId);
    void selectSession(const QString &sessionId);
    ConsoleView *currentConsole() const;
    SessionWorkspace *currentWorkspace() const;
    ISession *currentSession() const;
    void addSessionTab(const QVariantMap &info);
    void removeSessionTab(const QString &sessionId);
    void detachSessionTab(int index, const QPoint &globalPos);
    void dockSessionTab(const QString &sessionId);
    void updateTabInfo(const QVariantMap &info);
    void updateStatus();
    void appendOutput(const QString &text, bool error = false);

    void showPalette();
    void executeCommand(const QString &commandId, const QVariantMap &args = QVariantMap());
    CommandResult executeCommandResult(const QString &commandId, const QVariantMap &args);
    QAction *createCommandAction(const QString &commandId, const QString &text,
                                 QObject *parent,
                                 const QKeySequence &shortcut = QKeySequence());
    void addCommand(const QString &commandId, const QString &title, const QString &category,
                    std::function<CommandResult(const CommandInvocation &)> handler,
                    QVector<CommandArg> args = {});
    QString uniqueSessionName(const QString &base) const;

    CommandRegistry *m_commands = nullptr;
    SessionService *m_sessions = nullptr;
    SettingsService *m_settings = nullptr;
    EventBus *m_bus = nullptr;
    PluginHost *m_plugins = nullptr;
    UiContext *m_ui = nullptr;
    NotificationHub *m_notifications = nullptr;

    QTabWidget *m_centerTabs = nullptr;
    QTabWidget *m_bottomTabs = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QDockWidget *m_sidebarDock = nullptr;
    QDockWidget *m_bottomDock = nullptr;
    QStackedWidget *m_sidebarStack = nullptr;
    QToolBar *m_activityBar = nullptr;
    QWidget *m_welcomePage = nullptr;
    QLabel *m_statusSession = nullptr;
    QLabel *m_statusStats = nullptr;
    QLabel *m_statusPlugins = nullptr;
    CommandPalette *m_palette = nullptr;
    QTimer *m_statusTimer = nullptr;

    QHash<QString, SessionWorkspace *> m_consoles;
    QHash<QString, SessionDockWidget *> m_floatingSessions;
    QHash<QString, int> m_sessionTabPositions;
    QString m_draggingDetachedSession;
    QPoint m_detachDragOffset;
    bool m_detachDragLeftWorkspace = false;
    QHash<QString, ViewHost *> m_consoleHosts;
    QHash<QString, QWidget *> m_sidebarPages;
    QHash<QString, QWidget *> m_panelPages;
    QHash<QString, QWidget *> m_editorPages;
    QHash<QString, QAction *> m_activityActions;

    QVector<ISubscription *> m_eventSubscriptions;
    QVector<ISubscription *> m_commandSubscriptions;
    QVector<ICommandHandler *> m_commandHandlers;
    QVector<IViewFactory *> m_factories;

    QString m_activeSidebarView;
    QRect m_resizeStartFrame;
    QPoint m_resizeStartCursor;
    quintptr m_resizeWindowId = 0;
    int m_resizeEdges = 0;
    bool m_liveWindowResize = false;
};

} // namespace xcom
