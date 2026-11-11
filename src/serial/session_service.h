#pragma once

#include "xcom/api/events.h"
#include "xcom/api/sessions.h"

#include <QObject>
#include <QVariantMap>
#include <QVector>

namespace xcom {

class SerialSession;

class SessionService : public QObject, public ISessionService
{
    Q_OBJECT

public:
    explicit SessionService(IEventBus *bus, QObject *parent = nullptr);
    ~SessionService() override;

    bool open(const SessionDescriptor &descriptor, QString *sessionId,
              Error *error = nullptr) override;
    bool close(const QString &sessionId, Error *error = nullptr) override;
    void closeAll() override;
    QVector<SessionInfo> list() const override;
    ISession *find(const QString &ref) const override;
    QString activeId() const override { return m_activeId; }
    void setActiveId(const QString &sessionId) override;
    bool setDtr(const QString &sessionId, bool enabled, Error *error = nullptr) override;
    bool setRts(const QString &sessionId, bool enabled, Error *error = nullptr) override;
    bool setConnected(const QString &sessionId, bool connected, Error *error = nullptr);
    bool reconfigure(const QString &sessionId, const SessionDescriptor &descriptor,
                     Error *error = nullptr);

private:
    SerialSession *findSession(const QString &ref) const;
    void publish(const QString &topic, const QVariantMap &payload);
    QVariantMap infoPayload(const SerialSession *session) const;
    void publishState(const SerialSession *session);
    void updateActiveAfterRemoval(const QString &removedId);

    IEventBus *m_bus = nullptr;
    QVector<SerialSession *> m_sessions;
    QString m_activeId;
};

} // namespace xcom
