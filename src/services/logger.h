#pragma once

#include "xcom/api/services.h"

#include <QString>

namespace xcom {

class IEventBus;

class Logger : public ILogger
{
public:
    explicit Logger(QString name, IEventBus *bus = nullptr);

    void debug(const QString &message) override;
    void info(const QString &message) override;
    void warning(const QString &message) override;
    void error(const QString &message) override;

private:
    void write(const QString &level, const QString &message);
    QString m_name;
    IEventBus *m_bus = nullptr;
};

class NullNotificationService : public INotificationService
{
public:
    void info(const QString &title, const QString &message) override;
    void warning(const QString &title, const QString &message) override;
    void error(const QString &title, const QString &message) override;
};

} // namespace xcom
