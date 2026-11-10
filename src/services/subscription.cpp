#include "services/subscription.h"

#include <utility>

namespace xcom {

CallbackSubscription::CallbackSubscription(std::function<void()> callback, QObject *parent)
    : QObject(parent)
    , m_callback(std::move(callback))
{
}

void CallbackSubscription::unsubscribe()
{
    if (m_done) {
        return;
    }
    m_done = true;
    if (m_callback) {
        m_callback();
        m_callback = nullptr;
    }
    deleteLater();
}

} // namespace xcom
