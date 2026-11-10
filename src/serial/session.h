#pragma once

#include "serial/transport.h"
#include "xcom/api/sessions.h"

#include <QObject>
#include <QThread>
#include <QVector>

class QTimer;

namespace xcom {

class SerialSession;

class SessionWorker : public QObject, public ITransportEvents
{
    Q_OBJECT

public:
    explicit SessionWorker(SessionDescriptor descriptor, QObject *parent = nullptr);
    ~SessionWorker() override;

public slots:
    void start();
    void stop();
    void sendBytes(const QByteArray &bytes);
    void setDtr(bool enabled);
    void setRts(bool enabled);
    void reconfigure(const SessionDescriptor &descriptor);

signals:
    void opened();
    void openFailed(const QString &message, int errorCode);
    void chunk(const QByteArray &bytes, quint64 sequence, qint64 monotonicNs, qint64 wallClockMs);
    void transportError(const QString &message);

public:
    // ITransportEvents（I/O 线程回调）
    void onTransportData(const QByteArray &bytes) override;
    void onTransportError(const QString &message) override;

private:
    SessionDescriptor m_descriptor;
    ISerialTransport *m_transport = nullptr;
    quint64 m_rxSequence = 0;
};

class SerialSession : public QObject, public ISession
{
    Q_OBJECT

public:
    SerialSession(const SessionDescriptor &descriptor, const QString &id,
                  QObject *parent = nullptr);
    ~SerialSession() override;

    // ISession
    QString id() const override { return m_id; }
    QString name() const override { return m_descriptor.name; }
    QString transport() const override { return m_descriptor.transport; }
    QString encoding() const override { return m_descriptor.encoding; }
    SessionState state() const override { return m_state; }
    PortConfig config() const override { return m_descriptor.config; }
    SessionStats stats() const override { return m_stats; }
    bool send(const QByteArray &bytes, Error *error = nullptr) override;
    ISubscription *subscribe(ISessionDataSink *sink) override;
    QVector<DataChunk> history(quint64 fromSequence = 0, int maxChunks = 500) const override;

    SessionDescriptor descriptor() const { return m_descriptor; }
    void removeSink(ISessionDataSink *sink);

    void start();
    void shutdown();
    void setDtr(bool enabled);
    void setRts(bool enabled);
    void reconfigure(const SessionDescriptor &descriptor);

signals:
    void openedSignal();
    void failedSignal(const QString &message);
    void stateChangedSignal(xcom::SessionState state);

private slots:
    void handleOpened();
    void handleOpenFailed(const QString &message, int errorCode);
    void handleTransportError(const QString &message);
    void handleChunk(const QByteArray &bytes, quint64 sequence, qint64 monotonicNs,
                     qint64 wallClockMs);

private:
    void setState(SessionState state, const Error &error = Error());
    void appendHistory(const DataChunk &chunk);
    void notifyState();
    void notifyData(const DataChunk &chunk);

    QString m_id;
    SessionDescriptor m_descriptor;
    SessionState m_state = SessionState::Created;
    SessionStats m_stats;
    quint64 m_txSequence = 0;

    QThread *m_thread = nullptr;
    SessionWorker *m_worker = nullptr;

    QVector<ISessionDataSink *> m_sinks;
    QVector<DataChunk> m_history;
    qint64 m_historyBytes = 0;
};

} // namespace xcom
