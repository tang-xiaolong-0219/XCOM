#include "services/plugin_host.h"

#include "foundation/log.h"
#include "services/plugin_context.h"
#include "xcom/api/version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QSet>
#include <QDebug>

namespace xcom {

struct PluginHost::Record {
    PluginInfo info;
    QString directory;
    QString libraryPath;
    QPluginLoader *loader = nullptr;
    IPlugin *plugin = nullptr;
    std::unique_ptr<PluginContext> context;
    bool active = false;
    bool shutdownDone = false;
};

namespace {

struct Manifest {
    QString id;
    QString name;
    QString version;
    QString apiVersion;
    QString entry;
    QString description;
    QString buildQt;
    bool headless = false;
    bool valid = false;
};

Manifest readManifest(const QString &pluginDir)
{
    Manifest manifest;
    QFile file(QDir(pluginDir).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return manifest;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return manifest;
    }
    const QJsonObject object = document.object();
    manifest.id = object.value(QStringLiteral("id")).toString();
    manifest.name = object.value(QStringLiteral("name")).toString();
    manifest.version = object.value(QStringLiteral("version")).toString();
    manifest.apiVersion = object.value(QStringLiteral("apiVersion")).toString();
    manifest.entry = object.value(QStringLiteral("entry")).toString();
    manifest.description = object.value(QStringLiteral("description")).toString();
    manifest.buildQt =
        object.value(QStringLiteral("build")).toObject().value(QStringLiteral("qt")).toString();
    manifest.headless = object.value(QStringLiteral("headless")).toBool(false);
    manifest.valid = !manifest.id.isEmpty() && !manifest.entry.isEmpty();
    return manifest;
}

bool isApiCompatible(const QString &apiVersion, QString *reason)
{
    QString normalized = apiVersion;
    normalized.remove(QLatin1Char('^'));
    normalized = normalized.trimmed();
    bool ok = false;
    const int major = normalized.section(QLatin1Char('.'), 0, 0).toInt(&ok);
    if (!ok || major != XCOM_API_VERSION_MAJOR) {
        if (reason) {
            *reason = QStringLiteral("需要 API %1.x，宿主为 %2")
                          .arg(XCOM_API_VERSION_MAJOR)
                          .arg(apiVersion);
        }
        return false;
    }
    return true;
}

} // namespace

PluginHost::PluginHost(Environment environment, QObject *parent)
    : QObject(parent)
    , m_env(environment)
{
}

PluginHost::~PluginHost()
{
    for (const auto &record : m_records) {
        if (record->plugin && !record->shutdownDone) {
            if (record->active) {
                record->plugin->deactivate();
            }
            record->plugin->shutdown();
            record->shutdownDone = true;
        }
        if (record->context) {
            record->context->revokeAll();
            record->context.reset();
        }
        if (record->loader) {
            record->loader->unload();
            delete record->loader;
            record->loader = nullptr;
        }
    }
    m_records.clear();
}

void PluginHost::shutdownAll()
{
    for (const auto &record : m_records) {
        if (record->active && record->plugin) {
            record->plugin->deactivate();
            record->active = false;
        }
        if (record->context) {
            record->context->revokeAll();
        }
        if (record->plugin && !record->shutdownDone) {
            record->plugin->shutdown();
            record->shutdownDone = true;
        }
        record->info.state = QStringLiteral("Disabled");
    }
    emit pluginsChanged();
}

void PluginHost::setSearchPaths(const QStringList &paths)
{
    m_searchPaths = paths;
}

void PluginHost::scanAndLoad()
{
    scan();
    for (const auto &record : m_records) {
        if (record->info.enabled && record->info.state != QStringLiteral("Incompatible")) {
            load(*record);
        }
    }
    emit pluginsChanged();
}

void PluginHost::scan()
{
    m_records.clear();

    QStringList paths = m_searchPaths;
    if (paths.isEmpty()) {
        const QStringList envPaths = qEnvironmentVariable("XCOM_PLUGIN_PATH")
                                         .split(QLatin1Char(';'), Qt::SkipEmptyParts);
        paths << envPaths;
        paths << QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
        paths << appDataDir() + QStringLiteral("/plugins");
    }

    const QStringList disabled = m_env.settings
                                     ? m_env.settings->value(QStringLiteral("plugins/disabled"))
                                           .toStringList()
                                     : QStringList();

    QSet<QString> seenIds;
    QSet<QString> seenDirs;

    for (const QString &basePath : paths) {
        const QString absoluteBase = QDir(basePath).absolutePath();
        if (absoluteBase.isEmpty() || seenDirs.contains(absoluteBase)) {
            continue;
        }
        seenDirs.insert(absoluteBase);

        const QDir baseDir(absoluteBase);
        if (!baseDir.exists()) {
            continue;
        }
        const QFileInfoList entries =
            baseDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const Manifest manifest = readManifest(entry.absoluteFilePath());
            if (!manifest.valid) {
                continue;
            }
            if (seenIds.contains(manifest.id)) {
                qWarning() << "duplicate plugin id, ignored:" << manifest.id
                           << entry.absoluteFilePath();
                continue;
            }
            seenIds.insert(manifest.id);

            auto record = std::make_unique<Record>();
            record->info.id = manifest.id;
            record->info.name = manifest.name.isEmpty() ? manifest.id : manifest.name;
            record->info.version = manifest.version;
            record->info.apiVersion = manifest.apiVersion;
            record->info.entry = manifest.entry;
            record->info.description = manifest.description;
            record->info.path = entry.absoluteFilePath();
            record->directory = entry.absoluteFilePath();
            record->libraryPath = QDir(record->directory).filePath(manifest.entry);

            QString incompatibleReason;
            bool compatible = isApiCompatible(manifest.apiVersion, &incompatibleReason);
            if (compatible && !manifest.buildQt.isEmpty() && manifest.buildQt != qVersion()) {
                compatible = false;
                incompatibleReason =
                    QStringLiteral("Qt 版本不一致：插件 %1，宿主 %2")
                        .arg(manifest.buildQt, QString::fromLatin1(qVersion()));
            }
            if (!compatible) {
                record->info.enabled = false;
                record->info.state = QStringLiteral("Incompatible");
                record->info.error = incompatibleReason;
            } else if (!m_env.ui && !manifest.headless) {
                // 无界面模式只加载声明了 headless 的插件，UI 插件直接跳过而不是报错。
                record->info.enabled = false;
                record->info.state = QStringLiteral("Disabled");
                record->info.error = QStringLiteral("仅供图形界面使用（headless=false）");
            } else {
                record->info.enabled = !disabled.contains(manifest.id);
                record->info.state = record->info.enabled ? QStringLiteral("Discovered")
                                                          : QStringLiteral("Disabled");
            }

            m_records.push_back(std::move(record));
        }
    }
}

bool PluginHost::load(Record &record)
{
    if (record.plugin) {
        return initialize(record);
    }
    if (record.info.state == QStringLiteral("Incompatible")) {
        return false;
    }
    if (record.libraryPath.isEmpty()) {
        record.info.state = QStringLiteral("Failed");
        record.info.error = QStringLiteral("manifest 缺少 entry 字段");
        return false;
    }

    auto *loader = new QPluginLoader(record.libraryPath, this);
    QObject *instance = loader->instance();
    if (!instance) {
        record.loader = loader;
        record.info.state = QStringLiteral("Failed");
        record.info.error = loader->errorString();
        qWarning() << "plugin load failed:" << record.info.id << loader->errorString();
        return false;
    }

    auto *plugin = qobject_cast<IPlugin *>(instance);
    if (!plugin) {
        record.loader = loader;
        record.info.state = QStringLiteral("Failed");
        record.info.error = QStringLiteral("插件未实现 IPlugin 接口");
        return false;
    }

    record.loader = loader;
    record.plugin = plugin;
    record.context = std::make_unique<PluginContext>(
        this, record.info.id, dataDirFor(record.info.id), m_env.commands, m_env.sessions,
        m_env.settings, m_env.events, m_env.logger, m_env.notifications, m_env.ui);
    return initialize(record);
}

bool PluginHost::initialize(Record &record)
{
    if (!record.plugin) {
        return false;
    }
    if (!record.context) {
        record.context = std::make_unique<PluginContext>(
            this, record.info.id, dataDirFor(record.info.id), m_env.commands, m_env.sessions,
            m_env.settings, m_env.events, m_env.logger, m_env.notifications, m_env.ui);
    }

    record.context->revokeAll();

    Error error;
    if (!record.plugin->initialize(*record.context, error)) {
        record.info.state = QStringLiteral("Failed");
        record.info.error = error.message;
        record.context->revokeAll();
        qWarning() << "plugin initialize failed:" << record.info.id << error.message;
        return false;
    }
    if (!record.plugin->activate(error)) {
        record.info.state = QStringLiteral("Failed");
        record.info.error = error.message;
        record.plugin->deactivate();
        record.context->revokeAll();
        qWarning() << "plugin activate failed:" << record.info.id << error.message;
        return false;
    }

    record.active = true;
    record.info.state = QStringLiteral("Active");
    record.info.error.clear();
    record.info.contributions = record.context->contributionCount();
    return true;
}

void PluginHost::deactivate(Record &record)
{
    if (record.plugin && record.active) {
        record.plugin->deactivate();
    }
    record.active = false;
    if (record.context) {
        record.context->revokeAll();
    }
}

QVector<PluginInfo> PluginHost::plugins() const
{
    QVector<PluginInfo> result;
    result.reserve(static_cast<int>(m_records.size()));
    for (const auto &record : m_records) {
        result.append(record->info);
    }
    return result;
}

bool PluginHost::setEnabled(const QString &pluginId, bool enabled)
{
    Record *record = find(pluginId);
    if (!record) {
        return false;
    }

    if (enabled) {
        if (record->active) {
            return true;
        }
        const bool ok = initialize(*record);
        record->info.enabled = ok;
        if (!ok && record->info.state != QStringLiteral("Incompatible")) {
            record->info.state = QStringLiteral("Failed");
            record->info.enabled = false;
        }
        updateDisabledSetting();
        emit pluginsChanged();
        return ok;
    }

    deactivate(*record);
    record->info.enabled = false;
    record->info.state = QStringLiteral("Disabled");
    updateDisabledSetting();
    emit pluginsChanged();
    return true;
}

void PluginHost::rescan()
{
    for (const auto &record : m_records) {
        if (record->loader) {
            qWarning() << "rescan requires restart: plugins already loaded";
            return;
        }
    }
    scanAndLoad();
}

int PluginHost::activeCount() const
{
    int count = 0;
    for (const auto &record : m_records) {
        if (record->active) {
            ++count;
        }
    }
    return count;
}

QStringList PluginHost::errors() const
{
    QStringList result;
    for (const auto &record : m_records) {
        if (record->info.state == QStringLiteral("Failed")
            || record->info.state == QStringLiteral("Incompatible")) {
            result.append(QStringLiteral("%1: %2").arg(record->info.id, record->info.error));
        }
    }
    return result;
}

PluginHost::Record *PluginHost::find(const QString &pluginId)
{
    for (const auto &record : m_records) {
        if (record->info.id == pluginId) {
            return record.get();
        }
    }
    return nullptr;
}

void PluginHost::updateDisabledSetting()
{
    if (!m_env.settings) {
        return;
    }
    QVariantList disabled;
    for (const auto &record : m_records) {
        if (!record->info.enabled && record->info.state == QStringLiteral("Disabled")) {
            disabled.append(record->info.id);
        }
    }
    m_env.settings->setValue(QStringLiteral("plugins/disabled"), disabled);
    m_env.settings->sync();
}

QString PluginHost::dataDirFor(const QString &pluginId) const
{
    return ensureDir(appDataDir() + QStringLiteral("/plugins/") + pluginId);
}

} // namespace xcom
