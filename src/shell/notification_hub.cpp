#include "shell/notification_hub.h"

namespace xcom {

NotificationHub::NotificationHub(QObject *parent)
    : QObject(parent)
{
}

void NotificationHub::info(const QString &title, const QString &message)
{
    emitNotification(0, title, message);
}

void NotificationHub::warning(const QString &title, const QString &message)
{
    emitNotification(1, title, message);
}

void NotificationHub::error(const QString &title, const QString &message)
{
    emitNotification(2, title, message);
}

void NotificationHub::emitNotification(int level, const QString &title, const QString &message)
{
    emit notified(level, title, message);
}

} // namespace xcom
