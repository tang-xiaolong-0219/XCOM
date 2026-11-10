#include "serial/session.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>

#include <chrono>

namespace xcom {
namespace {

constexpr int kMaxHistoryChunks = 4000;
constexpr qint64 kMaxHistoryBytes = 8LL * 1024 * 1024;

qint64 monotonicNs()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

class SinkSubscription : public QObject, public ISubscription
{
public:
    SinkSubscription(SerialSession *session, ISessionDataSink *sink)
        : m_session(session)
        , m_sink(sink)
    {
    }

    void unsubscribe() override
    {
        if (m_done) {
            return;
        }
        m_done = true;
        if (m_session) {
            m_session->removeSink(m_sink);
        }
        deleteLater();
    }

private:
    QPointer<SerialSession> m_session;
    ISessionDataSink *m_sink = nullptr;
    bool m_done = false;
};

} // namespace

// ---------------------------------------------------------------------------
// SessionWorker（I/O 线程）
// ---------------------------------------------------------------------------
SessionWorker::SessionWorker(SessionDescriptor descriptor, QObject *parent)
    : QObject(parent)
    , m_descriptor(std::move(descriptor))
{
}

SessionWorker::~SessionWorker()
{
    if (m_transport) {
        m_transport->close();
        delete m_transport;
        m_transport = nullptr;
    }
}

void SessionWorker::start()
{
    if (m_transport) {
        return;
    }
    m_transport = createTransport(m_descriptor.transport);
    Error error;
    if (!m_transport->open(m_descriptor.config, this, &error)) {
        delete m_transport;
        m_transport = nullptr;
        emit openFailed(error.message, int(error.code));
        return;
    }
    emit opened();
}

void SessionWorker::stop()
{
    if (m_transport) {
        m_transport->close();
        delete m_transport;
        m_transport = nullptr;
    }
}

void SessionWorker::reconfigure(const SessionDescriptor &descriptor)
{
    stop();
    m_descriptor = descriptor;
    m_rxSequence = 0;
    start();
}

void SessionWorker::sendBytes(const QByteArray &bytes)
{
    if (!m_transport || !m_transport->isOpen()) {
        emit transportError(QStringLiteral("会话未打开，无法发送"));
        return;
    }
    Error error;
    if (!m_transport->write(bytes, &error)) {
        emit transportError(error.message);
    }
}

void SessionWorker::setDtr(bool enabled)
{
    if (m_transport && m_transport->isOpen()) {
        m_transport->setDtr(enabled);
    }
}

void SessionWorker::setRts(bool enabled)
{
    if (m_transport && m_transport->isOpen()) {
        m_transport->setRts(enabled);
    }
}

void SessionWorker::onTransportData(const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return;
    }
    ++m_rxSequence;
    emit chunk(bytes, m_rxSequence, monotonicNs(), QDateTime::currentMSecsSinceEpoch());
}

void SessionWorker::onTransportError(const QString &message)
{
    emit transportError(message);
}

// ---------------------------------------------------------------------------
// SerialSession（主线程门面）
// ---------------------------------------------------------------------------
SerialSession::SerialSession(const SessionDescriptor &descriptor, const QString &id,
                             QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_descriptor(descriptor)
    , m_thread(new QThread(this))
    , m_worker(new SessionWorker(descriptor))
{
    m_worker->moveToThread(m_thread);

    connect(m_worker, &SessionWorker::opened, this, &SerialSession::handleOpened);
    connect(m_worker, &SessionWorker::openFailed, this, &SerialSession::handleOpenFailed);
    connect(m_worker, &SessionWorker::chunk, this, &SerialSession::handleChunk);
    connect(m_worker, &SessionWorker::transportError, this,
            &SerialSession::handleTransportError);
}

SerialSession::~SerialSession()
{
    shutdown();
    delete m_worker;
    m_worker = nullptr;
}

void SerialSession::start()
{
    if (m_state != SessionState::Created) {
        return;
    }
    setState(SessionState::Opening);
    m_thread->start();
    SessionWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker] { worker->start(); }, Qt::QueuedConnection);
}

void SerialSession::shutdown()
{
    if (m_thread->isRunning()) {
        SessionWorker *worker = m_worker;
        QMetaObject::invokeMethod(
            worker, [worker] { worker->stop(); }, Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait(3000);
    }
    if (m_state != SessionState::Closed) {
        setState(SessionState::Closed);
    }
}

void SerialSession::setDtr(bool enabled)
{
    m_descriptor.config.dtr = enabled;
    SessionWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, enabled] { worker->setDtr(enabled); }, Qt::QueuedConnection);
}

void SerialSession::setRts(bool enabled)
{
    m_descriptor.config.rts = enabled;
    SessionWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, enabled] { worker->setRts(enabled); }, Qt::QueuedConnection);
}

void SerialSession::reconfigure(const SessionDescriptor &descriptor)
{
    if (m_state == SessionState::Closed) return;
    m_descriptor = descriptor;
    setState(SessionState::Opening);
    SessionWorker *worker = m_worker;
    const SessionDescriptor updated = descriptor;
    QMetaObject::invokeMethod(worker, [worker, updated] { worker->reconfigure(updated); },
                              Qt::QueuedConnection);
}

void SerialSession::handleOpened()
{
    setState(SessionState::Open);
    emit openedSignal();
}

void SerialSession::handleOpenFailed(const QString &message, int errorCode)
{
    Error error = Error::make(static_cast<ErrorCode>(errorCode), message,
                              QStringLiteral("serial.session"));
    setState(SessionState::Failed, error);
    emit failedSignal(message);
}

void SerialSession::handleTransportError(const QString &message)
{
    if (m_state == SessionState::Failed || m_state == SessionState::Closed) {
        return;
    }
    Error error = Error::make(ErrorCode::DeviceRemoved, message,
                              QStringLiteral("serial.session"));
    setState(SessionState::Failed, error);
    emit failedSignal(message);
}

void SerialSession::handleChunk(const QByteArray &bytes, quint64 sequence, qint64 monotonic,
                                qint64 wallClockMs)
{
    DataChunk chunk;
    chunk.sessionId = m_id;
    chunk.direction = Direction::Rx;
    chunk.sequence = sequence;
    chunk.monotonicNs = monotonic;
    chunk.wallClockMs = wallClockMs;
    chunk.bytes = bytes;

    m_stats.rxBytes += static_cast<quint64>(bytes.size());
    ++m_stats.rxChunks;

    appendHistory(chunk);
    notifyData(chunk);
}

bool SerialSession::send(const QByteArray &bytes, Error *error)
{
    if (m_state != SessionState::Open) {
        if (error) {
            *error = Error::make(ErrorCode::NotOpen, QStringLiteral("会话未打开"),
                                 QStringLiteral("serial.session"));
        }
        return false;
    }
    if (bytes.isEmpty()) {
        return true;
    }

    DataChunk chunk;
    chunk.sessionId = m_id;
    chunk.direction = Direction::Tx;
    chunk.sequence = ++m_txSequence;
    chunk.monotonicNs = monotonicNs();
    chunk.wallClockMs = QDateTime::currentMSecsSinceEpoch();
    chunk.bytes = bytes;

    m_stats.txBytes += static_cast<quint64>(bytes.size());
    ++m_stats.txChunks;
    appendHistory(chunk);
    notifyData(chunk);

    SessionWorker *worker = m_worker;
    const QByteArray payload = bytes;
    QMetaObject::invokeMethod(
        worker, [worker, payload] { worker->sendBytes(payload); }, Qt::QueuedConnection);
    return true;
}

ISubscription *SerialSession::subscribe(ISessionDataSink *sink)
{
    if (sink) {
        m_sinks.append(sink);
    }
    return new SinkSubscription(this, sink);
}

void SerialSession::removeSink(ISessionDataSink *sink)
{
    m_sinks.removeAll(sink);
}

QVector<DataChunk> SerialSession::history(quint64 fromSequence, int maxChunks) const
{
    QVector<DataChunk> result;
    for (const DataChunk &chunk : m_history) {
        if (chunk.sequence >= fromSequence) {
            result.append(chunk);
        }
    }
    if (result.size() > maxChunks) {
        result = result.mid(result.size() - maxChunks);
    }
    return result;
}

void SerialSession::setState(SessionState state, const Error &error)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    notifyState();
    Q_UNUSED(error)
    emit stateChangedSignal(state);
}

void SerialSession::notifyState()
{
    const QVector<ISessionDataSink *> sinks = m_sinks;
    for (ISessionDataSink *sink : sinks) {
        if (sink) {
            sink->onStateChanged(m_state, Error());
        }
    }
}

void SerialSession::notifyData(const DataChunk &chunk)
{
    const QVector<ISessionDataSink *> sinks = m_sinks;
    for (ISessionDataSink *sink : sinks) {
        if (sink) {
            sink->onData(chunk);
        }
    }
}

void SerialSession::appendHistory(const DataChunk &chunk)
{
    m_history.append(chunk);
    m_historyBytes += chunk.bytes.size();
    while (m_history.size() > kMaxHistoryChunks && !m_history.isEmpty()) {
        m_historyBytes -= m_history.first().bytes.size();
        m_history.removeFirst();
    }
    while (m_historyBytes > kMaxHistoryBytes && m_history.size() > 1) {
        m_historyBytes -= m_history.first().bytes.size();
        m_history.removeFirst();
    }
}

} // namespace xcom
