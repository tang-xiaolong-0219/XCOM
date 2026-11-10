#include "shell/session_dialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace xcom {

namespace {
const QString kLoopbackPort = QStringLiteral("LOOPBACK");
}

SessionDialog::SessionDialog(const QString &defaultPort, QWidget *parent)
    : QDialog(parent)
    , m_preferredPort(defaultPort)
    , m_autoSelectPort(defaultPort.isEmpty())
{
    setWindowTitle(QStringLiteral("串口配置"));
    // 此页也会嵌入会话底部面板；给组合框留出空间，避免流控和校验位文字被压缩。
    setMinimumWidth(500);

    auto *layout = new QVBoxLayout(this);
    auto *rows = new QVBoxLayout;
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(8);
    auto addField = [this](QHBoxLayout *row, const QString &labelText, QWidget *field,
                           int labelWidth, int fieldStretch = 0) {
        auto *label = new QLabel(labelText, this);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setFixedWidth(labelWidth);
        row->addWidget(label);
        row->addWidget(field, fieldStretch);
    };

    m_port = new QComboBox(this);
    m_port->setMinimumWidth(120);

    m_baud = new QComboBox(this);
    m_baud->setEditable(true);
    m_baud->setMinimumWidth(95);
    for (int rate : {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600})
        m_baud->addItem(QString::number(rate));
    m_baud->setCurrentText(QStringLiteral("115200"));

    m_dataBits = new QComboBox(this);
    for (const QString &value : {QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"),
                                 QStringLiteral("8")})
        m_dataBits->addItem(value, value.toInt());
    m_dataBits->setCurrentIndex(3);

    m_stopBits = new QComboBox(this);
    m_stopBits->addItem(QStringLiteral("1"), 1);
    m_stopBits->addItem(QStringLiteral("2"), 2);

    m_parity = new QComboBox(this);
    m_parity->addItem(QStringLiteral("无"), 0);
    m_parity->addItem(QStringLiteral("偶校验"), 1);
    m_parity->addItem(QStringLiteral("奇校验"), 2);
    m_parity->addItem(QStringLiteral("标记"), 3);
    m_parity->addItem(QStringLiteral("空格"), 4);

    m_flow = new QComboBox(this);
    m_flow->addItem(QStringLiteral("无"), 0);
    m_flow->addItem(QStringLiteral("RTS/CTS"), 1);
    m_flow->addItem(QStringLiteral("XON/XOFF"), 2);
    m_flow->setMinimumWidth(130);
    m_flow->setToolTip(QStringLiteral("无、硬件 RTS/CTS 或软件 XON/XOFF"));

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(QStringLiteral("会话名称（默认使用串口名）"));
    m_applyButton = new QPushButton(QStringLiteral("应用"), this);
    m_applyButton->setVisible(false);

    m_dataBits->setMinimumWidth(50);
    m_stopBits->setMinimumWidth(50);
    m_parity->setMinimumWidth(95);

    auto *nameRow = new QHBoxLayout;
    nameRow->setSpacing(4);
    addField(nameRow, QStringLiteral("会话名称"), m_name, 56, 1);
    nameRow->addWidget(m_applyButton);
    rows->addLayout(nameRow);

    auto *portRow = new QHBoxLayout;
    portRow->setSpacing(4);
    addField(portRow, QStringLiteral("串口"), m_port, 30, 2);
    addField(portRow, QStringLiteral("波特率"), m_baud, 48, 1);
    addField(portRow, QStringLiteral("流控"), m_flow, 30, 1);
    rows->addLayout(portRow);

    auto *formatRow = new QHBoxLayout;
    formatRow->setSpacing(4);
    addField(formatRow, QStringLiteral("数据位"), m_dataBits, 44, 1);
    addField(formatRow, QStringLiteral("停止位"), m_stopBits, 44, 1);
    addField(formatRow, QStringLiteral("校验位"), m_parity, 44, 1);
    rows->addLayout(formatRow);

    layout->addLayout(rows);
    layout->addSpacing(6);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("打开"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_embedded) emit configurationApplied(descriptor());
        else accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, [this] {
        if (!m_embedded) reject();
    });
    connect(m_applyButton, &QPushButton::clicked, this, [this] {
        emit configurationApplied(descriptor());
    });
    connect(m_port, &QComboBox::currentIndexChanged, this, [this] {
        m_autoSelectPort = false;
        updateDefaultName();
    });
    connect(m_name, &QLineEdit::textEdited, this, [this] { m_nameEdited = true; });

    refreshPorts();
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &SessionDialog::refreshPorts);
    m_refreshTimer->start();
}

SessionDialog::SessionDialog(const SessionDescriptor &initial, QWidget *parent)
    : SessionDialog(initial.transport == QStringLiteral("loopback")
                        ? kLoopbackPort : initial.config.portName, parent)
{
    m_baud->setCurrentText(QString::number(initial.config.baudRate));
    m_dataBits->setCurrentIndex(m_dataBits->findData(initial.config.dataBits));
    m_stopBits->setCurrentIndex(m_stopBits->findData(initial.config.stopBits));
    m_parity->setCurrentIndex(m_parity->findData(initial.config.parity));
    m_flow->setCurrentIndex(m_flow->findData(initial.config.flow));
    m_name->setText(initial.name);
    const bool autoName = initial.name.isEmpty()
                          || initial.name == initial.config.portName
                          || initial.name == QStringLiteral("回环测试");
    m_nameEdited = !autoName;
    if (autoName) updateDefaultName();
}

void SessionDialog::setEmbeddedMode(bool embedded)
{
    m_embedded = embedded;
    if (!embedded) {
        m_applyButton->hide();
        m_buttons->show();
        return;
    }
    setWindowFlags(Qt::Widget);
    setModal(false);
    setWindowModality(Qt::NonModal);
    setEnabled(true);
    setWindowTitle(QStringLiteral("串口配置"));
    m_applyButton->show();
    m_buttons->hide();
}

void SessionDialog::setBusyPortsProvider(std::function<QStringList()> provider)
{
    m_busyPortsProvider = std::move(provider);
    refreshPorts();
}

SessionDescriptor SessionDialog::descriptor() const
{
    SessionDescriptor result;
    result.config.dtr = true;
    const bool loopback = m_port->currentData().toString() == kLoopbackPort;
    result.transport = loopback ? QStringLiteral("loopback") : QStringLiteral("serial");
    result.config.portName = loopback ? kLoopbackPort : m_port->currentData().toString();
    result.config.baudRate = m_baud->currentText().toInt();
    if (result.config.baudRate <= 0) result.config.baudRate = 115200;
    result.config.dataBits = m_dataBits->currentData().toInt();
    result.config.stopBits = m_stopBits->currentData().toInt();
    result.config.parity = m_parity->currentData().toInt();
    result.config.flow = m_flow->currentData().toInt();
    result.name = m_name->text().trimmed();
    return result;
}

void SessionDialog::refreshPorts()
{
    if (m_port->view()->isVisible()) return;
    const QString selected = m_port->currentData().toString().isEmpty()
                                 ? m_preferredPort : m_port->currentData().toString();
    const QStringList busyPorts = m_busyPortsProvider ? m_busyPortsProvider() : QStringList();
    const QSignalBlocker blocker(m_port);
    m_port->clear();
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (busyPorts.contains(info.portName(), Qt::CaseInsensitive)) continue;
        QString label = info.portName();
        if (!info.description().isEmpty()) label += QStringLiteral(" (%1)").arg(info.description());
        m_port->addItem(label, info.portName());
    }
    m_port->addItem(QStringLiteral("回环测试"), kLoopbackPort);

    int index = m_port->findData(selected);
    if (m_autoSelectPort && selected == kLoopbackPort && m_port->count() > 1)
        index = 0;
    if (index < 0) {
        index = m_port->count() > 1 ? 0 : m_port->count() - 1;
    }
    m_port->setCurrentIndex(index);
    m_preferredPort = m_port->currentData().toString();
    updateDefaultName();
}

void SessionDialog::updateDefaultName()
{
    if (m_nameEdited || !m_port || m_port->currentIndex() < 0) return;
    const QString data = m_port->currentData().toString();
    m_name->setText(data == kLoopbackPort ? QStringLiteral("回环测试") : data);
}

} // namespace xcom
