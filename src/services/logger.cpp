#include "services/logger.h"

#include "xcom/api/events.h"

#include <QDateTime>
#include <QDebug>

namespace xcom {

Logger::Logger(QString name, IEventBus *bus)
    : m_name(std::move(name))
    , m_bus(bus)
{
}

void Logger::debug(const QString &message)
{
    write(QStringLiteral("debug"), message);
    qDebug().noquote() << QStringLiteral("[%1] %2").arg(m_name, message);
}

void Logger::info(const QString &message)
{
    write(QStringLiteral("info"), message);
    qInfo().noquote() << QStringLiteral("[%1] %2").arg(m_name, message);
}

void Logger::warning(const QString &message)
{
    write(QStringLiteral("warning"), message);
    qWarning().noquote() << QStringLiteral("[%1] %2").arg(m_name, message);
}

void Logger::error(const QString &message)
{
    write(QStringLiteral("error"), message);
    qCritical().noquote() << QStringLiteral("[%1] %2").arg(m_name, message);
}

void Logger::write(const QString &level, const QString &message)
{
    if (!m_bus) {
        return;
    }
    Event event;
    event.topic = QStringLiteral("log/message");
    event.payload = QVariantMap{{QStringLiteral("level"), level},
                                {QStringLiteral("text"), message},
                                {QStringLiteral("source"), m_name},
                                {QStringLiteral("time"),
                                 QDateTime::currentDateTime().toString(
                                     QStringLiteral("HH:mm:ss"))}};
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();
    event.source = m_name;
    m_bus->publish(event);
}

void NullNotificationService::info(const QString &title, const QString &message)
{
    qInfo().noquote() << QStringLiteral("[通知] %1 - %2").arg(title, message);
}

void NullNotificationService::warning(const QString &title, const QString &message)
{
    qWarning().noquote() << QStringLiteral("[警告] %1 - %2").arg(title, message);
}

void NullNotificationService::error(const QString &title, const QString &message)
{
    qCritical().noquote() << QStringLiteral("[错误] %1 - %2").arg(title, message);
}

} // namespace xcom
