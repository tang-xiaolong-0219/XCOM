#pragma once

#include "xcom/api/types.h"

namespace xcom {

class ISessionDataSink
{
public:
    virtual ~ISessionDataSink() = default;
    virtual void onData(const DataChunk &chunk) = 0;
    virtual void onStateChanged(SessionState state, const Error &reason)
    {
        Q_UNUSED(state)
        Q_UNUSED(reason)
    }
    virtual void onError(const Error &error) { Q_UNUSED(error) }
};

class ISession
{
public:
    virtual ~ISession() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString transport() const = 0;
    virtual QString encoding() const = 0;
    virtual SessionState state() const = 0;
    virtual PortConfig config() const = 0;
    virtual SessionStats stats() const = 0;

    virtual bool send(const QByteArray &bytes, Error *error = nullptr) = 0;
    virtual ISubscription *subscribe(ISessionDataSink *sink) = 0;
    virtual QVector<DataChunk> history(quint64 fromSequence = 0, int maxChunks = 500) const = 0;
};

class ISessionService
{
public:
    virtual ~ISessionService() = default;

    virtual bool open(const SessionDescriptor &descriptor, QString *sessionId,
                      Error *error = nullptr) = 0;
    virtual bool close(const QString &sessionId, Error *error = nullptr) = 0;
    virtual void closeAll() = 0;
    virtual QVector<SessionInfo> list() const = 0;
    virtual ISession *find(const QString &ref) const = 0;
    virtual QString activeId() const = 0;
    virtual void setActiveId(const QString &sessionId) = 0;
    virtual bool setDtr(const QString &sessionId, bool enabled, Error *error = nullptr) = 0;
    virtual bool setRts(const QString &sessionId, bool enabled, Error *error = nullptr) = 0;
};

} // namespace xcom
