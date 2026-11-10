#pragma once

#include "xcom/api/events.h"

#include <QObject>
#include <QVector>

namespace xcom {

class CallbackSubscription;

class EventBus : public QObject, public IEventBus
{
    Q_OBJECT

public:
    explicit EventBus(QObject *parent = nullptr);

    ISubscription *subscribe(const QString &topic, IEventHandler *handler) override;
    void publish(const Event &event) override;

private:
    struct Entry {
        QString topic;
        IEventHandler *handler = nullptr;
        CallbackSubscription *subscription = nullptr;
    };

    void remove(const QString &topic, IEventHandler *handler);
    QVector<Entry> m_entries;
};

} // namespace xcom
