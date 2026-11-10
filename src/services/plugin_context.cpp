#include "services/plugin_context.h"

#include "foundation/log.h"

#include <QDir>
#include <QDebug>

namespace xcom {

class ProxyCommandRegistry : public ICommandRegistry
{
public:
    ProxyCommandRegistry(PluginContext *context, ICommandRegistry *inner)
        : m_context(context)
        , m_inner(inner)
    {
    }

    ISubscription *registerCommand(const CommandSpec &spec, ICommandHandler *handler) override
    {
        ISubscription *subscription = m_inner->registerCommand(spec, handler);
        if (subscription) {
            m_context->track(subscription);
        }
        return subscription;
    }

    bool execute(const CommandInvocation &invocation, CommandResult *result,
                 ICommandProgress *progress) override
    {
        return m_inner->execute(invocation, result, progress);
    }

    bool contains(const QString &commandId) const override
    {
        return m_inner->contains(commandId);
    }

    QVector<CommandSpec> commands() const override { return m_inner->commands(); }

private:
    PluginContext *m_context = nullptr;
    ICommandRegistry *m_inner = nullptr;
};

class ProxyViewRegistry : public IViewRegistry
{
public:
    ProxyViewRegistry(PluginContext *context, IViewRegistry *inner)
        : m_context(context)
        , m_inner(inner)
    {
    }

    ISubscription *registerView(const ViewSpec &spec, IViewFactory *factory) override
    {
        ISubscription *subscription = m_inner->registerView(spec, factory);
        if (subscription) {
            m_context->track(subscription);
        }
        return subscription;
    }

    bool show(const QString &viewId) override { return m_inner->show(viewId); }
    QVector<ViewSpec> views() const override { return m_inner->views(); }

private:
    PluginContext *m_context = nullptr;
    IViewRegistry *m_inner = nullptr;
};

class ProxyMenuRegistry : public IMenuRegistry
{
public:
    ProxyMenuRegistry(PluginContext *context, IMenuRegistry *inner)
        : m_context(context)
        , m_inner(inner)
    {
    }

    ISubscription *addItem(const MenuContribution &contribution) override
    {
        ISubscription *subscription = m_inner->addItem(contribution);
        if (subscription) {
            m_context->track(subscription);
        }
        return subscription;
    }

    QVector<MenuContribution> items() const override { return m_inner->items(); }

private:
    PluginContext *m_context = nullptr;
    IMenuRegistry *m_inner = nullptr;
};

class ProxyUiContext : public IUiContext
{
public:
    ProxyUiContext(PluginContext *context, IUiContext *inner)
        : m_inner(inner)
        , m_views(context, &inner->views())
        , m_menus(context, &inner->menus())
    {
    }

    IViewRegistry &views() override { return m_views; }
    IMenuRegistry &menus() override { return m_menus; }
    void showView(const QString &viewId) override { m_inner->showView(viewId); }
    void openSessionConsole(const QString &sessionId) override
    {
        m_inner->openSessionConsole(sessionId);
    }
    void refreshMenus() override { m_inner->refreshMenus(); }

private:
    IUiContext *m_inner = nullptr;
    ProxyViewRegistry m_views;
    ProxyMenuRegistry m_menus;
};

PluginContext::PluginContext(PluginHost *host, QString pluginId, QString dataDir,
                             ICommandRegistry *commands, ISessionService *sessions,
                             ISettingsService *settings, IEventBus *events, ILogger *logger,
                             INotificationService *notifications, IUiContext *ui)
    : m_host(host)
    , m_pluginId(std::move(pluginId))
    , m_dataDir(std::move(dataDir))
    , m_sessions(sessions)
    , m_settings(settings)
    , m_events(events)
    , m_logger(logger)
    , m_notifications(notifications)
{
    Q_UNUSED(m_host)
    m_commandProxy = std::make_unique<ProxyCommandRegistry>(this, commands);
    if (ui) {
        m_uiProxy = std::make_unique<ProxyUiContext>(this, ui);
    }
}

PluginContext::~PluginContext()
{
    revokeAll();
}

ICommandRegistry &PluginContext::commands()
{
    return *m_commandProxy;
}

IUiContext *PluginContext::ui()
{
    return m_uiProxy.get();
}

void PluginContext::track(ISubscription *subscription)
{
    if (subscription) {
        m_subscriptions.append(subscription);
    }
}

void PluginContext::revokeAll()
{
    const QVector<ISubscription *> subscriptions = m_subscriptions;
    m_subscriptions.clear();
    for (ISubscription *subscription : subscriptions) {
        if (subscription) {
            subscription->unsubscribe();
        }
    }
}

} // namespace xcom
