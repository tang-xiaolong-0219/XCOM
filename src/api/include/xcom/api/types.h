#pragma once

#include "xcom/api/version.h"

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace xcom {

enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    InvalidState,
    NotSupported,
    PermissionDenied,
    Timeout,
    Canceled,
    Internal,
    PortNotFound,
    PortBusy,
    AccessDenied,
    DeviceRemoved,
    OpenTimeout,
    WriteTimeout,
    NotOpen,
    QueueFull,
    TooManySessions,
    StorageError,
    PluginFailed
};

struct Error {
    ErrorCode code = ErrorCode::Ok;
    QString message;
    QString source;
    QVariantMap details;

    bool isOk() const { return code == ErrorCode::Ok; }

    static Error ok() { return Error{}; }

    static Error make(ErrorCode code, const QString &message, const QString &source = QString())
    {
        Error error;
        error.code = code;
        error.message = message;
        error.source = source;
        return error;
    }
};

// 注册类 API 返回的撤销令牌。宿主拥有对象所有权，调用方只可调用 unsubscribe()。
class ISubscription
{
public:
    virtual ~ISubscription() = default;
    virtual void unsubscribe() = 0;
};

enum class SessionState { Created, Opening, Open, Closing, Closed, Failed };

enum class Direction { Rx, Tx };

struct PortConfig {
    QString portName;
    int baudRate = 115200;
    int dataBits = 8;
    int stopBits = 1;   // 1 / 2（1.5 暂不支持，按 2 处理）
    int parity = 0;     // 0 none, 1 even, 2 odd, 3 mark, 4 space
    int flow = 0;       // 0 none, 1 hardware, 2 software
    bool dtr = false;
    bool rts = false;

    bool operator==(const PortConfig &other) const
    {
        return portName == other.portName && baudRate == other.baudRate
               && dataBits == other.dataBits && stopBits == other.stopBits
               && parity == other.parity && flow == other.flow && dtr == other.dtr
               && rts == other.rts;
    }
};

struct SessionDescriptor {
    QString name;
    QString transport = QStringLiteral("serial"); // serial / loopback
    PortConfig config;
    QString encoding = QStringLiteral("system");  // system / utf8 / latin1
};

struct SessionInfo {
    QString id;
    QString name;
    QString transport;
    SessionState state = SessionState::Created;
    PortConfig config;
    quint64 rxBytes = 0;
    quint64 txBytes = 0;
};

struct SessionStats {
    quint64 rxBytes = 0;
    quint64 txBytes = 0;
    quint64 rxChunks = 0;
    quint64 txChunks = 0;
    quint64 droppedBytes = 0;
    int queueDepth = 0;
};

struct DataChunk {
    QString sessionId;
    Direction direction = Direction::Rx;
    quint64 sequence = 0;
    qint64 monotonicNs = 0;
    qint64 wallClockMs = 0;
    QByteArray bytes;
    bool replay = false;
    bool gap = false;
};

struct PluginInfo {
    QString id;
    QString name;
    QString version;
    QString apiVersion;
    QString entry;
    QString description;
    QString path;
    QString state = QStringLiteral("Discovered");
    QString error;
    bool enabled = true;
    int contributions = 0;
};

XCOM_API_EXPORT QString errorCodeName(ErrorCode code);
XCOM_API_EXPORT QString sessionStateName(SessionState state);
XCOM_API_EXPORT QString sessionStateIcon(SessionState state);

} // namespace xcom
