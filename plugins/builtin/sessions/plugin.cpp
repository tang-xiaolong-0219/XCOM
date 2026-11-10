#include "xcom/api/plugin.h"
#include "xcom/api/ui.h"

#include <QAction>
#include <QColor>
#include <QHBoxLayout>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QtPlugin>

#include <memory>

using namespace xcom;

namespace {

class SessionExplorerView : public QWidget
{
public:
    SessionExplorerView(ISessionService *sessions, IViewHost *host, QWidget *parent)
        : QWidget(parent)
        , m_sessions(sessions)
        , m_host(host)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(4);

        auto *toolbar = new QHBoxLayout;
        auto *newButton = new QPushButton(QStringLiteral("新建会话"), this);
        auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
        toolbar->addWidget(newButton);
        toolbar->addWidget(refreshButton);
        toolbar->addStretch();

        m_tree = new QTreeWidget(this);
        m_tree->setHeaderLabels({QStringLiteral("会话"), QStringLiteral("详情")});
        m_tree->setRootIsDecorated(false);
        m_tree->setAlternatingRowColors(true);
        m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

        layout->addLayout(toolbar);
        layout->addWidget(m_tree, 1);

        connect(newButton, &QPushButton::clicked, this, [this] {
            m_host->executeCommand(QStringLiteral("session.openLoopback"),
                                   {{QStringLiteral("name"), QStringLiteral("回环测试")}});
        });
        connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
        connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
                [this](QTreeWidgetItem *item, int) {
                    if (item) {
                        m_host->executeCommand(
                            QStringLiteral("session.focus"),
                            {{QStringLiteral("session"), item->data(0, Qt::UserRole).toString()}});
                    }
                });
        connect(m_tree, &QWidget::customContextMenuRequested, this,
                [this](const QPoint &pos) {
                    QTreeWidgetItem *item = m_tree->itemAt(pos);
                    if (!item) {
                        return;
                    }
                    const QString id = item->data(0, Qt::UserRole).toString();
                    QMenu menu(m_tree);
                    // 「打开控制台」= 切到该会话并展开控制台；
                    // 单独调用 session.focus 只会切换标签页，用户会觉得「点了没反应」。
                    menu.addAction(QStringLiteral("打开控制台"), this, [this, id] {
                        const CommandResult result = m_host->executeCommand(
                            QStringLiteral("console.open"),
                            {{QStringLiteral("session"), id}});
                        if (!result.ok) {
                            m_host->executeCommand(QStringLiteral("session.focus"),
                                                   {{QStringLiteral("session"), id}});
                        }
                    });
                    menu.addAction(QStringLiteral("浮动控制台"), this, [this, id] {
                        m_host->executeCommand(QStringLiteral("session.focus"),
                                               {{QStringLiteral("session"), id}});
                        m_host->executeCommand(QStringLiteral("console.float"));
                    });
                    menu.addAction(QStringLiteral("关闭会话"), this, [this, id] {
                        m_host->executeCommand(
                            QStringLiteral("session.close"),
                            {{QStringLiteral("session"), id}});
                    });
                    menu.exec(m_tree->mapToGlobal(pos));
                });

        refresh();
    }

    void refresh()
    {
        const QString selected = m_tree->currentItem()
                                     ? m_tree->currentItem()->data(0, Qt::UserRole).toString()
                                     : QString();
        m_tree->clear();
        if (!m_sessions) {
            return;
        }
        for (const SessionInfo &info : m_sessions->list()) {
            auto *item = new QTreeWidgetItem(m_tree);
            item->setText(0, sessionStateIcon(info.state) + QLatin1Char(' ') + info.name);
            const QString transport = info.transport == QStringLiteral("loopback")
                                          ? QStringLiteral("回环")
                                          : info.config.portName;
            item->setText(1, QStringLiteral("%1 · %2 · %3")
                                 .arg(transport)
                                 .arg(info.config.baudRate)
                                 .arg(sessionStateName(info.state)));
            item->setData(0, Qt::UserRole, info.id);
            if (info.state == SessionState::Failed) {
                item->setForeground(0, QColor(0xf4, 0x87, 0x71));
            } else if (info.state == SessionState::Open) {
                item->setForeground(0, QColor(0x89, 0xd1, 0x85));
            } else {
                item->setForeground(0, QColor(0x9d, 0x9d, 0x9d));
            }
            item->setToolTip(0, QStringLiteral("%1 (%2)").arg(info.name, info.id));
            if (info.id == selected) {
                m_tree->setCurrentItem(item);
            }
        }
        if (m_tree->topLevelItemCount() == 0) {
            auto *empty = new QTreeWidgetItem(m_tree);
            empty->setText(0, QStringLiteral("(没有会话)"));
            empty->setFlags(Qt::NoItemFlags);
            empty->setForeground(0, QColor(0x9d, 0x9d, 0x9d));
        }
    }

private:
    ISessionService *m_sessions = nullptr;
    IViewHost *m_host = nullptr;
    QTreeWidget *m_tree = nullptr;
};

} // namespace

class SessionsPlugin : public QObject, public IPlugin, public IEventHandler
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID XCOM_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(xcom::IPlugin)

public:
    explicit SessionsPlugin(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~SessionsPlugin() override
    {
        unsubscribeAll();
    }

    bool initialize(IPluginContext &context, Error &error) override
    {
        m_context = &context;
        IUiContext *ui = context.ui();
        if (!ui) {
            error = Error::make(ErrorCode::NotSupported,
                                QStringLiteral("会话资源管理器需要图形界面"));
            return false;
        }

        class Factory : public IViewFactory
        {
        public:
            Factory(SessionsPlugin *plugin, ISessionService *sessions)
                : m_plugin(plugin)
                , m_sessions(sessions)
            {
            }

            QWidget *create(QWidget *parent, IViewHost *host) override
            {
                auto *view = new SessionExplorerView(m_sessions, host, parent);
                m_plugin->m_view = view;
                return view;
            }

        private:
            SessionsPlugin *m_plugin = nullptr;
            ISessionService *m_sessions = nullptr;
        };

        ViewSpec spec;
        spec.id = QStringLiteral("com.xcom.sessions.explorer");
        spec.title = QStringLiteral("会话");
        spec.icon = QStringLiteral("sessions");
        spec.location = ViewLocation::Sidebar;
        spec.order = 100;

        auto factory = std::make_unique<Factory>(this, &context.sessions());
        if (!ui->views().registerView(spec, factory.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("视图 com.xcom.sessions.explorer 注册失败"));
            return false;
        }
        m_factory = std::move(factory);
        return true;
    }

    bool activate(Error &error) override
    {
        Q_UNUSED(error)
        if (!m_context) {
            return false;
        }
        if (m_view) {
            m_view->refresh();
        }
        const QStringList topics = {
            QStringLiteral("session/added"),        QStringLiteral("session/closed"),
            QStringLiteral("session/stateChanged"), QStringLiteral("session/activeChanged")};
        for (const QString &topic : topics) {
            if (ISubscription *subscription = m_context->events().subscribe(topic, this)) {
                m_subscriptions.append(subscription);
            }
        }
        return true;
    }

    void deactivate() override
    {
        unsubscribeAll();
    }

    void shutdown() override
    {
        unsubscribeAll();
        m_view = nullptr;
        m_context = nullptr;
    }

    void onEvent(const Event &event) override
    {
        Q_UNUSED(event)
        if (m_view) {
            m_view->refresh();
        }
    }

private:
    void unsubscribeAll()
    {
        const QVector<ISubscription *> subscriptions = m_subscriptions;
        m_subscriptions.clear();
        for (ISubscription *subscription : subscriptions) {
            if (subscription) {
                subscription->unsubscribe();
            }
        }
    }

    IPluginContext *m_context = nullptr;
    std::unique_ptr<IViewFactory> m_factory;
    QPointer<SessionExplorerView> m_view;
    QVector<ISubscription *> m_subscriptions;
};
#include "plugin.moc"
