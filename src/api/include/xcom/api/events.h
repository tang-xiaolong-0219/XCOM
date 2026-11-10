#pragma once

#include "xcom/api/types.h"

#include <QVariantMap>

namespace xcom {

struct Event {
    QString topic;
    QVariantMap payload;
    qint64 timestampMs = 0;
    QString source;
};

class IEventHandler
{
public:
    virtual ~IEventHandler() = default;
    virtual void onEvent(const Event &event) = 0;
};

class IEventBus
{
public:
    virtual ~IEventBus() = default;

    virtual ISubscription *subscribe(const QString &topic, IEventHandler *handler) = 0;
    virtual void publish(const Event &event) = 0;
};

} // namespace xcom
