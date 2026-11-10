#include "xcom/api/version.h"

#include "xcom/api/types.h"

namespace xcom {

QString apiVersion()
{
    return QStringLiteral(XCOM_API_VERSION_STRING);
}

QString apiHash()
{
    // 由公共头文件 + 版本决定；当前实现固定为 API 版本，后续接入构建期哈希。
    return QStringLiteral("xcom-api-") + apiVersion();
}

QString errorCodeName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::Ok:
        return QStringLiteral("Ok");
    case ErrorCode::InvalidArgument:
        return QStringLiteral("InvalidArgument");
    case ErrorCode::NotFound:
        return QStringLiteral("NotFound");
    case ErrorCode::AlreadyExists:
        return QStringLiteral("AlreadyExists");
    case ErrorCode::InvalidState:
        return QStringLiteral("InvalidState");
    case ErrorCode::NotSupported:
        return QStringLiteral("NotSupported");
    case ErrorCode::PermissionDenied:
        return QStringLiteral("PermissionDenied");
    case ErrorCode::Timeout:
        return QStringLiteral("Timeout");
    case ErrorCode::Canceled:
        return QStringLiteral("Canceled");
    case ErrorCode::Internal:
        return QStringLiteral("Internal");
    case ErrorCode::PortNotFound:
        return QStringLiteral("PortNotFound");
    case ErrorCode::PortBusy:
        return QStringLiteral("PortBusy");
    case ErrorCode::AccessDenied:
        return QStringLiteral("AccessDenied");
    case ErrorCode::DeviceRemoved:
        return QStringLiteral("DeviceRemoved");
    case ErrorCode::OpenTimeout:
        return QStringLiteral("OpenTimeout");
    case ErrorCode::WriteTimeout:
        return QStringLiteral("WriteTimeout");
    case ErrorCode::NotOpen:
        return QStringLiteral("NotOpen");
    case ErrorCode::QueueFull:
        return QStringLiteral("QueueFull");
    case ErrorCode::TooManySessions:
        return QStringLiteral("TooManySessions");
    case ErrorCode::StorageError:
        return QStringLiteral("StorageError");
    case ErrorCode::PluginFailed:
        return QStringLiteral("PluginFailed");
    }
    return QStringLiteral("Unknown");
}

QString sessionStateName(SessionState state)
{
    switch (state) {
    case SessionState::Created:
        return QStringLiteral("未连接");
    case SessionState::Opening:
        return QStringLiteral("打开中");
    case SessionState::Open:
        return QStringLiteral("已连接");
    case SessionState::Closing:
        return QStringLiteral("关闭中");
    case SessionState::Closed:
        return QStringLiteral("已关闭");
    case SessionState::Failed:
        return QStringLiteral("失败");
    }
    return QStringLiteral("未知");
}

QString sessionStateIcon(SessionState state)
{
    switch (state) {
    case SessionState::Open:
        return QStringLiteral("●");
    case SessionState::Opening:
    case SessionState::Closing:
        return QStringLiteral("◐");
    case SessionState::Failed:
        return QStringLiteral("✕");
    default:
        return QStringLiteral("○");
    }
}

} // namespace xcom
