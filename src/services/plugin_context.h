#pragma once

#include "xcom/api/plugin.h"
#include "xcom/api/ui.h"

#include <QString>
#include <QVector>

#include <memory>

namespace xcom {

class PluginHost;
class ProxyCommandRegistry;
class ProxyUiContext;

class PluginContext : public IPluginContext
{
public:
    PluginContext(PluginHost *host, QString pluginId, QString dataDir,
                  ICommandRegistry *commands, ISessionService *sessions,
                  ISettingsService *settings, IEventBus *events, ILogger *logger,
                  INotificationService *notifications, IUiContext *ui);
    ~PluginContext() override;

    QString pluginId() const override { return m_pluginId; }
    QString dataDir() const override { return m_dataDir; }
    ICommandRegistry &commands() override;
    ISessionService &sessions() override { return *m_sessions; }
    ISettingsService &settings() override { return *m_settings; }
    IEventBus &events() override { return *m_events; }
    ILogger &log() override { return *m_logger; }
    INotificationService &notifications() override { return *m_notifications; }
    IUiContext *ui() override;

    void track(ISubscription *subscription);
    void revokeAll();
    int contributionCount() const { return m_subscriptions.size(); }

private:
    PluginHost *m_host = nullptr;
    QString m_pluginId;
    QString m_dataDir;
    ISessionService *m_sessions = nullptr;
    ISettingsService *m_settings = nullptr;
    IEventBus *m_events = nullptr;
    ILogger *m_logger = nullptr;
    INotificationService *m_notifications = nullptr;

    std::unique_ptr<ProxyCommandRegistry> m_commandProxy;
    std::unique_ptr<ProxyUiContext> m_uiProxy;
    QVector<ISubscription *> m_subscriptions;
};

} // namespace xcom
