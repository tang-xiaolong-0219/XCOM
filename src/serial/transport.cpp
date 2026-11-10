#include "serial/transport.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>

namespace xcom {
namespace {

QSerialPort::DataBits toDataBits(int value)
{
    switch (value) {
    case 5:
        return QSerialPort::Data5;
    case 6:
        return QSerialPort::Data6;
    case 7:
        return QSerialPort::Data7;
    default:
        return QSerialPort::Data8;
    }
}

QSerialPort::StopBits toStopBits(int value)
{
    return value == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop;
}

QSerialPort::Parity toParity(int value)
{
    switch (value) {
    case 1:
        return QSerialPort::EvenParity;
    case 2:
        return QSerialPort::OddParity;
    case 3:
        return QSerialPort::MarkParity;
    case 4:
        return QSerialPort::SpaceParity;
    default:
        return QSerialPort::NoParity;
    }
}

QSerialPort::FlowControl toFlow(int value)
{
    switch (value) {
    case 1:
        return QSerialPort::HardwareControl;
    case 2:
        return QSerialPort::SoftwareControl;
    default:
        return QSerialPort::NoFlowControl;
    }
}

class QSerialPortTransport : public QObject, public ISerialTransport
{
    Q_OBJECT

public:
    explicit QSerialPortTransport(QObject *parent = nullptr)
        : QObject(parent)
        , m_port(new QSerialPort(this))
    {
        connect(m_port, &QSerialPort::readyRead, this, &QSerialPortTransport::handleReadyRead);
        connect(m_port, &QSerialPort::errorOccurred, this,
                &QSerialPortTransport::handleErrorOccurred);
    }

    bool open(const PortConfig &config, ITransportEvents *events, Error *error) override
    {
        m_events = events;
        m_port->setPortName(config.portName);
        m_port->setBaudRate(config.baudRate);
        m_port->setDataBits(toDataBits(config.dataBits));
        m_port->setStopBits(toStopBits(config.stopBits));
        m_port->setParity(toParity(config.parity));
        m_port->setFlowControl(toFlow(config.flow));

        if (!m_port->open(QIODevice::ReadWrite)) {
            const QSerialPort::SerialPortError portError = m_port->error();
            ErrorCode code = ErrorCode::PortNotFound;
            if (portError == QSerialPort::PermissionError) {
                code = ErrorCode::PortBusy;
            } else if (portError == QSerialPort::DeviceNotFoundError) {
                code = ErrorCode::PortNotFound;
            } else if (portError == QSerialPort::OpenError) {
                code = ErrorCode::OpenTimeout;
            }
            if (error) {
                *error = Error::make(code, m_port->errorString(), QStringLiteral("serial.transport"));
            }
            return false;
        }

        m_port->setDataTerminalReady(config.dtr);
        m_port->setRequestToSend(config.rts);
        return true;
    }

    void close() override
    {
        if (m_port->isOpen()) {
            m_port->close();
        }
        m_events = nullptr;
    }

    bool write(const QByteArray &bytes, Error *error) override
    {
        if (!m_port->isOpen()) {
            if (error) {
                *error = Error::make(ErrorCode::NotOpen, QStringLiteral("串口未打开"));
            }
            return false;
        }
        const qint64 written = m_port->write(bytes);
        if (written < 0) {
            if (error) {
                *error = Error::make(ErrorCode::WriteTimeout, m_port->errorString());
            }
            return false;
        }
        return true;
    }

    bool isOpen() const override { return m_port->isOpen(); }

    bool setDtr(bool enabled) override
    {
        return m_port->isOpen() ? m_port->setDataTerminalReady(enabled) : false;
    }

    bool setRts(bool enabled) override
    {
        return m_port->isOpen() ? m_port->setRequestToSend(enabled) : false;
    }

private slots:
    void handleReadyRead()
    {
        const QByteArray data = m_port->readAll();
        if (!data.isEmpty() && m_events) {
            m_events->onTransportData(data);
        }
    }

    void handleErrorOccurred(QSerialPort::SerialPortError error)
    {
        if (error == QSerialPort::NoError || !m_events) {
            return;
        }
        if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError
            || error == QSerialPort::PermissionError) {
            m_events->onTransportError(m_port->errorString());
        }
    }

private:
    QSerialPort *m_port = nullptr;
    ITransportEvents *m_events = nullptr;
};

// 回环传输：写入的数据延迟数毫秒后原样回送，用于无串口硬件时演示与测试。
class LoopbackTransport : public QObject, public ISerialTransport
{
    Q_OBJECT

public:
    explicit LoopbackTransport(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    bool open(const PortConfig &config, ITransportEvents *events, Error *error) override
    {
        Q_UNUSED(config)
        Q_UNUSED(error)
        m_events = events;
        m_open = true;
        return true;
    }

    void close() override
    {
        m_open = false;
        m_events = nullptr;
    }

    bool write(const QByteArray &bytes, Error *error) override
    {
        if (!m_open) {
            if (error) {
                *error = Error::make(ErrorCode::NotOpen, QStringLiteral("回环端口未打开"));
            }
            return false;
        }
        const QByteArray echo = bytes;
        QTimer::singleShot(20, this, [this, echo] {
            if (m_open && m_events) {
                m_events->onTransportData(echo);
            }
        });
        return true;
    }

    bool isOpen() const override { return m_open; }

private:
    ITransportEvents *m_events = nullptr;
    bool m_open = false;
};

} // namespace

ISerialTransport *createTransport(const QString &kind)
{
    if (kind.compare(QStringLiteral("loopback"), Qt::CaseInsensitive) == 0) {
        return new LoopbackTransport();
    }
    return new QSerialPortTransport();
}

} // namespace xcom

#include "transport.moc"
