#pragma once

#include "xcom/api/types.h"

namespace xcom {

class ILogger
{
public:
    virtual ~ILogger() = default;

    virtual void debug(const QString &message) = 0;
    virtual void info(const QString &message) = 0;
    virtual void warning(const QString &message) = 0;
    virtual void error(const QString &message) = 0;
};

class INotificationService
{
public:
    virtual ~INotificationService() = default;

    virtual void info(const QString &title, const QString &message) = 0;
    virtual void warning(const QString &title, const QString &message) = 0;
    virtual void error(const QString &title, const QString &message) = 0;
};

} // namespace xcom
