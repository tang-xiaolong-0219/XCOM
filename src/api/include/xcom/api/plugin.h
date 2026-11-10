#pragma once

#include "xcom/api/commands.h"
#include "xcom/api/events.h"
#include "xcom/api/services.h"
#include "xcom/api/sessions.h"
#include "xcom/api/settings.h"
#include "xcom/api/version.h"

#include <QtPlugin>

namespace xcom {

class IUiContext;

class IPluginContext
{
public:
    virtual ~IPluginContext() = default;

    virtual QString pluginId() const = 0;
    virtual QString dataDir() const = 0;
    virtual ICommandRegistry &commands() = 0;
    virtual ISessionService &sessions() = 0;
    virtual ISettingsService &settings() = 0;
    virtual IEventBus &events() = 0;
    virtual ILogger &log() = 0;
    virtual INotificationService &notifications() = 0;
    // 无界面模式下返回 nullptr，插件必须判空。
    virtual IUiContext *ui() = 0;
};

class IPlugin
{
public:
    virtual ~IPlugin() = default;

    // 只注册贡献，禁止耗时操作。
    virtual bool initialize(IPluginContext &context, Error &error) = 0;
    // 首次使用前的资源准备。
    virtual bool activate(Error &error) = 0;
    // 释放运行资源，注册由宿主统一撤销。
    virtual void deactivate() = 0;
    // 最终清理。
    virtual void shutdown() = 0;
};

class IPluginHost
{
public:
    virtual ~IPluginHost() = default;

    virtual QVector<PluginInfo> plugins() const = 0;
    virtual bool setEnabled(const QString &pluginId, bool enabled) = 0;
    virtual void rescan() = 0;
};

} // namespace xcom

Q_DECLARE_INTERFACE(xcom::IPlugin, XCOM_PLUGIN_IID)
