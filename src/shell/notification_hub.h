#pragma once

#include "xcom/api/services.h"

#include <QObject>

namespace xcom {

class NotificationHub : public QObject, public INotificationService
{
    Q_OBJECT

public:
    explicit NotificationHub(QObject *parent = nullptr);

    void info(const QString &title, const QString &message) override;
    void warning(const QString &title, const QString &message) override;
    void error(const QString &title, const QString &message) override;

signals:
    void notified(int level, const QString &title, const QString &message);

private:
    void emitNotification(int level, const QString &title, const QString &message);
};

} // namespace xcom
