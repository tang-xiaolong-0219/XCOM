#pragma once

#include "xcom/api/types.h"

namespace xcom {

class ITransportEvents
{
public:
    virtual ~ITransportEvents() = default;
    virtual void onTransportData(const QByteArray &bytes) = 0;
    virtual void onTransportError(const QString &message) = 0;
};

class ISerialTransport
{
public:
    virtual ~ISerialTransport() = default;

    virtual bool open(const PortConfig &config, ITransportEvents *events, Error *error) = 0;
    virtual void close() = 0;
    virtual bool write(const QByteArray &bytes, Error *error) = 0;
    virtual bool isOpen() const = 0;
    virtual bool setDtr(bool enabled)
    {
        Q_UNUSED(enabled)
        return false;
    }
    virtual bool setRts(bool enabled)
    {
        Q_UNUSED(enabled)
        return false;
    }
};

// 传输工厂：serial → QSerialPort；loopback → 回环虚拟端口（用于无硬件测试/演示）。
ISerialTransport *createTransport(const QString &kind);

} // namespace xcom
