#include "serial/session_service.h"

#include "serial/session.h"

#include <QDateTime>
#include <QUuid>

namespace xcom {
namespace {
constexpr int kMaxSessions = 32;
}

SessionService::SessionService(IEventBus *bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
}

SessionService::~SessionService()
{
    for (SerialSession *session : m_sessions) {
        session->shutdown();
        delete session;
    }
    m_sessions.clear();
}

bool SessionService::open(const SessionDescriptor &descriptor, QString *sessionId, Error *error)
{
    if (m_sessions.size() >= kMaxSessions) {
        if (error) {
            *error = Error::make(ErrorCode::TooManySessions,
                                 QStringLiteral("会话数量已达上限（%1）").arg(kMaxSessions));
        }
        return false;
    }

    QString name = descriptor.name.trimmed();
    if (name.isEmpty()) {
        name = descriptor.config.portName.isEmpty() ? descriptor.transport
                                                    : descriptor.config.portName;
    }

    for (const SerialSession *session : m_sessions) {
        if (session->name().compare(name, Qt::CaseInsensitive) == 0) {
            if (error) {
                *error = Error::make(ErrorCode::AlreadyExists,
                                     QStringLiteral("会话名“%1”已存在").arg(name));
            }
            return false;
        }
        const bool bothSerial = session->state() != SessionState::Closed
                                && session->transport() != QStringLiteral("loopback")
                                && descriptor.transport != QStringLiteral("loopback");
        if (bothSerial && !descriptor.config.portName.isEmpty()
            && session->config().portName.compare(descriptor.config.portName,
                                                  Qt::CaseInsensitive)
                   == 0) {
            if (error) {
                *error = Error::make(
                    ErrorCode::PortBusy,
                    QStringLiteral("端口 %1 已被会话“%2”占用")
                        .arg(descriptor.config.portName, session->name()));
            }
            return false;
        }
    }

    SessionDescriptor effective = descriptor;
    effective.name = name;

    const QString id = QStringLiteral("sess-")
                       + QUuid::createUuid().toString(QUuid::Id128).left(6);
    auto *session = new SerialSession(effective, id);

    connect(session, &SerialSession::openedSignal, this, [this, session] {
        publish(QStringLiteral("session/opened"), infoPayload(session));
        publishState(session);
    });
    connect(session, &SerialSession::failedSignal, this,
            [this, session](const QString &message) {
                QVariantMap payload = infoPayload(session);
                payload.insert(QStringLiteral("message"), message);
                publish(QStringLiteral("session/error"), payload);
                publishState(session);
            });
    connect(session, &SerialSession::stateChangedSignal, this,
            [this, session](SessionState) { publishState(session); });

    m_sessions.append(session);

    QVariantMap added = infoPayload(session);
    added.insert(QStringLiteral("state"), int(SessionState::Opening));
    added.insert(QStringLiteral("stateText"), sessionStateName(SessionState::Opening));
    publish(QStringLiteral("session/added"), added);

    if (m_activeId.isEmpty()) {
        m_activeId = id;
        publish(QStringLiteral("session/activeChanged"),
                QVariantMap{{QStringLiteral("id"), id}});
    }

    if (sessionId) {
        *sessionId = id;
    }
    session->start();
    return true;
}

bool SessionService::close(const QString &sessionId, Error *error)
{
    SerialSession *session = findSession(sessionId);
    if (!session) {
        if (error) {
            *error = Error::make(ErrorCode::NotFound,
                                 QStringLiteral("会话不存在：%1").arg(sessionId));
        }
        return false;
    }
    const QString id = session->id();
    const QString name = session->name();

    session->shutdown();
    publish(QStringLiteral("session/closed"),
            QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}});

    m_sessions.removeOne(session);
    updateActiveAfterRemoval(id);
    delete session;
    return true;
}

void SessionService::closeAll()
{
    const QVector<SerialSession *> sessions = m_sessions;
    for (SerialSession *session : sessions) {
        close(session->id());
    }
}

QVector<SessionInfo> SessionService::list() const
{
    QVector<SessionInfo> result;
    result.reserve(m_sessions.size());
    for (const SerialSession *session : m_sessions) {
        SessionInfo info;
        info.id = session->id();
        info.name = session->name();
        info.transport = session->transport();
        info.state = session->state();
        info.config = session->config();
        info.rxBytes = session->stats().rxBytes;
        info.txBytes = session->stats().txBytes;
        result.append(info);
    }
    return result;
}

ISession *SessionService::find(const QString &ref) const
{
    return findSession(ref);
}

void SessionService::setActiveId(const QString &sessionId)
{
    if (m_activeId == sessionId) {
        return;
    }
    if (!findSession(sessionId)) {
        return;
    }
    m_activeId = sessionId;
    publish(QStringLiteral("session/activeChanged"),
            QVariantMap{{QStringLiteral("id"), sessionId}});
}

bool SessionService::setDtr(const QString &sessionId, bool enabled, Error *error)
{
    SerialSession *session = findSession(sessionId);
    if (!session || session->state() != SessionState::Open) {
        if (error) {
            *error = Error::make(ErrorCode::InvalidState, QStringLiteral("会话未打开"));
        }
        return false;
    }
    session->setDtr(enabled);
    return true;
}

bool SessionService::setRts(const QString &sessionId, bool enabled, Error *error)
{
    SerialSession *session = findSession(sessionId);
    if (!session || session->state() != SessionState::Open) {
        if (error) {
            *error = Error::make(ErrorCode::InvalidState, QStringLiteral("会话未打开"));
        }
        return false;
    }
    session->setRts(enabled);
    return true;
}

bool SessionService::setConnected(const QString &sessionId, bool connected, Error *error)
{
    SerialSession *session = findSession(sessionId);
    if (!session) {
        if (error) *error = Error::make(ErrorCode::NotFound, QStringLiteral("会话不存在"));
        return false;
    }
    session->setConnected(connected);
    publishState(session);
    return true;
}

bool SessionService::reconfigure(const QString &sessionId, const SessionDescriptor &descriptor,
                                 Error *error)
{
    SerialSession *session = findSession(sessionId);
    if (!session) {
        if (error) *error = Error::make(ErrorCode::NotFound, QStringLiteral("会话不存在"));
        return false;
    }
    const QString name = descriptor.name.trimmed();
    if (name.isEmpty()) {
        if (error) *error = Error::make(ErrorCode::InvalidState, QStringLiteral("会话名称不能为空"));
        return false;
    }
    if (descriptor.transport != QStringLiteral("loopback")
        && descriptor.config.portName.trimmed().isEmpty()) {
        if (error) *error = Error::make(ErrorCode::InvalidArgument, QStringLiteral("请选择有效串口"));
        return false;
    }
    if (descriptor.config.baudRate <= 0) {
        if (error) *error = Error::make(ErrorCode::InvalidArgument, QStringLiteral("波特率必须大于 0"));
        return false;
    }
    for (const SerialSession *other : m_sessions) {
        if (other == session) continue;
        if (other->name().compare(name, Qt::CaseInsensitive) == 0) {
            if (error) *error = Error::make(ErrorCode::AlreadyExists,
                                            QStringLiteral("会话名“%1”已存在").arg(name));
            return false;
        }
        const bool bothSerial = other->state() != SessionState::Closed
                                && other->transport() != QStringLiteral("loopback")
                                && descriptor.transport != QStringLiteral("loopback");
        if (bothSerial && other->config().portName.compare(descriptor.config.portName,
                                                           Qt::CaseInsensitive) == 0) {
            if (error) *error = Error::make(ErrorCode::PortBusy,
                                            QStringLiteral("端口 %1 已被占用").arg(descriptor.config.portName));
            return false;
        }
    }
    SessionDescriptor updated = descriptor;
    updated.name = name;
    session->reconfigure(updated);
    publishState(session);
    return true;
}

SerialSession *SessionService::findSession(const QString &ref) const
{
    QString key = ref.trimmed();
    if (key.isEmpty() || key.compare(QStringLiteral("active"), Qt::CaseInsensitive) == 0) {
        key = m_activeId;
    }
    for (SerialSession *session : m_sessions) {
        if (session->id() == key) {
            return session;
        }
    }
    for (SerialSession *session : m_sessions) {
        if (session->name().compare(key, Qt::CaseInsensitive) == 0) {
            return session;
        }
    }
    return nullptr;
}

void SessionService::publish(const QString &topic, const QVariantMap &payload)
{
    if (!m_bus) {
        return;
    }
    Event event;
    event.topic = topic;
    event.payload = payload;
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();
    event.source = QStringLiteral("session.service");
    m_bus->publish(event);
}

QVariantMap SessionService::infoPayload(const SerialSession *session) const
{
    const SessionInfo info = [&session] {
        SessionInfo value;
        value.id = session->id();
        value.name = session->name();
        value.transport = session->transport();
        value.state = session->state();
        value.config = session->config();
        value.rxBytes = session->stats().rxBytes;
        value.txBytes = session->stats().txBytes;
        return value;
    }();

    QVariantMap payload;
    payload.insert(QStringLiteral("id"), info.id);
    payload.insert(QStringLiteral("name"), info.name);
    payload.insert(QStringLiteral("transport"), info.transport);
    payload.insert(QStringLiteral("state"), int(info.state));
    payload.insert(QStringLiteral("stateText"), sessionStateName(info.state));
    payload.insert(QStringLiteral("port"), info.config.portName);
    payload.insert(QStringLiteral("baud"), info.config.baudRate);
    payload.insert(QStringLiteral("rxBytes"), QVariant::fromValue(info.rxBytes));
    payload.insert(QStringLiteral("txBytes"), QVariant::fromValue(info.txBytes));
    return payload;
}

void SessionService::publishState(const SerialSession *session)
{
    publish(QStringLiteral("session/stateChanged"), infoPayload(session));
}

void SessionService::updateActiveAfterRemoval(const QString &removedId)
{
    if (m_activeId != removedId) {
        return;
    }
    m_activeId = m_sessions.isEmpty() ? QString() : m_sessions.first()->id();
    publish(QStringLiteral("session/activeChanged"),
            QVariantMap{{QStringLiteral("id"), m_activeId}});
}

} // namespace xcom
