#include "xcom/api/plugin.h"
#include "xcom/api/ui.h"

#include <QAbstractSocket>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextDocument>
#include <QUdpSocket>
#include <QVBoxLayout>
#include <QWidget>
#include <QtPlugin>

#include <memory>

using namespace xcom;

namespace {

bool parseHex(const QString &text, QByteArray *out, QString *error)
{
    QString digits;
    digits.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch.isSpace() || ch == QLatin1Char(',') || ch == QLatin1Char(';')
            || ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char(':')) {
            continue;
        }
        if (ch == QLatin1Char('0') && i + 1 < text.size()
            && (text.at(i + 1) == QLatin1Char('x') || text.at(i + 1) == QLatin1Char('X'))) {
            ++i;
            continue;
        }
        const ushort value = ch.unicode();
        const bool isHexDigit = (value >= ushort('0') && value <= ushort('9'))
                                || (value >= ushort('a') && value <= ushort('f'))
                                || (value >= ushort('A') && value <= ushort('F'));
        if (!isHexDigit) {
            if (error) {
                *error = QStringLiteral("HEX 数据包含非法字符：%1").arg(ch);
            }
            return false;
        }
        digits.append(ch);
    }

    if (digits.size() % 2 != 0) {
        if (error) {
            *error = QStringLiteral("HEX 数据需要按两个字符一组输入");
        }
        return false;
    }

    QByteArray bytes;
    bytes.reserve(digits.size() / 2);
    for (int i = 0; i < digits.size(); i += 2) {
        bool ok = false;
        const int value = digits.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("无法解析 HEX 字节：%1").arg(digits.mid(i, 2));
            }
            return false;
        }
        bytes.append(static_cast<char>(value));
    }

    if (out) {
        *out = bytes;
    }
    return true;
}

QString formatHex(const QByteArray &bytes)
{
    QStringList parts;
    parts.reserve(bytes.size());
    for (char raw : bytes) {
        const auto value = static_cast<quint8>(raw);
        parts.append(QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper());
    }
    return parts.join(QLatin1Char(' '));
}

QString endpointName(const QHostAddress &address, quint16 port)
{
    return QStringLiteral("%1:%2").arg(address.toString()).arg(port);
}

class NetworkServicesView : public QWidget
{
public:
    explicit NetworkServicesView(QWidget *parent)
        : QWidget(parent)
    {
        buildUi();

        m_tcpServer = new QTcpServer(this);
        m_tcpClient = new QTcpSocket(this);
        m_udpSocket = new QUdpSocket(this);

        connect(m_tcpServer, &QTcpServer::newConnection, this,
                [this] { acceptTcpConnections(); });
        connect(m_tcpClient, &QTcpSocket::connected, this, [this] {
            appendLog(m_tcpLog, QStringLiteral("TCP 已连接到 %1:%2")
                                    .arg(m_tcpAddress->text().trimmed())
                                    .arg(m_tcpPort->value()));
            setTcpStatus(QStringLiteral("已连接"), true);
            updateTcpControls();
        });
        connect(m_tcpClient, &QTcpSocket::disconnected, this, [this] {
            if (!m_stoppingTcp) {
                appendLog(m_tcpLog, QStringLiteral("TCP 连接已断开"));
            }
            updateTcpControls();
            if (!m_stoppingTcp && m_tcpMode->currentIndex() == 0) {
                setTcpStatus(QStringLiteral("未连接"), false);
            }
        });
        connect(m_tcpClient, &QTcpSocket::readyRead, this, [this] {
            const QByteArray bytes = m_tcpClient->readAll();
            appendBytes(m_tcpLog, QStringLiteral("RX"), bytes,
                        endpointName(m_tcpClient->peerAddress(), m_tcpClient->peerPort()),
                        m_tcpHexDisplay->isChecked());
        });
        connect(m_tcpClient, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) {
                    if (m_stoppingTcp) {
                        return;
                    }
                    appendLog(m_tcpLog, QStringLiteral("TCP 错误：%1")
                                            .arg(m_tcpClient->errorString()));
                    setTcpStatus(m_tcpClient->errorString(), false);
                    updateTcpControls();
                });
        connect(m_udpSocket, &QUdpSocket::readyRead, this,
                [this] { readUdpDatagrams(); });
        connect(m_udpSocket, &QUdpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) {
                    if (!m_stoppingUdp) {
                        appendLog(m_udpLog, QStringLiteral("UDP 错误：%1")
                                                .arg(m_udpSocket->errorString()));
                        setUdpStatus(m_udpSocket->errorString(), false);
                    }
                });

        updateTcpMode();
        updateTcpControls();
        updateUdpControls();
    }

    ~NetworkServicesView() override { stopAll(); }

    void stopAll()
    {
        stopTcp();
        stopUdp();
    }

private:
    void buildUi()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(4);

        auto *tabs = new QTabWidget(this);
        tabs->addTab(buildTcpPage(), QStringLiteral("TCP"));
        tabs->addTab(buildUdpPage(), QStringLiteral("UDP"));
        layout->addWidget(tabs);
    }

    QWidget *buildTcpPage()
    {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(4, 6, 4, 4);
        layout->setSpacing(5);

        auto *form = new QFormLayout;
        m_tcpMode = new QComboBox(page);
        m_tcpMode->addItems({QStringLiteral("客户端"), QStringLiteral("服务端")});
        form->addRow(QStringLiteral("模式"), m_tcpMode);

        auto *endpointRow = new QHBoxLayout;
        m_tcpAddressLabel = new QLabel(page);
        m_tcpAddress = new QLineEdit(QStringLiteral("127.0.0.1"), page);
        m_tcpPort = new QSpinBox(page);
        m_tcpPort->setRange(1, 65535);
        m_tcpPort->setValue(9000);
        endpointRow->addWidget(m_tcpAddress, 1);
        endpointRow->addWidget(new QLabel(QStringLiteral("端口"), page));
        endpointRow->addWidget(m_tcpPort);
        form->addRow(m_tcpAddressLabel, endpointRow);
        layout->addLayout(form);

        auto *actions = new QHBoxLayout;
        m_tcpStartButton = new QPushButton(page);
        m_tcpStopButton = new QPushButton(QStringLiteral("停止"), page);
        m_tcpStatus = new QLabel(QStringLiteral("未连接"), page);
        actions->addWidget(m_tcpStartButton);
        actions->addWidget(m_tcpStopButton);
        actions->addWidget(m_tcpStatus, 1);
        layout->addLayout(actions);

        auto *logOptions = new QHBoxLayout;
        m_tcpHexDisplay = new QCheckBox(QStringLiteral("HEX 显示"), page);
        auto *clearLog = new QPushButton(QStringLiteral("清空日志"), page);
        logOptions->addWidget(m_tcpHexDisplay);
        logOptions->addStretch();
        logOptions->addWidget(clearLog);
        layout->addLayout(logOptions);

        m_tcpLog = new QPlainTextEdit(page);
        m_tcpLog->setReadOnly(true);
        m_tcpLog->document()->setMaximumBlockCount(5000);
        m_tcpLog->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(m_tcpLog, 1);

        auto *sendOptions = new QHBoxLayout;
        m_tcpHexSend = new QCheckBox(QStringLiteral("HEX 发送"), page);
        m_tcpSendButton = new QPushButton(QStringLiteral("发送"), page);
        sendOptions->addWidget(m_tcpHexSend);
        sendOptions->addStretch();
        sendOptions->addWidget(m_tcpSendButton);
        layout->addLayout(sendOptions);

        m_tcpSend = new QPlainTextEdit(page);
        m_tcpSend->setPlaceholderText(QStringLiteral("输入 UTF-8 文本或 HEX 数据"));
        m_tcpSend->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_tcpSend->setMaximumHeight(100);
        layout->addWidget(m_tcpSend);

        connect(m_tcpMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int) { updateTcpMode(); });
        connect(m_tcpStartButton, &QPushButton::clicked, this, [this] { startTcp(); });
        connect(m_tcpStopButton, &QPushButton::clicked, this, [this] { stopTcp(); });
        connect(m_tcpSendButton, &QPushButton::clicked, this, [this] { sendTcp(); });
        connect(clearLog, &QPushButton::clicked, m_tcpLog, &QPlainTextEdit::clear);
        return page;
    }

    QWidget *buildUdpPage()
    {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(4, 6, 4, 4);
        layout->setSpacing(5);

        auto *form = new QFormLayout;
        auto *bindRow = new QHBoxLayout;
        m_udpBindAddress = new QLineEdit(QStringLiteral("0.0.0.0"), page);
        m_udpBindPort = new QSpinBox(page);
        m_udpBindPort->setRange(1, 65535);
        m_udpBindPort->setValue(9001);
        bindRow->addWidget(m_udpBindAddress, 1);
        bindRow->addWidget(new QLabel(QStringLiteral("端口"), page));
        bindRow->addWidget(m_udpBindPort);
        form->addRow(QStringLiteral("本地监听"), bindRow);

        auto *targetRow = new QHBoxLayout;
        m_udpTargetAddress = new QLineEdit(QStringLiteral("127.0.0.1"), page);
        m_udpTargetPort = new QSpinBox(page);
        m_udpTargetPort->setRange(1, 65535);
        m_udpTargetPort->setValue(9001);
        targetRow->addWidget(m_udpTargetAddress, 1);
        targetRow->addWidget(new QLabel(QStringLiteral("端口"), page));
        targetRow->addWidget(m_udpTargetPort);
        form->addRow(QStringLiteral("目标 IP"), targetRow);
        layout->addLayout(form);

        auto *actions = new QHBoxLayout;
        m_udpStartButton = new QPushButton(QStringLiteral("绑定/开始接收"), page);
        m_udpStopButton = new QPushButton(QStringLiteral("停止"), page);
        m_udpStatus = new QLabel(QStringLiteral("未绑定"), page);
        actions->addWidget(m_udpStartButton);
        actions->addWidget(m_udpStopButton);
        actions->addWidget(m_udpStatus, 1);
        layout->addLayout(actions);

        auto *logOptions = new QHBoxLayout;
        m_udpHexDisplay = new QCheckBox(QStringLiteral("HEX 显示"), page);
        auto *clearLog = new QPushButton(QStringLiteral("清空日志"), page);
        logOptions->addWidget(m_udpHexDisplay);
        logOptions->addStretch();
        logOptions->addWidget(clearLog);
        layout->addLayout(logOptions);

        m_udpLog = new QPlainTextEdit(page);
        m_udpLog->setReadOnly(true);
        m_udpLog->document()->setMaximumBlockCount(5000);
        m_udpLog->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(m_udpLog, 1);

        auto *sendOptions = new QHBoxLayout;
        m_udpHexSend = new QCheckBox(QStringLiteral("HEX 发送"), page);
        m_udpSendButton = new QPushButton(QStringLiteral("发送"), page);
        sendOptions->addWidget(m_udpHexSend);
        sendOptions->addStretch();
        sendOptions->addWidget(m_udpSendButton);
        layout->addLayout(sendOptions);

        m_udpSend = new QPlainTextEdit(page);
        m_udpSend->setPlaceholderText(QStringLiteral("输入 UTF-8 文本或 HEX 数据"));
        m_udpSend->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_udpSend->setMaximumHeight(100);
        layout->addWidget(m_udpSend);

        connect(m_udpStartButton, &QPushButton::clicked, this, [this] { startUdp(); });
        connect(m_udpStopButton, &QPushButton::clicked, this, [this] { stopUdp(); });
        connect(m_udpSendButton, &QPushButton::clicked, this, [this] { sendUdp(); });
        connect(clearLog, &QPushButton::clicked, m_udpLog, &QPlainTextEdit::clear);
        return page;
    }

    void updateTcpMode()
    {
        const bool serverMode = m_tcpMode->currentIndex() == 1;
        m_tcpAddressLabel->setText(serverMode ? QStringLiteral("监听地址")
                                              : QStringLiteral("目标主机"));
        m_tcpAddress->setText(serverMode ? QStringLiteral("0.0.0.0")
                                         : QStringLiteral("127.0.0.1"));
        m_tcpStartButton->setText(serverMode ? QStringLiteral("开始监听")
                                             : QStringLiteral("连接"));
        updateTcpControls();
    }

    void updateTcpControls()
    {
        const bool serverMode = m_tcpMode->currentIndex() == 1;
        const bool active = serverMode ? m_tcpServer->isListening()
                                       : m_tcpClient->state() != QAbstractSocket::UnconnectedState;
        m_tcpMode->setEnabled(!active);
        m_tcpAddress->setEnabled(!active);
        m_tcpPort->setEnabled(!active);
        m_tcpStartButton->setEnabled(!active);
        m_tcpStopButton->setEnabled(active);
    }

    void startTcp()
    {
        const quint16 port = static_cast<quint16>(m_tcpPort->value());
        if (m_tcpMode->currentIndex() == 1) {
            QHostAddress address;
            const QString addressText = m_tcpAddress->text().trimmed();
            if (addressText.isEmpty() || addressText == QStringLiteral("*")) {
                address = QHostAddress::AnyIPv4;
            } else if (!address.setAddress(addressText)) {
                setTcpStatus(QStringLiteral("监听地址必须是 IP 地址"), false);
                return;
            }

            if (!m_tcpServer->listen(address, port)) {
                const QString error = m_tcpServer->errorString();
                setTcpStatus(error, false);
                appendLog(m_tcpLog, QStringLiteral("TCP 监听失败：%1").arg(error));
                return;
            }
            appendLog(m_tcpLog, QStringLiteral("TCP 服务端已监听 %1")
                                    .arg(endpointName(m_tcpServer->serverAddress(),
                                                      m_tcpServer->serverPort())));
            setTcpStatus(QStringLiteral("监听中"), true);
        } else {
            const QString host = m_tcpAddress->text().trimmed();
            if (host.isEmpty()) {
                setTcpStatus(QStringLiteral("请输入目标主机"), false);
                return;
            }
            if (m_tcpClient->state() != QAbstractSocket::UnconnectedState) {
                m_tcpClient->abort();
            }
            m_tcpClient->connectToHost(host, port);
            appendLog(m_tcpLog, QStringLiteral("正在连接 %1:%2")
                                    .arg(host)
                                    .arg(port));
            setTcpStatus(QStringLiteral("连接中…"), false);
        }
        updateTcpControls();
    }

    void stopTcp()
    {
        m_stoppingTcp = true;
        m_tcpServer->close();
        const QSet<QTcpSocket *> peers = m_tcpPeers;
        m_tcpPeers.clear();
        for (QTcpSocket *peer : peers) {
            if (!peer) {
                continue;
            }
            disconnect(peer, nullptr, this, nullptr);
            peer->abort();
            peer->deleteLater();
        }
        if (m_tcpClient->state() != QAbstractSocket::UnconnectedState) {
            m_tcpClient->abort();
        }
        m_stoppingTcp = false;
        updateTcpControls();
        setTcpStatus(QStringLiteral("已停止"), false);
    }

    void acceptTcpConnections()
    {
        while (m_tcpServer->hasPendingConnections()) {
            QTcpSocket *socket = m_tcpServer->nextPendingConnection();
            if (!socket) {
                continue;
            }
            socket->setParent(this);
            m_tcpPeers.insert(socket);
            const QString peer = endpointName(socket->peerAddress(), socket->peerPort());
            appendLog(m_tcpLog, QStringLiteral("TCP 客户端已连接：%1").arg(peer));

            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                const QByteArray bytes = socket->readAll();
                appendBytes(m_tcpLog, QStringLiteral("RX"), bytes,
                            endpointName(socket->peerAddress(), socket->peerPort()),
                            m_tcpHexDisplay->isChecked());
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket, peer] {
                m_tcpPeers.remove(socket);
                if (!m_stoppingTcp) {
                    appendLog(m_tcpLog, QStringLiteral("TCP 客户端已断开：%1").arg(peer));
                }
                socket->deleteLater();
            });
            connect(socket, &QTcpSocket::errorOccurred, this,
                    [this, socket](QAbstractSocket::SocketError) {
                        if (!m_stoppingTcp) {
                            appendLog(m_tcpLog, QStringLiteral("TCP 客户端错误：%1")
                                                    .arg(socket->errorString()));
                        }
                    });
        }
        updateTcpControls();
    }

    void sendTcp()
    {
        QByteArray bytes;
        QString error;
        if (!readSendData(m_tcpSend, m_tcpHexSend, &bytes, &error)) {
            setTcpStatus(error, false);
            return;
        }
        if (bytes.isEmpty()) {
            setTcpStatus(QStringLiteral("发送数据为空"), false);
            return;
        }

        if (m_tcpMode->currentIndex() == 1) {
            if (m_tcpPeers.isEmpty()) {
                setTcpStatus(QStringLiteral("当前没有已连接的客户端"), false);
                return;
            }
            int sentCount = 0;
            const QSet<QTcpSocket *> peers = m_tcpPeers;
            for (QTcpSocket *peer : peers) {
                if (peer && peer->state() == QAbstractSocket::ConnectedState
                    && peer->write(bytes) >= 0) {
                    ++sentCount;
                    appendBytes(m_tcpLog, QStringLiteral("TX"), bytes,
                                endpointName(peer->peerAddress(), peer->peerPort()),
                                m_tcpHexDisplay->isChecked() || m_tcpHexSend->isChecked());
                }
            }
            setTcpStatus(QStringLiteral("已发送到 %1 个客户端").arg(sentCount), sentCount > 0);
            return;
        }

        if (m_tcpClient->state() != QAbstractSocket::ConnectedState) {
            setTcpStatus(QStringLiteral("TCP 尚未连接"), false);
            return;
        }
        if (m_tcpClient->write(bytes) < 0) {
            setTcpStatus(m_tcpClient->errorString(), false);
            return;
        }
        appendBytes(m_tcpLog, QStringLiteral("TX"), bytes, QString(),
                    m_tcpHexDisplay->isChecked() || m_tcpHexSend->isChecked());
        setTcpStatus(QStringLiteral("已发送 %1 字节").arg(bytes.size()), true);
    }

    void startUdp()
    {
        QHostAddress bindAddress;
        const QString addressText = m_udpBindAddress->text().trimmed();
        if (addressText.isEmpty() || addressText == QStringLiteral("*")) {
            bindAddress = QHostAddress::AnyIPv4;
        } else if (!bindAddress.setAddress(addressText)) {
            setUdpStatus(QStringLiteral("监听地址必须是 IP 地址"), false);
            return;
        }

        if (m_udpSocket->state() != QAbstractSocket::UnconnectedState) {
            m_udpSocket->close();
        }
        if (!m_udpSocket->bind(bindAddress, static_cast<quint16>(m_udpBindPort->value()))) {
            const QString error = m_udpSocket->errorString();
            setUdpStatus(error, false);
            appendLog(m_udpLog, QStringLiteral("UDP 绑定失败：%1").arg(error));
            return;
        }

        appendLog(m_udpLog, QStringLiteral("UDP 已绑定 %1")
                                .arg(endpointName(m_udpSocket->localAddress(),
                                                  m_udpSocket->localPort())));
        setUdpStatus(QStringLiteral("接收中"), true);
        updateUdpControls();
    }

    void stopUdp()
    {
        m_stoppingUdp = true;
        m_udpSocket->close();
        m_stoppingUdp = false;
        updateUdpControls();
        setUdpStatus(QStringLiteral("已停止"), false);
    }

    void updateUdpControls()
    {
        const bool bound = m_udpSocket->state() == QAbstractSocket::BoundState;
        m_udpBindAddress->setEnabled(!bound);
        m_udpBindPort->setEnabled(!bound);
        m_udpStartButton->setEnabled(!bound);
        m_udpStopButton->setEnabled(bound);
    }

    void readUdpDatagrams()
    {
        while (m_udpSocket->hasPendingDatagrams()) {
            const qint64 pendingSize = m_udpSocket->pendingDatagramSize();
            if (pendingSize < 0) {
                break;
            }
            QByteArray bytes;
            bytes.resize(static_cast<int>(pendingSize));
            QHostAddress sender;
            quint16 senderPort = 0;
            const qint64 received =
                m_udpSocket->readDatagram(bytes.data(), bytes.size(), &sender, &senderPort);
            if (received < 0) {
                appendLog(m_udpLog, QStringLiteral("读取 UDP 数据失败：%1")
                                        .arg(m_udpSocket->errorString()));
                break;
            }
            bytes.resize(static_cast<int>(received));
            appendBytes(m_udpLog, QStringLiteral("RX"), bytes,
                        endpointName(sender, senderPort), m_udpHexDisplay->isChecked());
        }
    }

    void sendUdp()
    {
        if (m_udpSocket->state() != QAbstractSocket::BoundState) {
            setUdpStatus(QStringLiteral("请先绑定本地端口"), false);
            return;
        }

        QByteArray bytes;
        QString error;
        if (!readSendData(m_udpSend, m_udpHexSend, &bytes, &error)) {
            setUdpStatus(error, false);
            return;
        }
        if (bytes.isEmpty()) {
            setUdpStatus(QStringLiteral("发送数据为空"), false);
            return;
        }

        QHostAddress target;
        if (!target.setAddress(m_udpTargetAddress->text().trimmed())) {
            setUdpStatus(QStringLiteral("目标地址必须是 IPv4 或 IPv6 地址"), false);
            return;
        }
        const quint16 port = static_cast<quint16>(m_udpTargetPort->value());
        const qint64 sent = m_udpSocket->writeDatagram(bytes, target, port);
        if (sent < 0) {
            setUdpStatus(m_udpSocket->errorString(), false);
            return;
        }
        appendBytes(m_udpLog, QStringLiteral("TX"), bytes, endpointName(target, port),
                    m_udpHexDisplay->isChecked() || m_udpHexSend->isChecked());
        setUdpStatus(QStringLiteral("已发送 %1 字节").arg(sent), true);
    }

    bool readSendData(QPlainTextEdit *edit, QCheckBox *hex, QByteArray *bytes,
                      QString *error) const
    {
        const QString text = edit->toPlainText();
        if (hex->isChecked()) {
            return parseHex(text, bytes, error);
        }
        *bytes = text.toUtf8();
        return true;
    }

    void appendLog(QPlainTextEdit *log, const QString &message)
    {
        if (!log) {
            return;
        }
        const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
        log->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
    }

    void appendBytes(QPlainTextEdit *log, const QString &direction, const QByteArray &bytes,
                     const QString &peer, bool asHex)
    {
        const QString data = asHex
                                 ? formatHex(bytes)
                                 : QString::fromUtf8(bytes.constData(), bytes.size());
        const QString peerText = peer.isEmpty() ? QString() : QStringLiteral(" %1").arg(peer);
        appendLog(log, QStringLiteral("%1%2 (%3 字节)：%4")
                            .arg(direction, peerText)
                            .arg(bytes.size())
                            .arg(data));
    }

    void setTcpStatus(const QString &text, bool good)
    {
        m_tcpStatus->setText(text);
        m_tcpStatus->setStyleSheet(good ? QStringLiteral("color:#89d185;")
                                        : QStringLiteral("color:#f48771;"));
    }

    void setUdpStatus(const QString &text, bool good)
    {
        m_udpStatus->setText(text);
        m_udpStatus->setStyleSheet(good ? QStringLiteral("color:#89d185;")
                                        : QStringLiteral("color:#f48771;"));
    }

    QTcpServer *m_tcpServer = nullptr;
    QTcpSocket *m_tcpClient = nullptr;
    QSet<QTcpSocket *> m_tcpPeers;
    QUdpSocket *m_udpSocket = nullptr;
    bool m_stoppingTcp = false;
    bool m_stoppingUdp = false;

    QComboBox *m_tcpMode = nullptr;
    QLabel *m_tcpAddressLabel = nullptr;
    QLineEdit *m_tcpAddress = nullptr;
    QSpinBox *m_tcpPort = nullptr;
    QPushButton *m_tcpStartButton = nullptr;
    QPushButton *m_tcpStopButton = nullptr;
    QLabel *m_tcpStatus = nullptr;
    QCheckBox *m_tcpHexDisplay = nullptr;
    QCheckBox *m_tcpHexSend = nullptr;
    QPlainTextEdit *m_tcpLog = nullptr;
    QPlainTextEdit *m_tcpSend = nullptr;
    QPushButton *m_tcpSendButton = nullptr;

    QLineEdit *m_udpBindAddress = nullptr;
    QSpinBox *m_udpBindPort = nullptr;
    QLineEdit *m_udpTargetAddress = nullptr;
    QSpinBox *m_udpTargetPort = nullptr;
    QPushButton *m_udpStartButton = nullptr;
    QPushButton *m_udpStopButton = nullptr;
    QLabel *m_udpStatus = nullptr;
    QCheckBox *m_udpHexDisplay = nullptr;
    QCheckBox *m_udpHexSend = nullptr;
    QPlainTextEdit *m_udpLog = nullptr;
    QPlainTextEdit *m_udpSend = nullptr;
    QPushButton *m_udpSendButton = nullptr;
};

} // namespace

class NetworkServicesPlugin : public QObject, public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID XCOM_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(xcom::IPlugin)

public:
    explicit NetworkServicesPlugin(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    bool initialize(IPluginContext &context, Error &error) override
    {
        m_context = &context;
        m_showHandler = std::make_unique<ShowHandler>(this);

        CommandSpec show;
        show.id = QStringLiteral("network.services.show");
        show.title = QStringLiteral("打开 TCP/UDP 服务工具");
        show.category = QStringLiteral("工具");
        if (!context.commands().registerCommand(show, m_showHandler.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("命令 network.services.show 注册失败"));
            return false;
        }

        IUiContext *ui = context.ui();
        if (!ui) {
            return true;
        }

        class Factory : public IViewFactory
        {
        public:
            explicit Factory(NetworkServicesPlugin *plugin)
                : m_plugin(plugin)
            {
            }

            QWidget *create(QWidget *parent, IViewHost *) override
            {
                auto *view = new NetworkServicesView(parent);
                m_plugin->m_view = view;
                return view;
            }

        private:
            NetworkServicesPlugin *m_plugin = nullptr;
        };

        ViewSpec view;
        view.id = QStringLiteral("com.xcom.tools.network-services.view");
        view.title = QStringLiteral("TCP/UDP 服务");
        view.icon = QStringLiteral("network");
        view.location = ViewLocation::Sidebar;
        view.order = 350;

        auto factory = std::make_unique<Factory>(this);
        if (!ui->views().registerView(view, factory.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("TCP/UDP 服务视图注册失败"));
            return false;
        }
        m_factory = std::move(factory);

        MenuContribution menu;
        menu.menu = QStringLiteral("menu/tools");
        menu.group = QStringLiteral("network-services");
        menu.order = 30;
        menu.command = QStringLiteral("network.services.show");
        menu.label = QStringLiteral("TCP/UDP 服务");
        if (!ui->menus().addItem(menu)) {
            error = Error::make(ErrorCode::Internal, QStringLiteral("菜单贡献注册失败"));
            return false;
        }
        return true;
    }

    bool activate(Error &error) override
    {
        Q_UNUSED(error)
        return true;
    }

    void deactivate() override
    {
        if (m_view) {
            m_view->stopAll();
        }
        m_view = nullptr;
    }

    void shutdown() override
    {
        deactivate();
        m_factory.reset();
        m_showHandler.reset();
        m_context = nullptr;
    }

    CommandResult showView()
    {
        if (!m_context || !m_context->ui()) {
            return CommandResult::failure(QStringLiteral("当前没有图形界面"));
        }
        m_context->ui()->showView(QStringLiteral("com.xcom.tools.network-services.view"));
        return CommandResult::success();
    }

private:
    class ShowHandler : public ICommandHandler
    {
    public:
        explicit ShowHandler(NetworkServicesPlugin *plugin)
            : m_plugin(plugin)
        {
        }

        CommandResult execute(const CommandInvocation &, ICommandProgress *) override
        {
            return m_plugin ? m_plugin->showView()
                            : CommandResult::failure(QStringLiteral("插件已卸载"));
        }

    private:
        NetworkServicesPlugin *m_plugin = nullptr;
    };

    IPluginContext *m_context = nullptr;
    std::unique_ptr<IViewFactory> m_factory;
    QPointer<NetworkServicesView> m_view;
    std::unique_ptr<ShowHandler> m_showHandler;
};

#include "plugin.moc"
