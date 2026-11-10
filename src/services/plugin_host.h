#pragma once

#include "xcom/api/plugin.h"

#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

namespace xcom {

class PluginContext;

class PluginHost : public QObject, public IPluginHost
{
    Q_OBJECT

public:
    struct Environment {
        ICommandRegistry *commands = nullptr;
        ISessionService *sessions = nullptr;
        ISettingsService *settings = nullptr;
        IEventBus *events = nullptr;
        ILogger *logger = nullptr;
        INotificationService *notifications = nullptr;
        IUiContext *ui = nullptr;
    };

    explicit PluginHost(Environment environment, QObject *parent = nullptr);
    ~PluginHost() override;

    void setSearchPaths(const QStringList &paths);
    void scanAndLoad();
    // 退出前调用：停用全部插件并撤销贡献，避免宿主界面先于插件销毁。
    void shutdownAll();

    QVector<PluginInfo> plugins() const override;
    bool setEnabled(const QString &pluginId, bool enabled) override;
    void rescan() override;

    int activeCount() const;
    QStringList errors() const;

signals:
    void pluginsChanged();

private:
    struct Record;

    Record *find(const QString &pluginId);
    void scan();
    bool load(Record &record);
    bool initialize(Record &record);
    void deactivate(Record &record);
    void updateDisabledSetting();
    QString dataDirFor(const QString &pluginId) const;

    Environment m_env;
    QStringList m_searchPaths;
    std::vector<std::unique_ptr<Record>> m_records;
};

} // namespace xcom
