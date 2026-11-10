#include "shell/main_window.h"

#include "serial/session_service.h"
#include "services/command_registry.h"
#include "services/event_bus.h"
#include "services/plugin_host.h"
#include "services/settings_service.h"
#include "shell/command_palette.h"
#include "shell/command_terminal.h"
#include "shell/notification_hub.h"
#include "shell/plugin_manager_view.h"
#include "shell/session_dialog.h"
#include "shell/ui_context.h"
#include "ui-kit/command_line.h"
#include "ui-kit/console_view.h"
#include "ui-kit/frame_parser_view.h"
#include "ui-kit/session_workspace.h"
#include "xcom/api/version.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QDockWidget>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace xcom {
namespace {

#ifdef Q_OS_WIN
constexpr int kResizeLeft = 0x01;
constexpr int kResizeRight = 0x02;
constexpr int kResizeTop = 0x04;
constexpr int kResizeBottom = 0x08;
#endif

class DetachableTabBar : public QTabBar
{
public:
    explicit DetachableTabBar(QWidget *parent = nullptr) : QTabBar(parent) {}

    std::function<void(int, const QPoint &)> onDetach;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressedIndex = tabAt(event->position().toPoint());
            m_pressPos = event->position().toPoint();
            m_detached = false;
        }
        QTabBar::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_detached && m_pressedIndex >= 0 && (event->buttons() & Qt::LeftButton)
            && (event->position().toPoint() - m_pressPos).manhattanLength()
                   >= QApplication::startDragDistance()) {
            const QPoint local = mapFromGlobal(event->globalPosition().toPoint());
            if (!rect().contains(local)) {
                m_detached = true;
                if (onDetach) onDetach(m_pressedIndex, event->globalPosition().toPoint());
                return;
            }
        }
        QTabBar::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_pressedIndex = -1;
        QTabBar::mouseReleaseEvent(event);
    }

private:
    int m_pressedIndex = -1;
    QPoint m_pressPos;
    bool m_detached = false;
};

class DetachableTabWidget : public QTabWidget
{
public:
    explicit DetachableTabWidget(QWidget *parent = nullptr) : QTabWidget(parent)
    {
        auto *bar = new DetachableTabBar(this);
        setTabBar(bar);
        m_bar = bar;
    }
    void setDetachHandler(std::function<void(int, const QPoint &)> handler)
    {
        m_bar->onDetach = std::move(handler);
    }
private:
    DetachableTabBar *m_bar = nullptr;
};

class FunctionHandler : public ICommandHandler
{
public:
    using Callback = std::function<CommandResult(const CommandInvocation &)>;

    explicit FunctionHandler(Callback callback)
        : m_callback(std::move(callback))
    {
    }

    CommandResult execute(const CommandInvocation &invocation, ICommandProgress *progress) override
    {
        Q_UNUSED(progress)
        return m_callback(invocation);
    }

private:
    Callback m_callback;
};

class LambdaViewFactory : public IViewFactory
{
public:
    using Callback = std::function<QWidget *(QWidget *, IViewHost *)>;

    explicit LambdaViewFactory(Callback callback)
        : m_callback(std::move(callback))
    {
    }

    QWidget *create(QWidget *parent, IViewHost *host) override
    {
        return m_callback(parent, host);
    }

private:
    Callback m_callback;
};

QString formatBytes(quint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(QString::number(kb / 1024.0, 'f', 2));
}

} // namespace

class SessionDockWidget : public QDockWidget
{
public:
    SessionDockWidget(const QString &title, QWidget *parent = nullptr)
        : QDockWidget(title, parent)
    {
        setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                    | QDockWidget::DockWidgetFloatable);
        setAllowedAreas(Qt::AllDockWidgetAreas);
        setAttribute(Qt::WA_DeleteOnClose, false);
        connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
            if (floating) {
                QTimer::singleShot(0, this, [this] { clearTransientParent(); });
            }
        });
    }

    std::function<void()> onCloseToTab;
    std::function<void()> onActivated;
    std::function<void()> onMoved;
    QWidget *takeContent()
    {
        QWidget *content = widget();
        setWidget(nullptr);
        if (content) content->setParent(nullptr);
        return content;
    }
    void trackMoves() { m_trackMoves = true; }
    void clearTransientParent()
    {
        if (!isFloating()) return;
        // Keep QDockWidget's parent for drag-to-dock, but remove the transient
        // window relation that propagates minimize/restore to the main workbench.
        if (QWindow *handle = windowHandle()) {
            handle->setTransientParent(nullptr);
#ifdef Q_OS_WIN
            // Qt sets the native owner while creating the floating tool window.
            // Clear it once after topLevelChanged; later Qt shows honor the null
            // transient parent above. Avoid touching the handle during paint/state events.
            const HWND nativeHandle = reinterpret_cast<HWND>(handle->winId());
            if (nativeHandle) SetWindowLongPtrW(nativeHandle, GWLP_HWNDPARENT, 0);
#endif
        }
    }
    void lockFloatingSize(const QSize &size)
    {
        m_sizeLocked = true;
        setFixedSize(size);
        resize(size);
    }
    void unlockFloatingSize()
    {
        if (!m_sizeLocked) return;
        const QSize current = size();
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        resize(current);
        m_sizeLocked = false;
    }

protected:
    void moveEvent(QMoveEvent *event) override
    {
        QDockWidget::moveEvent(event);
        if (m_trackMoves && onMoved) onMoved();
    }
    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if ((event->type() == QEvent::WindowActivate || event->type() == QEvent::Show)
            && onActivated) onActivated();
    }
    void closeEvent(QCloseEvent *event) override
    {
        QDockWidget::closeEvent(event);
        if (onCloseToTab) onCloseToTab();
    }

private:
    bool m_trackMoves = false;
    bool m_sizeLocked = false;
};

MainWindow::MainWindow(CommandRegistry *commands, SessionService *sessions,
                       SettingsService *settings, EventBus *bus, PluginHost *plugins,
                       UiContext *ui, NotificationHub *notifications, QWidget *parent)
    : QMainWindow(parent)
    , m_commands(commands)
    , m_sessions(sessions)
    , m_settings(settings)
    , m_bus(bus)
    , m_plugins(plugins)
    , m_ui(ui)
    , m_notifications(notifications)
{
    buildUi();
    qApp->installEventFilter(this);
    registerCommands();
    buildMenus();
    subscribeEvents();
    restoreSettings();

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer->start();

    updateStatus();
    appendOutput(QStringLiteral("X-COM %1 已启动，已激活插件 %2 个")
                     .arg(apiVersion())
                     .arg(m_plugins ? m_plugins->activeCount() : 0));
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    for (ISubscription *subscription : m_eventSubscriptions) {
        if (subscription) {
            subscription->unsubscribe();
        }
    }
    m_eventSubscriptions.clear();

    for (ISubscription *subscription : m_commandSubscriptions) {
        if (subscription) {
            subscription->unsubscribe();
        }
    }
    m_commandSubscriptions.clear();

    for (ICommandHandler *handler : m_commandHandlers) {
        delete handler;
    }
    m_commandHandlers.clear();

    for (SessionDockWidget *window : m_floatingSessions) {
        window->onCloseToTab = nullptr;
        window->takeContent();
        removeDockWidget(window);
        delete window;
    }
    m_floatingSessions.clear();
    for (SessionWorkspace *workspace : m_consoles) {
        delete workspace;
    }
    m_consoles.clear();
    for (ViewHost *host : m_consoleHosts) {
        delete host;
    }
    m_consoleHosts.clear();

    // 核心视图工厂由 UiContext 持有引用，这里不释放。
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("X-COM 串口工作台"));
    setMinimumSize(980, 640);
    resize(1280, 820);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks
                   | QMainWindow::AllowNestedDocks);

    auto *sessionTabs = new DetachableTabWidget(this);
    m_centerTabs = sessionTabs;
    sessionTabs->setDetachHandler([this](int index, const QPoint &pos) {
        // 不在 QTabBar::mouseMoveEvent 调用栈内移除 tab，避免 Qt 内部拖动状态失效。
        QTimer::singleShot(0, this, [this, index, pos] { detachSessionTab(index, pos); });
    });
    m_centerTabs->setTabsClosable(true);
    m_centerTabs->setMovable(true);
    m_centerTabs->setDocumentMode(true);
    setCentralWidget(m_centerTabs);

    m_welcomePage = new QWidget(m_centerTabs);
    auto *welcomeLayout = new QVBoxLayout(m_welcomePage);
    welcomeLayout->setContentsMargins(40, 40, 40, 40);
    auto *welcome = new QLabel(m_welcomePage);
    welcome->setAlignment(Qt::AlignCenter);
    welcome->setText(QStringLiteral(
        "<h2>X-COM 串口工作台</h2>"
        "<p>插件化、多会话的串口调试工具。</p>"
        "<p style='color:#9d9d9d'>Ctrl+T 打开串口会话     Ctrl+Shift+T 打开回环会话<br>"
        "Ctrl+Shift+P 命令面板     Ctrl+` 命令终端</p>"));
    welcome->setWordWrap(true);
    welcomeLayout->addWidget(welcome);
    const int welcomeIndex = m_centerTabs->addTab(m_welcomePage, QStringLiteral("欢迎"));
    m_centerTabs->tabBar()->setTabButton(welcomeIndex, QTabBar::RightSide, nullptr);

    m_activityBar = new QToolBar(QStringLiteral("活动栏"), this);
    m_activityBar->setMovable(false);
    m_activityBar->setFloatable(false);
    m_activityBar->setOrientation(Qt::Vertical);
    m_activityBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_activityBar->setMinimumWidth(64);
    addToolBar(Qt::LeftToolBarArea, m_activityBar);

    m_sidebarStack = new QStackedWidget(this);
    m_sidebarDock = new QDockWidget(QStringLiteral("资源管理器"), this);
    m_sidebarDock->setObjectName(QStringLiteral("sidebarDock"));
    m_sidebarDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarDock->setWidget(m_sidebarStack);
    m_sidebarDock->setMinimumWidth(240);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);

    m_bottomTabs = new QTabWidget(this);
    m_bottomTabs->setDocumentMode(true);
    m_output = new QPlainTextEdit(m_bottomTabs);
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(5000);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_output->setFont(mono);
    m_bottomTabs->addTab(m_output, QStringLiteral("输出"));

    m_bottomDock = new QDockWidget(QStringLiteral("面板"), this);
    m_bottomDock->setObjectName(QStringLiteral("bottomDock"));
    m_bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_bottomDock->setWidget(m_bottomTabs);
    addDockWidget(Qt::BottomDockWidgetArea, m_bottomDock);

    resizeDocks({m_sidebarDock}, {280}, Qt::Horizontal);
    resizeDocks({m_bottomDock}, {190}, Qt::Vertical);

    buildStatusBar();

    connect(m_centerTabs, &QTabWidget::currentChanged, this, [this](int) {
        if (ISession *session = currentSession()) {
            m_sessions->setActiveId(session->id());
        }
        updateStatus();
    });
    connect(m_centerTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *widget = m_centerTabs->widget(index);
        if (!widget || widget == m_welcomePage) {
            return;
        }
        const QString id = widget->property("sessionId").toString();
        if (!id.isEmpty()) {
            closeSession(id);
        }
    });

    m_palette = new CommandPalette(m_commands, this);

    m_ui->setShowViewHandler([this](const QString &id) { showSidebarView(id); });
    m_ui->setOpenSessionHandler([this](const QString &id) { selectSession(id); });
    m_ui->setCommandHandler([this](const QString &id, const QVariantMap &args) {
        return executeCommandResult(id, args);
    });

    connect(m_ui->viewRegistry(), &ViewRegistry::viewAdded, this,
            [this](const QString &id) { addViewToUi(id); });
    connect(m_ui->viewRegistry(), &ViewRegistry::viewRemoved, this,
            [this](const QString &id) { removeViewFromUi(id); });
    connect(m_ui, &UiContext::menuChanged, this, &MainWindow::buildMenus);

    if (m_notifications) {
        connect(m_notifications, &NotificationHub::notified, this,
                [this](int level, const QString &title, const QString &message) {
                    appendOutput(QStringLiteral("[%1] %2").arg(title, message), level >= 2);
                    statusBar()->showMessage(QStringLiteral("%1：%2").arg(title, message), 5000);
                });
    }

    registerCoreViews();

    const QVector<ViewSpec> existing = m_ui->viewRegistry()->views();
    for (const ViewSpec &spec : existing) {
        addViewToUi(spec.id);
    }
}

void MainWindow::buildStatusBar()
{
    m_statusSession = new QLabel(QStringLiteral("无会话"), this);
    m_statusStats = new QLabel(this);
    m_statusPlugins = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusSession);
    statusBar()->addPermanentWidget(m_statusStats);
    statusBar()->addPermanentWidget(m_statusPlugins);
}

void MainWindow::registerCoreViews()
{
    {
        ViewSpec spec;
        spec.id = QStringLiteral("shell.plugins");
        spec.title = QStringLiteral("插件");
        spec.location = ViewLocation::Sidebar;
        spec.order = 200;
        auto *factory = new LambdaViewFactory([this](QWidget *parent, IViewHost *) {
            return new PluginManagerView(m_plugins, parent);
        });
        m_factories.append(factory);
        m_ui->views().registerView(spec, factory);
    }
    {
        ViewSpec spec;
        spec.id = QStringLiteral("shell.terminal");
        spec.title = QStringLiteral("命令终端");
        spec.location = ViewLocation::Panel;
        spec.order = 100;
        auto *factory = new LambdaViewFactory([this](QWidget *parent, IViewHost *) {
            return new CommandTerminal(m_commands, parent);
        });
        m_factories.append(factory);
        m_ui->views().registerView(spec, factory);
    }
}

bool MainWindow::addViewToUi(const QString &viewId)
{
    if (m_sidebarPages.contains(viewId) || m_panelPages.contains(viewId)
        || m_editorPages.contains(viewId)) {
        return true;
    }
    ViewRegistry *registry = m_ui->viewRegistry();
    if (!registry->contains(viewId)) {
        return false;
    }
    const ViewSpec spec = registry->spec(viewId);

    switch (spec.location) {
    case ViewLocation::Sidebar: {
        QWidget *widget = registry->widget(viewId, m_sidebarStack);
        if (!widget) {
            return false;
        }
        m_sidebarStack->addWidget(widget);
        m_sidebarPages.insert(viewId, widget);

        auto *action = new QAction(spec.title, this);
        action->setCheckable(true);
        action->setToolTip(QStringLiteral("%1 (%2)").arg(spec.title, viewId));
        connect(action, &QAction::triggered, this, [this, viewId] { showSidebarView(viewId); });
        QAction *before = nullptr;
        for (auto it = m_activityActions.constBegin(); it != m_activityActions.constEnd(); ++it) {
            if (registry->contains(it.key()) && registry->spec(it.key()).order > spec.order) {
                before = it.value();
                break;
            }
        }
        if (before) m_activityBar->insertAction(before, action);
        else m_activityBar->addAction(action);
        m_activityActions.insert(viewId, action);

        if (m_activeSidebarView.isEmpty()
            || (m_activeSidebarView == QStringLiteral("shell.plugins")
                && viewId == QStringLiteral("com.xcom.sessions.explorer"))) {
            showSidebarView(viewId);
        }
        return true;
    }
    case ViewLocation::Panel: {
        QWidget *widget = registry->widget(viewId, m_bottomTabs);
        if (!widget) {
            return false;
        }
        m_bottomTabs->addTab(widget, spec.title);
        m_panelPages.insert(viewId, widget);
        return true;
    }
    case ViewLocation::Editor: {
        QWidget *widget = registry->widget(viewId, m_centerTabs);
        if (!widget) {
            return false;
        }
        m_centerTabs->addTab(widget, spec.title);
        m_editorPages.insert(viewId, widget);
        return true;
    }
    }
    return false;
}

void MainWindow::removeViewFromUi(const QString &viewId)
{
    if (m_sidebarPages.contains(viewId)) {
        QWidget *widget = m_sidebarPages.take(viewId);
        m_sidebarStack->removeWidget(widget);
        if (QAction *action = m_activityActions.take(viewId)) {
            m_activityBar->removeAction(action);
            action->deleteLater();
        }
        if (m_activeSidebarView == viewId) {
            m_activeSidebarView.clear();
            if (!m_sidebarPages.isEmpty()) {
                showSidebarView(m_sidebarPages.constBegin().key());
            }
        }
    }
    if (m_panelPages.contains(viewId)) {
        QWidget *widget = m_panelPages.take(viewId);
        const int index = m_bottomTabs->indexOf(widget);
        if (index >= 0) {
            m_bottomTabs->removeTab(index);
        }
    }
    if (m_editorPages.contains(viewId)) {
        QWidget *widget = m_editorPages.take(viewId);
        const int index = m_centerTabs->indexOf(widget);
        if (index >= 0) {
            m_centerTabs->removeTab(index);
        }
    }
}

void MainWindow::showSidebarView(const QString &viewId)
{
    if (!m_sidebarPages.contains(viewId)) {
        return;
    }
    m_sidebarDock->show();
    m_sidebarStack->setCurrentWidget(m_sidebarPages.value(viewId));
    m_activeSidebarView = viewId;
    for (auto it = m_activityActions.constBegin(); it != m_activityActions.constEnd(); ++it) {
        it.value()->setChecked(it.key() == viewId);
    }
    if (m_settings) {
        m_settings->setValue(QStringLiteral("ui/sidebarView"), viewId);
    }
}

void MainWindow::toggleSidebar()
{
    m_sidebarDock->setVisible(!m_sidebarDock->isVisible());
}

void MainWindow::registerCommands()
{
    addCommand(QStringLiteral("app.palette.show"), QStringLiteral("显示命令面板"),
               QStringLiteral("应用"), [this](const CommandInvocation &) {
                   showPalette();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("session.open"), QStringLiteral("打开串口会话"),
               QStringLiteral("会话"),
               [this](const CommandInvocation &invocation) {
                   // 带 port 参数时直接打开（供启动参数、插件、脚本与 CLI 使用）；
                   // 无参数时弹出对话框，交给用户选择。
                   const QString port = invocation.args.value(QStringLiteral("port")).toString();
                   if (port.isEmpty()) {
                       openSessionDialog();
                       return CommandResult::success();
                   }
                   SessionDescriptor descriptor;
                   descriptor.transport = QStringLiteral("serial");
                   descriptor.config.portName = port;
                   descriptor.config.baudRate =
                       invocation.args.value(QStringLiteral("baud")).toInt();
                   if (descriptor.config.baudRate <= 0) {
                       descriptor.config.baudRate = 115200;
                   }
                   descriptor.config.dataBits =
                       invocation.args.value(QStringLiteral("dataBits")).toInt();
                   if (descriptor.config.dataBits <= 0) {
                       descriptor.config.dataBits = 8;
                   }
                   descriptor.name = uniqueSessionName(port);

                   Error error;
                   QString id;
                   if (!m_sessions->open(descriptor, &id, &error)) {
                       return CommandResult::failure(error.message);
                   }
                   return CommandResult::success(QStringLiteral("已打开 %1").arg(port),
                                                 QVariantMap{{QStringLiteral("session"), id}});
               },
               {CommandArg{QStringLiteral("port"), CommandArgType::String, false, {},
                           QStringLiteral("串口名；留空则弹出选择对话框")},
                CommandArg{QStringLiteral("baud"), CommandArgType::Int, false, 115200,
                           QStringLiteral("波特率")},
                CommandArg{QStringLiteral("dataBits"), CommandArgType::Int, false, 8,
                           QStringLiteral("数据位")}});

    addCommand(QStringLiteral("session.openLoopback"), QStringLiteral("打开回环会话"),
               QStringLiteral("会话"), [this](const CommandInvocation &invocation) {
                   openLoopbackSession(invocation.args.value(QStringLiteral("name")).toString());
                   return CommandResult::success();
               },
               {CommandArg{QStringLiteral("name"), CommandArgType::String, false, {},
                           QStringLiteral("会话名称，留空自动生成")}});

    addCommand(QStringLiteral("serial.setDtr"), QStringLiteral("设置 DTR 信号"),
               QStringLiteral("串口"), [this](const CommandInvocation &invocation) {
                   const QString sessionId =
                       invocation.args.value(QStringLiteral("session")).toString();
                   Error error;
                   if (!m_sessions->setDtr(sessionId,
                                           invocation.args.value(QStringLiteral("enabled")).toBool(),
                                           &error)) {
                       return CommandResult::failure(error.message);
                   }
                   return CommandResult::success();
               },
               {CommandArg{QStringLiteral("session"), CommandArgType::SessionId, true, {},
                           QStringLiteral("会话 id 或名称")},
                CommandArg{QStringLiteral("enabled"), CommandArgType::Bool, true, {},
                           QStringLiteral("是否启用 DTR")}});

    addCommand(QStringLiteral("serial.setRts"), QStringLiteral("设置 RTS 信号"),
               QStringLiteral("串口"), [this](const CommandInvocation &invocation) {
                   const QString sessionId =
                       invocation.args.value(QStringLiteral("session")).toString();
                   Error error;
                   if (!m_sessions->setRts(sessionId,
                                           invocation.args.value(QStringLiteral("enabled")).toBool(),
                                           &error)) {
                       return CommandResult::failure(error.message);
                   }
                   return CommandResult::success();
               },
               {CommandArg{QStringLiteral("session"), CommandArgType::SessionId, true, {},
                           QStringLiteral("会话 id 或名称")},
                CommandArg{QStringLiteral("enabled"), CommandArgType::Bool, true, {},
                           QStringLiteral("是否启用 RTS")}});

    addCommand(QStringLiteral("session.close"), QStringLiteral("关闭会话"), QStringLiteral("会话"),
               [this](const CommandInvocation &invocation) {
                   QString id = invocation.args.value(QStringLiteral("session")).toString();
                   if (id.isEmpty()) {
                       if (ISession *session = currentSession()) {
                           id = session->id();
                       }
                   }
                   if (id.isEmpty()) {
                       return CommandResult::failure(QStringLiteral("没有可关闭的会话"));
                   }
                   closeSession(id);
                   return CommandResult::success();
               },
               {CommandArg{QStringLiteral("session"), CommandArgType::SessionId, false, {},
                           QStringLiteral("会话 id 或名称，留空表示当前会话")}});

    addCommand(QStringLiteral("session.closeAll"), QStringLiteral("关闭全部会话"),
               QStringLiteral("会话"), [this](const CommandInvocation &) {
                   m_sessions->closeAll();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("session.next"), QStringLiteral("切换到下一个会话"),
               QStringLiteral("会话"), [this](const CommandInvocation &) {
                   if (m_centerTabs->count() <= 1) {
                       return CommandResult::success();
                   }
                   int index = m_centerTabs->currentIndex();
                   for (int i = 0; i < m_centerTabs->count(); ++i) {
                       index = (index + 1) % m_centerTabs->count();
                       if (m_centerTabs->widget(index) != m_welcomePage) {
                           m_centerTabs->setCurrentIndex(index);
                           break;
                       }
                   }
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("session.focus"), QStringLiteral("聚焦会话"), QStringLiteral("会话"),
               [this](const CommandInvocation &invocation) {
                   const QString id = invocation.args.value(QStringLiteral("session")).toString();
                   if (id.isEmpty()) {
                       return CommandResult::failure(QStringLiteral("缺少会话参数"));
                   }
                   selectSession(id);
                   return CommandResult::success();
               },
               {CommandArg{QStringLiteral("session"), CommandArgType::SessionId, true, {},
                           QStringLiteral("会话 id 或名称")}});

    addCommand(QStringLiteral("console.clear"), QStringLiteral("清空当前控制台"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   ConsoleView *console = currentConsole();
                   if (!console) {
                       return CommandResult::failure(QStringLiteral("当前没有控制台会话"));
                   }
                   console->clearDisplay();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("console.toggleHex"), QStringLiteral("切换 HEX 显示"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   ConsoleView *console = currentConsole();
                   if (!console) {
                       return CommandResult::failure(QStringLiteral("当前没有控制台会话"));
                   }
                   console->toggleHexDisplay();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("console.toggleTimestamp"), QStringLiteral("切换时间戳"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   ConsoleView *console = currentConsole();
                   if (!console) {
                       return CommandResult::failure(QStringLiteral("当前没有控制台会话"));
                   }
                   console->toggleTimestamp();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("console.save"), QStringLiteral("保存接收数据"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   ConsoleView *console = currentConsole();
                   if (!console) {
                       return CommandResult::failure(QStringLiteral("当前没有控制台会话"));
                   }
                   console->saveToFile();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("console.commandLine"), QStringLiteral("串口命令行"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   SessionWorkspace *workspace = currentWorkspace();
                   if (!workspace) {
                       return CommandResult::failure(QStringLiteral("当前没有会话标签页"));
                   }
                   workspace->showCommandLine();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("console.parser"), QStringLiteral("命令解析窗口"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   SessionWorkspace *workspace = currentWorkspace();
                   if (!workspace) {
                       return CommandResult::failure(QStringLiteral("当前没有会话标签页"));
                   }
                   workspace->showParser();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("console.open"), QStringLiteral("打开控制台"),
               QStringLiteral("控制台"),
               [this](const CommandInvocation &invocation) {
                   const QString id = invocation.args.value(QStringLiteral("session")).toString();
                   if (!id.isEmpty()) {
                       selectSession(id);
                   }
                   SessionWorkspace *workspace = currentWorkspace();
                   if (!workspace) {
                       return CommandResult::failure(QStringLiteral("当前没有会话标签页"));
                   }
                   workspace->showConsole();
                   return CommandResult::success(QStringLiteral("已打开控制台"));
               },
               {CommandArg{QStringLiteral("session"), CommandArgType::SessionId, false, {},
                           QStringLiteral("会话 id 或名称，留空表示当前会话")}});

    addCommand(QStringLiteral("console.toggleVisibility"), QStringLiteral("显示/隐藏控制台"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   SessionWorkspace *workspace = currentWorkspace();
                   if (!workspace) {
                       return CommandResult::failure(QStringLiteral("当前没有会话标签页"));
                   }
                   const bool visible = !workspace->isConsoleVisible();
                   workspace->setConsoleVisible(visible);
                   if (m_settings) {
                       m_settings->setValue(QStringLiteral("ui/consoleVisible"), visible);
                   }
                   return CommandResult::success(visible ? QStringLiteral("已显示控制台")
                                                         : QStringLiteral("已隐藏控制台"));
               });

    addCommand(QStringLiteral("console.toggleBottom"), QStringLiteral("显示/隐藏串口命令行"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   SessionWorkspace *workspace = currentWorkspace();
                   if (!workspace) {
                       return CommandResult::failure(QStringLiteral("当前没有会话标签页"));
                   }
                   const bool visible = !workspace->isBottomVisible();
                   workspace->setBottomVisible(visible);
                   if (m_settings) {
                       m_settings->setValue(QStringLiteral("ui/bottomVisible"), visible);
                   }
                   return CommandResult::success(visible ? QStringLiteral("已显示串口命令行")
                                                         : QStringLiteral("已隐藏串口命令行"));
               });

    addCommand(QStringLiteral("console.toggleParser"), QStringLiteral("显示/隐藏命令解析"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   SessionWorkspace *workspace = currentWorkspace();
                   if (!workspace) {
                       return CommandResult::failure(QStringLiteral("当前没有会话标签页"));
                   }
                   const bool visible = !workspace->isParserVisible();
                   workspace->setParserVisible(visible);
                   if (m_settings) {
                       m_settings->setValue(QStringLiteral("ui/parserVisible"), visible);
                   }
                   return CommandResult::success(visible ? QStringLiteral("已显示命令解析")
                                                         : QStringLiteral("已隐藏命令解析"));
               });

    addCommand(QStringLiteral("console.float"), QStringLiteral("控制台浮出为独立窗口"),
               QStringLiteral("控制台"), [this](const CommandInvocation &) {
                   SessionWorkspace *workspace = currentWorkspace();
                   if (!workspace) {
                       return CommandResult::failure(QStringLiteral("当前没有会话标签页"));
                   }
                   if (!workspace->isConsoleVisible()) {
                       return CommandResult::failure(QStringLiteral("控制台已隐藏，请先显示控制台"));
                   }
                   workspace->setConsoleFloating(!workspace->isConsoleFloating());
                   return CommandResult::success(workspace->isConsoleFloating()
                                                     ? QStringLiteral("控制台已浮出为独立窗口")
                                                     : QStringLiteral("控制台已停靠回会话"));
               });

    addCommand(QStringLiteral("data.sendLine"), QStringLiteral("命令行发送一行"),
               QStringLiteral("发送"),
               [this](const CommandInvocation &invocation) {
                   const QString text = invocation.args.value(QStringLiteral("text")).toString();
                   if (text.isEmpty()) {
                       return CommandResult::failure(QStringLiteral("发送内容为空"));
                   }
                   SessionWorkspace *workspace = currentWorkspace();
                   CommandLineView *line = workspace ? workspace->commandLine() : nullptr;
                   if (!line) {
                       return CommandResult::failure(QStringLiteral("当前没有串口命令行"));
                   }
                   line->setHexMode(invocation.args.value(QStringLiteral("hex")).toBool());
                   line->setNewline(invocation.args.contains(QStringLiteral("newline"))
                                        ? invocation.args.value(QStringLiteral("newline")).toBool()
                                        : true);
                   return line->sendLine(text);
               },
               {CommandArg{QStringLiteral("text"), CommandArgType::String, true, {},
                           QStringLiteral("要发送的一行内容")},
                CommandArg{QStringLiteral("hex"), CommandArgType::Bool, false, false,
                           QStringLiteral("按十六进制解析")},
                CommandArg{QStringLiteral("newline"), CommandArgType::Bool, false, true,
                           QStringLiteral("追加 \\r\\n")}});

    addCommand(QStringLiteral("parser.setMode"), QStringLiteral("设置分帧方式"),
               QStringLiteral("命令解析"),
               [this](const CommandInvocation &invocation) {
                   SessionWorkspace *workspace = currentWorkspace();
                   FrameParserView *parser = workspace ? workspace->parser() : nullptr;
                   if (!parser) {
                       return CommandResult::failure(QStringLiteral("当前没有解析窗口"));
                   }
                   const QString modeKey =
                       invocation.args.value(QStringLiteral("mode")).toString();
                   bool ok = false;
                   const FrameMode mode = frameModeFromKey(modeKey, &ok);
                   if (!ok) {
                       return CommandResult::failure(
                           QStringLiteral("未知分帧方式：%1（可选 line / delimiter / fixedLength / "
                                          "idle）")
                               .arg(modeKey));
                   }
                   FrameSettings settings = parser->settings();
                   settings.mode = mode;
                   const QString delimiter = invocation.args.value(QStringLiteral("delimiter"))
                                                 .toString();
                   if (!delimiter.isEmpty()) {
                       settings.delimiter = FrameSplitter::decodeEscapes(delimiter);
                   }
                   const int length =
                       invocation.args.value(QStringLiteral("length")).toInt();
                   if (length > 0) {
                       settings.fixedLength = length;
                   }
                   if (invocation.args.contains(QStringLiteral("keepDelimiter"))) {
                       settings.keepDelimiter =
                           invocation.args.value(QStringLiteral("keepDelimiter")).toBool();
                   }
                   parser->setSettings(settings);
                   return CommandResult::success(
                       QStringLiteral("分帧方式：%1").arg(frameModeLabel(mode)));
               },
               {CommandArg{QStringLiteral("mode"), CommandArgType::Enum, true, {},
                           QStringLiteral("line / delimiter / fixedLength / idle")},
                CommandArg{QStringLiteral("delimiter"), CommandArgType::String, false, {},
                           QStringLiteral("分隔符，支持 \\r\\n \\x03 等转义")},
                CommandArg{QStringLiteral("length"), CommandArgType::Int, false, 0,
                           QStringLiteral("固定长度（字节）")},
                CommandArg{QStringLiteral("keepDelimiter"), CommandArgType::Bool, false, false,
                           QStringLiteral("保留分隔符")}});

    addCommand(QStringLiteral("parser.export"), QStringLiteral("导出解析结果"),
               QStringLiteral("命令解析"), [this](const CommandInvocation &) {
                   SessionWorkspace *workspace = currentWorkspace();
                   FrameParserView *parser = workspace ? workspace->parser() : nullptr;
                   if (!parser) {
                       return CommandResult::failure(QStringLiteral("当前没有解析窗口"));
                   }
                   parser->exportFrames();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("parser.clear"), QStringLiteral("清空解析结果"),
               QStringLiteral("命令解析"), [this](const CommandInvocation &) {
                   SessionWorkspace *workspace = currentWorkspace();
                   FrameParserView *parser = workspace ? workspace->parser() : nullptr;
                   if (!parser) {
                       return CommandResult::failure(QStringLiteral("当前没有解析窗口"));
                   }
                   parser->clearFrames();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("view.sessions"), QStringLiteral("显示会话资源管理器"),
               QStringLiteral("视图"), [this](const CommandInvocation &) {
                   const QString id = QStringLiteral("com.xcom.sessions.explorer");
                   if (!m_ui->viewRegistry()->contains(id)) {
                       return CommandResult::failure(
                           QStringLiteral("会话视图不可用（插件 com.xcom.sessions 未加载）"));
                   }
                   showSidebarView(id);
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("view.plugins"), QStringLiteral("显示插件管理器"),
               QStringLiteral("视图"), [this](const CommandInvocation &) {
                   showSidebarView(QStringLiteral("shell.plugins"));
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("view.output"), QStringLiteral("显示输出面板"),
               QStringLiteral("视图"), [this](const CommandInvocation &) {
                   m_bottomDock->show();
                   m_bottomTabs->setCurrentWidget(m_output);
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("view.terminal"), QStringLiteral("显示命令终端"),
               QStringLiteral("视图"), [this](const CommandInvocation &) {
                   m_bottomDock->show();
                   if (m_panelPages.contains(QStringLiteral("shell.terminal"))) {
                       m_bottomTabs->setCurrentWidget(
                           m_panelPages.value(QStringLiteral("shell.terminal")));
                   }
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("view.toggleSidebar"), QStringLiteral("显示/隐藏侧边栏"),
               QStringLiteral("视图"), [this](const CommandInvocation &) {
                   toggleSidebar();
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("app.about"), QStringLiteral("关于 X-COM"), QStringLiteral("帮助"),
               [this](const CommandInvocation &) {
                   QMessageBox::about(
                       this, QStringLiteral("关于 X-COM"),
                       QStringLiteral("<h3>X-COM 串口工作台</h3>"
                                      "<p>版本 %1 · API %2</p>"
                                      "<p>插件化、多会话串口调试工具。</p>")
                           .arg(QApplication::applicationVersion(), apiVersion()));
                   return CommandResult::success();
               });

    addCommand(QStringLiteral("app.quit"), QStringLiteral("退出"), QStringLiteral("应用"),
               [this](const CommandInvocation &) {
                   close();
                   return CommandResult::success();
               });
}

void MainWindow::buildMenus()
{
    menuBar()->clear();

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto *sessionMenu = menuBar()->addMenu(QStringLiteral("会话(&S)"));
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    auto *toolsMenu = menuBar()->addMenu(QStringLiteral("工具(&T)"));
    auto *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));

    fileMenu->addAction(createCommandAction(QStringLiteral("session.open"),
                                            QStringLiteral("打开会话..."), fileMenu,
                                            QKeySequence(QStringLiteral("Ctrl+T"))));
    fileMenu->addAction(createCommandAction(QStringLiteral("session.openLoopback"),
                                            QStringLiteral("打开回环会话"), fileMenu,
                                            QKeySequence(QStringLiteral("Ctrl+Shift+T"))));
    fileMenu->addSeparator();
    fileMenu->addAction(createCommandAction(QStringLiteral("app.palette.show"),
                                            QStringLiteral("命令面板..."), fileMenu,
                                            QKeySequence(QStringLiteral("Ctrl+Shift+P"))));
    fileMenu->addSeparator();
    fileMenu->addAction(createCommandAction(QStringLiteral("app.quit"), QStringLiteral("退出"),
                                            fileMenu, QKeySequence(QStringLiteral("Ctrl+Q"))));

    sessionMenu->addAction(createCommandAction(QStringLiteral("session.close"),
                                               QStringLiteral("关闭当前会话"), sessionMenu,
                                               QKeySequence(QStringLiteral("Ctrl+W"))));
    sessionMenu->addAction(createCommandAction(QStringLiteral("session.closeAll"),
                                               QStringLiteral("关闭全部会话"), sessionMenu));
    sessionMenu->addAction(createCommandAction(QStringLiteral("session.next"),
                                               QStringLiteral("下一个会话"), sessionMenu,
                                               QKeySequence(QStringLiteral("Ctrl+Tab"))));
    sessionMenu->addSeparator();
    sessionMenu->addAction(createCommandAction(QStringLiteral("console.toggleHex"),
                                               QStringLiteral("切换 HEX 显示"), sessionMenu,
                                               QKeySequence(QStringLiteral("Ctrl+Shift+H"))));
    sessionMenu->addAction(createCommandAction(QStringLiteral("console.clear"),
                                               QStringLiteral("清空接收"), sessionMenu,
                                               QKeySequence(QStringLiteral("Ctrl+L"))));
    sessionMenu->addAction(createCommandAction(QStringLiteral("console.save"),
                                               QStringLiteral("保存接收数据..."), sessionMenu));

    viewMenu->addAction(createCommandAction(QStringLiteral("view.sessions"),
                                            QStringLiteral("会话资源管理器"), viewMenu,
                                            QKeySequence(QStringLiteral("Ctrl+Shift+E"))));
    viewMenu->addAction(createCommandAction(QStringLiteral("view.plugins"),
                                            QStringLiteral("插件管理器"), viewMenu));
    viewMenu->addAction(createCommandAction(QStringLiteral("view.output"),
                                            QStringLiteral("输出面板"), viewMenu));
    viewMenu->addAction(createCommandAction(QStringLiteral("view.terminal"),
                                            QStringLiteral("命令终端"), viewMenu,
                                            QKeySequence(QStringLiteral("Ctrl+`"))));
    viewMenu->addSeparator();
    viewMenu->addAction(createCommandAction(QStringLiteral("console.toggleVisibility"),
                                            QStringLiteral("显示/隐藏控制台"), viewMenu));
    viewMenu->addAction(createCommandAction(QStringLiteral("console.toggleBottom"),
                                            QStringLiteral("显示/隐藏串口命令行"), viewMenu));
    viewMenu->addAction(createCommandAction(QStringLiteral("console.toggleParser"),
                                            QStringLiteral("显示/隐藏命令解析"), viewMenu));
    viewMenu->addAction(createCommandAction(QStringLiteral("console.float"),
                                            QStringLiteral("控制台浮出窗口"), viewMenu));
    viewMenu->addSeparator();
    viewMenu->addAction(createCommandAction(QStringLiteral("view.toggleSidebar"),
                                            QStringLiteral("显示/隐藏侧边栏"), viewMenu));
    viewMenu->addAction(createCommandAction(QStringLiteral("app.palette.show"),
                                            QStringLiteral("命令面板..."), viewMenu));

    helpMenu->addAction(createCommandAction(QStringLiteral("app.about"), QStringLiteral("关于 X-COM"),
                                            helpMenu));

    QHash<QString, QMenu *> menuByPath;
    menuByPath.insert(QStringLiteral("menu/file"), fileMenu);
    menuByPath.insert(QStringLiteral("menu/session"), sessionMenu);
    menuByPath.insert(QStringLiteral("menu/view"), viewMenu);
    menuByPath.insert(QStringLiteral("menu/tools"), toolsMenu);
    menuByPath.insert(QStringLiteral("menu/help"), helpMenu);

    QVector<MenuContribution> contributions = m_ui->menuRegistry()->items();
    std::sort(contributions.begin(), contributions.end(),
              [](const MenuContribution &a, const MenuContribution &b) {
                  if (a.menu != b.menu) {
                      return a.menu < b.menu;
                  }
                  if (a.order != b.order) {
                      return a.order < b.order;
                  }
                  return a.command < b.command;
              });

    for (const MenuContribution &contribution : contributions) {
        QMenu *menu = menuByPath.value(contribution.menu);
        if (!menu) {
            continue;
        }
        QString title = contribution.label;
        if (title.isEmpty()) {
            for (const CommandSpec &spec : m_commands->commands()) {
                if (spec.id == contribution.command) {
                    title = spec.title;
                    break;
                }
            }
        }
        if (title.isEmpty()) {
            title = contribution.command;
        }
        menu->addAction(createCommandAction(contribution.command, title, menu));
    }

    if (toolsMenu->isEmpty()) {
        QAction *placeholder = toolsMenu->addAction(QStringLiteral("(暂无工具插件)"));
        placeholder->setEnabled(false);
    }
}

QAction *MainWindow::createCommandAction(const QString &commandId, const QString &text,
                                         QObject *parent, const QKeySequence &shortcut)
{
    auto *action = new QAction(text, parent);
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WindowShortcut);
    }
    connect(action, &QAction::triggered, this, [this, commandId] {
        executeCommand(commandId);
    });
    return action;
}

void MainWindow::addCommand(const QString &commandId, const QString &title,
                            const QString &category,
                            std::function<CommandResult(const CommandInvocation &)> handler,
                            QVector<CommandArg> args)
{
    CommandSpec spec;
    spec.id = commandId;
    spec.title = title;
    spec.category = category;
    spec.args = args;

    auto *commandHandler = new FunctionHandler(std::move(handler));
    m_commandHandlers.append(commandHandler);
    ISubscription *subscription = m_commands->registerCommand(spec, commandHandler);
    if (subscription) {
        m_commandSubscriptions.append(subscription);
    }
}

void MainWindow::subscribeEvents()
{
    const QStringList topics = {
        QStringLiteral("session/added"),         QStringLiteral("session/closed"),
        QStringLiteral("session/stateChanged"),  QStringLiteral("session/activeChanged"),
        QStringLiteral("session/error"),         QStringLiteral("log/message")};
    for (const QString &topic : topics) {
        ISubscription *subscription = m_bus->subscribe(topic, this);
        if (subscription) {
            m_eventSubscriptions.append(subscription);
        }
    }
}

void MainWindow::onEvent(const Event &event)
{
    if (event.topic == QStringLiteral("session/added")) {
        addSessionTab(event.payload);
    } else if (event.topic == QStringLiteral("session/closed")) {
        removeSessionTab(event.payload.value(QStringLiteral("id")).toString());
    } else if (event.topic == QStringLiteral("session/stateChanged")) {
        updateTabInfo(event.payload);
        if (currentSession()
            && currentSession()->id() == event.payload.value(QStringLiteral("id")).toString()) {
            updateStatus();
        }
    } else if (event.topic == QStringLiteral("session/activeChanged")) {
        updateStatus();
    } else if (event.topic == QStringLiteral("session/error")) {
        const QString message = event.payload.value(QStringLiteral("message")).toString();
        appendOutput(QStringLiteral("[会话错误] %1").arg(message), true);
        statusBar()->showMessage(message, 6000);
    } else if (event.topic == QStringLiteral("log/message")) {
        const QString level = event.payload.value(QStringLiteral("level")).toString();
        const QString text = event.payload.value(QStringLiteral("text")).toString();
        const QString source = event.payload.value(QStringLiteral("source")).toString();
        appendOutput(QStringLiteral("[%1] %2").arg(source, text), level == QStringLiteral("error"));
        if (level == QStringLiteral("warning") || level == QStringLiteral("error")) {
            statusBar()->showMessage(QStringLiteral("%1：%2").arg(source, text), 4000);
        }
    }
}

void MainWindow::addSessionTab(const QVariantMap &info)
{
    const QString id = info.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || m_consoles.contains(id)) {
        return;
    }
    ISession *session = m_sessions->find(id);
    if (!session) {
        return;
    }

    if (m_welcomePage) {
        const int index = m_centerTabs->indexOf(m_welcomePage);
        if (index >= 0) {
            m_centerTabs->removeTab(index);
            m_welcomePage->deleteLater();
            m_welcomePage = nullptr;
        }
    }

    auto *host = new ViewHost(m_ui, QStringLiteral("session.console"), id);
    auto *workspace = new SessionWorkspace(session, host, m_centerTabs);
    workspace->setProperty("sessionId", id);
    // 面板开关是全局偏好：新会话沿用上次的布局，而不是每次都弹回默认。
    // 先恢复「命令解析」再恢复「底部面板」，这样用户主动收起的底部面板不会被重新展开。
    if (m_settings) {
        workspace->setParserVisible(
            m_settings->value(QStringLiteral("ui/parserVisible"), true).toBool());
        workspace->setBottomVisible(
            m_settings->value(QStringLiteral("ui/bottomVisible"), true).toBool());
        workspace->setConsoleVisible(
            m_settings->value(QStringLiteral("ui/consoleVisible"), true).toBool());
    }
    connect(workspace, &SessionWorkspace::panelVisibilityChanged, this, [this, workspace] {
        if (!m_settings) {
            return;
        }
        m_settings->setValue(QStringLiteral("ui/consoleVisible"), workspace->isConsoleVisible());
        m_settings->setValue(QStringLiteral("ui/bottomVisible"), workspace->isBottomVisible());
        m_settings->setValue(QStringLiteral("ui/parserVisible"), workspace->isParserVisible());
    });
    connect(workspace, &SessionWorkspace::serialConfigurationToggled, this,
            [this, id](bool visible) {
        auto *workspace = m_consoles.value(id, nullptr);
        if (!workspace) return;
        auto *existing = workspace->findChild<SessionDialog *>(QStringLiteral("sessionConfigPage"));
        if (!visible) {
            if (existing) workspace->hideBottomPage(existing);
            return;
        }
        ISession *session = m_sessions ? m_sessions->find(id) : nullptr;
        if (!session) return;
        if (existing) {
            workspace->showBottomPage(existing, QStringLiteral("串口配置"));
            return;
        }
        SessionDescriptor initial;
        initial.name = session->name();
        initial.transport = session->transport();
        initial.config = session->config();
        initial.encoding = session->encoding();
        auto *configPage = new SessionDialog(initial, workspace);
        configPage->setObjectName(QStringLiteral("sessionConfigPage"));
        configPage->setEmbeddedMode(true);
        configPage->setBusyPortsProvider([this, id] {
            QStringList busyPorts;
            if (!m_sessions) return busyPorts;
            for (const SessionInfo &info : m_sessions->list()) {
                if (info.id != id && info.transport != QStringLiteral("loopback"))
                    busyPorts.append(info.config.portName);
            }
            return busyPorts;
        });
        connect(configPage, &SessionDialog::configurationApplied, this,
                [this, id, initial](const SessionDescriptor &configuration) {
            SessionDescriptor updated = configuration;
            updated.encoding = initial.encoding;
            if (ISession *current = m_sessions ? m_sessions->find(id) : nullptr) {
                const PortConfig currentConfig = current->config();
                const bool switchingFromLoopback =
                    current->transport() == QStringLiteral("loopback")
                    && updated.transport != QStringLiteral("loopback");
                updated.config.dtr = switchingFromLoopback ? true : currentConfig.dtr;
                updated.config.rts = currentConfig.rts;
            } else {
                updated.config.dtr = initial.config.dtr;
                updated.config.rts = initial.config.rts;
            }
            Error error;
            if (!m_sessions->reconfigure(id, updated, &error))
                QMessageBox::warning(this, QStringLiteral("串口配置未应用"), error.message);
        });
        workspace->showBottomPage(configPage, QStringLiteral("串口配置"));
    });
    const QString name = info.value(QStringLiteral("name")).toString();
    const int index = m_centerTabs->addTab(workspace, name);
    m_centerTabs->setTabToolTip(index, QStringLiteral("%1 · %2").arg(id, name));
    m_consoles.insert(id, workspace);
    m_consoleHosts.insert(id, host);
    m_centerTabs->setCurrentIndex(index);
    m_sessions->setActiveId(id);
    updateStatus();
}

void MainWindow::removeSessionTab(const QString &sessionId)
{
    auto it = m_consoles.find(sessionId);
    if (it == m_consoles.end()) {
        return;
    }
    SessionWorkspace *workspace = it.value();
    if (SessionDockWidget *floating = m_floatingSessions.take(sessionId)) {
        floating->onCloseToTab = nullptr;
        floating->takeContent();
        removeDockWidget(floating);
        delete floating;
    }
    const int index = m_centerTabs->indexOf(workspace);
    if (index >= 0) {
        m_centerTabs->removeTab(index);
    }
    m_consoles.erase(it);
    delete workspace;

    if (ViewHost *host = m_consoleHosts.take(sessionId)) {
        delete host;
    }
    updateStatus();
}

void MainWindow::detachSessionTab(int index, const QPoint &globalPos)
{
    if (index < 0 || index >= m_centerTabs->count()) return;
    QWidget *widget = m_centerTabs->widget(index);
    const QString id = widget ? widget->property("sessionId").toString() : QString();
    if (id.isEmpty() || m_floatingSessions.contains(id)) return;

    const QString title = m_centerTabs->tabText(index);
    const QRect workspaceGeometry(m_centerTabs->mapToGlobal(QPoint(0, 0)), m_centerTabs->size());
    m_detachDragLeftWorkspace = false;
    m_sessionTabPositions.insert(id, index);
    m_centerTabs->removeTab(index);
    widget->setProperty("sessionFloating", true);
    auto *floating = new SessionDockWidget(title, this);
    floating->setWidget(widget);
    addDockWidget(Qt::RightDockWidgetArea, floating);
    floating->setFloating(true);
    widget->show();
    floating->onCloseToTab = [this, id] {
        QTimer::singleShot(0, this, [this, id] { dockSessionTab(id); });
    };
    floating->onActivated = [this, id] {
        if (m_sessions) m_sessions->setActiveId(id);
        updateStatus();
    };
    floating->onMoved = [this, id] {
        QTimer::singleShot(180, this, [this, id] {
            SessionDockWidget *dock = m_floatingSessions.value(id, nullptr);
            if (!dock || !dock->isFloating() || QApplication::mouseButtons() != Qt::NoButton)
                return;
            const QRect centerRect(m_centerTabs->mapToGlobal(QPoint(0, 0)), m_centerTabs->size());
            if (!centerRect.contains(QCursor::pos()))
                m_detachDragLeftWorkspace = true;
            else if (m_detachDragLeftWorkspace)
                dockSessionTab(id);
        });
    };
    m_floatingSessions.insert(id, floating);
    if (m_sessions) m_sessions->setActiveId(id);
    floating->show();
    // QDockWidget 首次 show 会按内容 sizeHint 调整尺寸，所以在显示后恢复原会话区域几何。
    floating->lockFloatingSize(workspaceGeometry.size());
    floating->setGeometry(workspaceGeometry);
    floating->raise();
    floating->activateWindow();
    floating->trackMoves();
    if (QApplication::mouseButtons() & Qt::LeftButton) {
        m_draggingDetachedSession = id;
        m_detachDragLeftWorkspace = false;
        m_detachDragOffset = globalPos - floating->frameGeometry().topLeft();
        m_detachDragOffset.setX(qBound(12, m_detachDragOffset.x(), qMax(12, floating->width() - 12)));
        m_detachDragOffset.setY(15);
        floating->move(globalPos - m_detachDragOffset);
        QTimer::singleShot(0, floating, [floating] {
            if (!(QApplication::mouseButtons() & Qt::LeftButton))
                floating->unlockFloatingSize();
        });
    } else {
        floating->unlockFloatingSize();
    }
}

void MainWindow::dockSessionTab(const QString &sessionId)
{
    SessionDockWidget *floating = m_floatingSessions.value(sessionId, nullptr);
    if (!floating) return;
    m_floatingSessions.take(sessionId);
    floating->onCloseToTab = nullptr;
    QWidget *widget = floating->takeContent();
    removeDockWidget(floating);
    if (widget) {
        widget->setProperty("sessionFloating", false);
        widget->setParent(m_centerTabs);
        ISession *session = m_sessions ? m_sessions->find(sessionId) : nullptr;
        const QString title = session ? session->name() : QStringLiteral("会话");
        const int index = m_centerTabs->insertTab(
            qBound(0, m_sessionTabPositions.take(sessionId), m_centerTabs->count()), widget, title);
        m_centerTabs->setTabToolTip(index, QStringLiteral("%1 · %2").arg(sessionId, title));
        m_centerTabs->setCurrentIndex(index);
        if (m_sessions) m_sessions->setActiveId(sessionId);
    }
    floating->hide();
    floating->deleteLater();
}

void MainWindow::updateTabInfo(const QVariantMap &info)
{
    const QString id = info.value(QStringLiteral("id")).toString();
    auto it = m_consoles.find(id);
    if (it == m_consoles.end()) {
        return;
    }
    const int index = m_centerTabs->indexOf(it.value());
    if (index < 0) {
        if (SessionDockWidget *floating = m_floatingSessions.value(id, nullptr))
            floating->setWindowTitle(info.value(QStringLiteral("name")).toString());
        return;
    }
    const int stateValue = info.value(QStringLiteral("state")).toInt();
    const QString name = info.value(QStringLiteral("name")).toString();
    const QString stateText = info.value(QStringLiteral("stateText")).toString();
    QString title = name;
    if (stateValue == int(SessionState::Failed)) {
        title += QStringLiteral("  [失败]");
    } else if (stateValue == int(SessionState::Opening)) {
        title += QStringLiteral("  [打开中]");
    }
    m_centerTabs->setTabText(index, title);
    m_centerTabs->setTabToolTip(index, QStringLiteral("%1 · %2 · %3").arg(name, id, stateText));
}

CommandResult MainWindow::runCommand(const QString &commandId, const QVariantMap &args)
{
    return executeCommandResult(commandId, args);
}

void MainWindow::openSessionDialog()
{
    SessionDialog dialog(QString(), this);
    dialog.setBusyPortsProvider([this] {
        QStringList busyPorts;
        if (!m_sessions) return busyPorts;
        for (const SessionInfo &info : m_sessions->list()) {
            if (info.transport != QStringLiteral("loopback"))
                busyPorts.append(info.config.portName);
        }
        return busyPorts;
    });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    SessionDescriptor descriptor = dialog.descriptor();
    descriptor.name = uniqueSessionName(descriptor.name);

    Error error;
    QString id;
    if (!m_sessions->open(descriptor, &id, &error)) {
        QMessageBox::warning(this, QStringLiteral("无法打开会话"), error.message);
    }
}

void MainWindow::openLoopbackSession(const QString &preferredName)
{
    SessionDescriptor descriptor;
    descriptor.transport = QStringLiteral("loopback");
    descriptor.config.portName = QStringLiteral("LOOPBACK");
    descriptor.config.baudRate = 115200;
    descriptor.name =
        uniqueSessionName(preferredName.isEmpty() ? QStringLiteral("回环测试") : preferredName);

    Error error;
    QString id;
    if (!m_sessions->open(descriptor, &id, &error)) {
        statusBar()->showMessage(error.message, 5000);
        appendOutput(error.message, true);
    }
}

QString MainWindow::uniqueSessionName(const QString &base) const
{
    QString name = base.trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("会话");
    }
    QStringList existing;
    for (const SessionInfo &info : m_sessions->list()) {
        existing.append(info.name);
    }
    if (!existing.contains(name)) {
        return name;
    }
    int suffix = 2;
    while (existing.contains(QStringLiteral("%1-%2").arg(name).arg(suffix))) {
        ++suffix;
    }
    return QStringLiteral("%1-%2").arg(name).arg(suffix);
}

void MainWindow::closeSession(const QString &sessionId)
{
    Error error;
    if (!m_sessions->close(sessionId, &error)) {
        statusBar()->showMessage(error.message, 4000);
        appendOutput(error.message, true);
    }
}

void MainWindow::selectSession(const QString &sessionId)
{
    auto it = m_consoles.find(sessionId);
    if (it == m_consoles.end()) {
        ISession *session = m_sessions->find(sessionId);
        if (!session) {
            return;
        }
        QVariantMap info;
        info.insert(QStringLiteral("id"), session->id());
        info.insert(QStringLiteral("name"), session->name());
        addSessionTab(info);
        it = m_consoles.find(sessionId);
        if (it == m_consoles.end()) {
            return;
        }
    if (SessionDockWidget *floating = m_floatingSessions.value(sessionId, nullptr)) {
        floating->show();
        floating->raise();
        floating->activateWindow();
        m_sessions->setActiveId(sessionId);
        return;
    }
    }
    m_centerTabs->setCurrentWidget(it.value());
    m_sessions->setActiveId(sessionId);
    updateStatus();
}

ConsoleView *MainWindow::currentConsole() const
{
    if (SessionWorkspace *workspace = currentWorkspace()) {
        return workspace->console();
    }
    return nullptr;
}

SessionWorkspace *MainWindow::currentWorkspace() const
{
    const QString activeId = m_sessions ? m_sessions->activeId() : QString();
    if (!activeId.isEmpty()) {
        if (SessionDockWidget *detached = m_floatingSessions.value(activeId, nullptr)) {
            if (detached->isFloating()) return m_consoles.value(activeId, nullptr);
        }
    }
    if (auto *workspace = qobject_cast<SessionWorkspace *>(m_centerTabs->currentWidget()))
        return workspace;
    return activeId.isEmpty() ? nullptr : m_consoles.value(activeId, nullptr);
}

ISession *MainWindow::currentSession() const
{
    if (SessionWorkspace *workspace = currentWorkspace()) {
        return m_sessions->find(workspace->sessionId());
    }
    const QString activeId = m_sessions->activeId();
    return activeId.isEmpty() ? nullptr : m_sessions->find(activeId);
}

void MainWindow::updateStatus()
{
    ISession *session = currentSession();
    if (!session) {
        m_statusSession->setText(QStringLiteral("无会话"));
        m_statusStats->clear();
    } else {
        const QString transport = session->transport() == QStringLiteral("loopback")
                                      ? QStringLiteral("回环")
                                      : session->config().portName;
        m_statusSession->setText(QStringLiteral("%1 %2 · %3 · %4 8N1")
                                     .arg(sessionStateIcon(session->state()), session->name(),
                                          transport)
                                     .arg(session->config().baudRate));
        const SessionStats stats = session->stats();
        m_statusStats->setText(QStringLiteral("RX %1 · TX %2")
                                   .arg(formatBytes(stats.rxBytes), formatBytes(stats.txBytes)));
    }

    if (m_plugins) {
        m_statusPlugins->setText(QStringLiteral("插件 %1/%2")
                                     .arg(m_plugins->activeCount())
                                     .arg(m_plugins->plugins().size()));
    }
}

void MainWindow::appendOutput(const QString &text, bool error)
{
    if (!m_output) {
        return;
    }
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString prefix = error ? QStringLiteral("[错误] ") : QString();
    m_output->appendPlainText(QStringLiteral("[%1] %2%3").arg(time, prefix, text));
}

void MainWindow::showPalette()
{
    if (m_palette) {
        m_palette->showPalette();
    }
}

void MainWindow::executeCommand(const QString &commandId, const QVariantMap &args)
{
    const CommandResult result = executeCommandResult(commandId, args);
    if (!result.ok && !result.message.isEmpty()) {
        statusBar()->showMessage(result.message, 5000);
        appendOutput(QStringLiteral("[命令失败] %1").arg(result.message), true);
    }
}

CommandResult MainWindow::executeCommandResult(const QString &commandId,
                                               const QVariantMap &args)
{
    CommandInvocation invocation;
    invocation.id = commandId;
    invocation.args = args;
    invocation.source = QStringLiteral("ui");
    CommandResult result;
    m_commands->execute(invocation, &result);
    return result;
}

void MainWindow::restoreSettings()
{
    if (!m_settings) {
        return;
    }
    const QByteArray geometry =
        m_settings->value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QByteArray state = m_settings->value(QStringLiteral("window/state")).toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }
    const QString view = m_settings->value(QStringLiteral("ui/sidebarView")).toString();
    if (!view.isEmpty() && m_sidebarPages.contains(view)) {
        showSidebarView(view);
    }
    // 会话标签页可能在恢复窗口状态之前就建好了，这里再对齐一次面板开关。
    const bool consoleVisible =
        m_settings->value(QStringLiteral("ui/consoleVisible"), true).toBool();
    const bool bottomVisible = m_settings->value(QStringLiteral("ui/bottomVisible"), true).toBool();
    const bool parserVisible = m_settings->value(QStringLiteral("ui/parserVisible"), true).toBool();
    for (SessionWorkspace *workspace : m_consoles) {
        workspace->setConsoleVisible(consoleVisible);
        workspace->setBottomVisible(bottomVisible);
        workspace->setParserVisible(parserVisible);
    }
}

void MainWindow::saveSettings()
{
    if (!m_settings) {
        return;
    }
    m_settings->setValue(QStringLiteral("window/geometry"), saveGeometry());
    m_settings->setValue(QStringLiteral("window/state"), saveState());
    m_settings->setValue(QStringLiteral("ui/sidebarView"), m_activeSidebarView);
    // 面板开关同样记进设置：不论是「视图」菜单还是工作区面板栏上的按钮改的，都算最后一次布局。
    if (SessionWorkspace *workspace = currentWorkspace()) {
        m_settings->setValue(QStringLiteral("ui/consoleVisible"), workspace->isConsoleVisible());
        m_settings->setValue(QStringLiteral("ui/bottomVisible"), workspace->isBottomVisible());
        m_settings->setValue(QStringLiteral("ui/parserVisible"), workspace->isParserVisible());
    }
    m_settings->sync();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_statusTimer) {
        m_statusTimer->stop();
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
#ifdef Q_OS_WIN
    // 等原生 HWND 创建完成后缓存句柄，避免在窗口创建消息中调用 winId() 引发重入。
    m_resizeWindowId = reinterpret_cast<quintptr>(winId());
#endif
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (message && result
        && (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG")) {
        auto *msg = static_cast<MSG *>(message);
        const HWND hwnd = reinterpret_cast<HWND>(m_resizeWindowId);
        if (hwnd && msg->hwnd == hwnd) {
            switch (msg->message) {
            case WM_NCHITTEST: {
                if (m_liveWindowResize) {
                    *result = HTCLIENT;
                    return true;
                }
                if (IsZoomed(hwnd)) {
                    m_resizeEdges = 0;
                    break;
                }

                RECT bounds{};
                if (!GetWindowRect(hwnd, &bounds)) break;
                const int borderX = qMax(6, GetSystemMetrics(SM_CXSIZEFRAME)
                                                + GetSystemMetrics(SM_CXPADDEDBORDER));
                const int borderY = qMax(6, GetSystemMetrics(SM_CYSIZEFRAME)
                                                + GetSystemMetrics(SM_CXPADDEDBORDER));
                const int x = GET_X_LPARAM(msg->lParam);
                const int y = GET_Y_LPARAM(msg->lParam);
                int edges = 0;
                if (x >= bounds.left && x < bounds.left + borderX) edges |= kResizeLeft;
                else if (x < bounds.right && x >= bounds.right - borderX) edges |= kResizeRight;
                if (y >= bounds.top && y < bounds.top + borderY) edges |= kResizeTop;
                else if (y < bounds.bottom && y >= bounds.bottom - borderY) edges |= kResizeBottom;

                m_resizeEdges = edges;
                if (edges != 0) {
                    // 将边框命中交给本窗口处理，避免 Windows 使用全局橡皮筋尺寸预览。
                    *result = HTCLIENT;
                    return true;
                }
                break;
            }
            case WM_SETCURSOR:
                if (m_resizeEdges != 0 && LOWORD(msg->lParam) == HTCLIENT) {
                    const bool horizontal = (m_resizeEdges & (kResizeLeft | kResizeRight)) != 0;
                    const bool vertical = (m_resizeEdges & (kResizeTop | kResizeBottom)) != 0;
                    LPCWSTR cursorId = IDC_ARROW;
                    if (horizontal && vertical) {
                        const bool northwestSoutheast =
                            ((m_resizeEdges & kResizeLeft) && (m_resizeEdges & kResizeTop))
                            || ((m_resizeEdges & kResizeRight) && (m_resizeEdges & kResizeBottom));
                        cursorId = northwestSoutheast ? IDC_SIZENWSE : IDC_SIZENESW;
                    } else if (horizontal) {
                        cursorId = IDC_SIZEWE;
                    } else if (vertical) {
                        cursorId = IDC_SIZENS;
                    }
                    SetCursor(LoadCursorW(nullptr, cursorId));
                    *result = TRUE;
                    return true;
                }
                break;
            case WM_LBUTTONDOWN:
                if (m_resizeEdges != 0 && !IsZoomed(hwnd)) {
                    RECT bounds{};
                    POINT cursor{};
                    if (GetWindowRect(hwnd, &bounds) && GetCursorPos(&cursor)) {
                        m_resizeStartFrame = QRect(bounds.left, bounds.top,
                                                   bounds.right - bounds.left,
                                                   bounds.bottom - bounds.top);
                        m_resizeStartCursor = QPoint(cursor.x, cursor.y);
                        m_liveWindowResize = true;
                        SetCapture(hwnd);
                        *result = 0;
                        return true;
                    }
                }
                break;
            case WM_MOUSEMOVE:
                if (m_liveWindowResize) {
                    POINT cursor{};
                    if (GetCursorPos(&cursor)) {
                        const int dx = cursor.x - m_resizeStartCursor.x();
                        const int dy = cursor.y - m_resizeStartCursor.y();
                        int left = m_resizeStartFrame.x();
                        int top = m_resizeStartFrame.y();
                        int right = left + m_resizeStartFrame.width();
                        int bottom = top + m_resizeStartFrame.height();
                        if (m_resizeEdges & kResizeLeft) left += dx;
                        if (m_resizeEdges & kResizeRight) right += dx;
                        if (m_resizeEdges & kResizeTop) top += dy;
                        if (m_resizeEdges & kResizeBottom) bottom += dy;

                        const int borderX = qMax(6, GetSystemMetrics(SM_CXSIZEFRAME)
                                                        + GetSystemMetrics(SM_CXPADDEDBORDER));
                        const int borderY = qMax(6, GetSystemMetrics(SM_CYSIZEFRAME)
                                                        + GetSystemMetrics(SM_CXPADDEDBORDER));
                        const QSize minimum = minimumSize().expandedTo(minimumSizeHint());
                        const qreal scale = devicePixelRatioF();
                        const int minWidth = qRound(minimum.width() * scale) + borderX * 2;
                        const int minHeight = qRound(minimum.height() * scale)
                                              + GetSystemMetrics(SM_CYCAPTION) + borderY * 2;
                        if (right - left < minWidth) {
                            if (m_resizeEdges & kResizeLeft) left = right - minWidth;
                            else right = left + minWidth;
                        }
                        if (bottom - top < minHeight) {
                            if (m_resizeEdges & kResizeTop) top = bottom - minHeight;
                            else bottom = top + minHeight;
                        }

                        SetWindowPos(hwnd, nullptr, left, top, right - left, bottom - top,
                                     SWP_NOZORDER | SWP_NOACTIVATE);
                    }
                    *result = 0;
                    return true;
                }
                break;
            case WM_LBUTTONUP:
                if (m_liveWindowResize) {
                    m_liveWindowResize = false;
                    m_resizeEdges = 0;
                    if (GetCapture() == hwnd) ReleaseCapture();
                    *result = 0;
                    return true;
                }
                break;
            case WM_CAPTURECHANGED:
            case WM_CANCELMODE:
                if (m_liveWindowResize) {
                    m_liveWindowResize = false;
                    m_resizeEdges = 0;
                }
                break;
            }
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (!m_draggingDetachedSession.isEmpty()) {
        SessionDockWidget *dock = m_floatingSessions.value(m_draggingDetachedSession, nullptr);
        if (!dock) {
            m_draggingDetachedSession.clear();
        } else if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->buttons() & Qt::LeftButton) {
                const QRect centerRect(m_centerTabs->mapToGlobal(QPoint(0, 0)), m_centerTabs->size());
                if (!centerRect.contains(mouse->globalPosition().toPoint()))
                    m_detachDragLeftWorkspace = true;
                dock->move(mouse->globalPosition().toPoint() - m_detachDragOffset);
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QString id = m_draggingDetachedSession;
            m_draggingDetachedSession.clear();
            dock->unlockFloatingSize();
            const QRect centerRect(m_centerTabs->mapToGlobal(QPoint(0, 0)), m_centerTabs->size());
            if (m_detachDragLeftWorkspace && centerRect.contains(mouse->globalPosition().toPoint()))
                QTimer::singleShot(0, this, [this, id] { dockSessionTab(id); });
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

} // namespace xcom
