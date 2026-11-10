#include "services/event_bus.h"

#include "services/subscription.h"

#include <QDebug>

#include <exception>

namespace xcom {

EventBus::EventBus(QObject *parent)
    : QObject(parent)
{
}

ISubscription *EventBus::subscribe(const QString &topic, IEventHandler *handler)
{
    if (!handler) {
        return nullptr;
    }
    Entry entry;
    entry.topic = topic;
    entry.handler = handler;
    entry.subscription = new CallbackSubscription(
        [this, topic, handler] { remove(topic, handler); }, this);
    m_entries.append(entry);
    return entry.subscription;
}

void EventBus::publish(const Event &event)
{
    QVector<IEventHandler *> handlers;
    handlers.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        if (entry.topic == event.topic) {
            handlers.append(entry.handler);
        }
    }

    for (IEventHandler *handler : handlers) {
        try {
            handler->onEvent(event);
        } catch (const std::exception &exception) {
            qWarning() << "event handler exception on" << event.topic << ":"
                       << exception.what();
        } catch (...) {
            qWarning() << "event handler unknown exception on" << event.topic;
        }
    }
}

void EventBus::remove(const QString &topic, IEventHandler *handler)
{
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (m_entries.at(i).topic == topic && m_entries.at(i).handler == handler) {
            m_entries.remove(i);
        }
    }
}

} // namespace xcom
