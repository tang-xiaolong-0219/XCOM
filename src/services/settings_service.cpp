#include "services/settings_service.h"

#include "foundation/log.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

namespace xcom {

SettingsService::SettingsService(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_path(filePath)
{
    m_syncTimer = new QTimer(this);
    m_syncTimer->setSingleShot(true);
    m_syncTimer->setInterval(400);
    connect(m_syncTimer, &QTimer::timeout, this, &SettingsService::sync);
    load();
}

SettingsService::~SettingsService()
{
    sync();
}

QVariant SettingsService::value(const QString &key, const QVariant &defaultValue) const
{
    return m_values.value(key, defaultValue);
}

void SettingsService::setValue(const QString &key, const QVariant &value)
{
    if (m_values.value(key) == value) {
        return;
    }
    m_values.insert(key, value);
    scheduleSync();
}

void SettingsService::sync()
{
    if (m_syncTimer->isActive()) {
        m_syncTimer->stop();
    }

    QVariantMap map;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
        map.insert(it.key(), it.value());
    }

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "cannot write settings:" << m_path;
        return;
    }
    const QJsonDocument document(QJsonObject::fromVariantMap(map));
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning() << "cannot commit settings:" << m_path;
    }
}

void SettingsService::load()
{
    QFile file(m_path);
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "cannot read settings:" << m_path;
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        qWarning() << "invalid settings json:" << m_path;
        return;
    }
    const QVariantMap map = document.object().toVariantMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        m_values.insert(it.key(), it.value());
    }
}

void SettingsService::scheduleSync()
{
    if (!m_syncTimer->isActive()) {
        m_syncTimer->start();
    }
}

} // namespace xcom
