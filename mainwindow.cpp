#include "mainwindow.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringConverter>
#include <QStyle>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kDefaultBaudRate = 115200;
constexpr int kAutoSendDefaultMs = 1000;
constexpr int kMaxDisplayBlocks = 10000;

QString toHexString(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

bool parseHexString(const QString &text, QByteArray *out, QString *error)
{
    QString digits;
    digits.reserve(text.size());

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch.isSpace() || ch == QLatin1Char(',') || ch == QLatin1Char(';')
            || ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            continue;
        }
        if (ch == QLatin1Char('0') && i + 1 < text.size()
            && (text.at(i + 1) == QLatin1Char('x') || text.at(i + 1) == QLatin1Char('X'))) {
            ++i; // 跳过 0x 前缀
            continue;
        }
        const QChar upper = ch.toUpper();
        const bool isHexDigit = ch.isDigit()
                                || (upper >= QLatin1Char('A') && upper <= QLatin1Char('F'));
        if (!isHexDigit) {
            if (error) {
                *error = QStringLiteral("包含非法字符“%1”，只能输入 0-9、A-F 以及空格/逗号分隔符。").arg(ch);
            }
            return false;
        }
        digits.append(ch);
    }

    if (digits.size() % 2 != 0) {
        if (error) {
            *error = QStringLiteral("十六进制数字个数为奇数（%1 个），无法组成完整字节。").arg(digits.size());
        }
        return false;
    }

    out->clear();
    out->reserve(digits.size() / 2);
    for (int i = 0; i < digits.size(); i += 2) {
        bool ok = false;
        const int value = digits.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("无法解析“%1”。").arg(digits.mid(i, 2));
            }
            return false;
        }
        out->append(static_cast<char>(value));
    }
    return true;
}

QString formatByteCount(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(QString::number(kb / 1024.0, 'f', 2));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_serial(new QSerialPort(this))
{
    buildUi();
    loadSettings();
    refreshPorts();

    m_autoSendTimer = new QTimer(this);
    connect(m_autoSendTimer, &QTimer::timeout, this, &MainWindow::sendData);

    m_portScanTimer = new QTimer(this);
    m_portScanTimer->setInterval(1000);
    connect(m_portScanTimer, &QTimer::timeout, this, &MainWindow::refreshPorts);
    m_portScanTimer->start();

    connect(m_serial, &QSerialPort::readyRead, this, &MainWindow::handleReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &MainWindow::handleSerialError);

    connect(m_portBox, &QComboBox::activated, this, [this](int) {
        m_preferredPort = m_portBox->currentData().toString();
    });

    updateUiState();
    updateCounters();
    statusBar()->showMessage(QStringLiteral("就绪"), 3000);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// 界面
// ---------------------------------------------------------------------------
void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("X-COM 串口助手"));
    setMinimumSize(880, 600);
    resize(1020, 720);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 8);
    rootLayout->setSpacing(8);

    const QFont monoFont = [] {
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(10);
        return font;
    }();

    // ---------- 串口设置 ----------
    auto *settingsGroup = new QGroupBox(QStringLiteral("串口设置"), central);
    auto *grid = new QGridLayout(settingsGroup);
    grid->setContentsMargins(12, 16, 12, 12);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    auto addField = [grid, settingsGroup](const QString &text, QWidget *field, int row, int column) {
        auto *label = new QLabel(text, settingsGroup);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(label, row, column);
        grid->addWidget(field, row, column + 1);
    };

    m_portBox = new QComboBox(settingsGroup);
    m_portBox->setMinimumWidth(240);
    m_portBox->setToolTip(QStringLiteral("列出当前系统可用的串口，插入设备后会自动刷新"));

    m_refreshButton = new QPushButton(QStringLiteral("刷新"), settingsGroup);
    m_refreshButton->setToolTip(QStringLiteral("立即重新扫描串口"));
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);

    auto *portWidget = new QWidget(settingsGroup);
    auto *portLayout = new QHBoxLayout(portWidget);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->setSpacing(6);
    portLayout->addWidget(m_portBox, 1);
    portLayout->addWidget(m_refreshButton);

    m_baudBox = new QComboBox(settingsGroup);
    m_baudBox->setEditable(true);
    m_baudBox->setMinimumWidth(110);
    const QList<int> baudRates = {1200, 2400, 4800, 9600, 19200, 38400, 57600,
                                  115200, 230400, 460800, 921600};
    for (int rate : baudRates) {
        m_baudBox->addItem(QString::number(rate));
    }
    m_baudBox->setCurrentText(QString::number(kDefaultBaudRate));

    m_dataBitsBox = new QComboBox(settingsGroup);
    m_dataBitsBox->addItem(QStringLiteral("5"), int(QSerialPort::Data5));
    m_dataBitsBox->addItem(QStringLiteral("6"), int(QSerialPort::Data6));
    m_dataBitsBox->addItem(QStringLiteral("7"), int(QSerialPort::Data7));
    m_dataBitsBox->addItem(QStringLiteral("8"), int(QSerialPort::Data8));
    m_dataBitsBox->setCurrentIndex(m_dataBitsBox->findData(int(QSerialPort::Data8)));

    m_stopBitsBox = new QComboBox(settingsGroup);
    m_stopBitsBox->addItem(QStringLiteral("1"), int(QSerialPort::OneStop));
    m_stopBitsBox->addItem(QStringLiteral("1.5"), int(QSerialPort::OneAndHalfStop));
    m_stopBitsBox->addItem(QStringLiteral("2"), int(QSerialPort::TwoStop));
    m_stopBitsBox->setCurrentIndex(m_stopBitsBox->findData(int(QSerialPort::OneStop)));

    m_parityBox = new QComboBox(settingsGroup);
    m_parityBox->addItem(QStringLiteral("无 (None)"), int(QSerialPort::NoParity));
    m_parityBox->addItem(QStringLiteral("偶校验 (Even)"), int(QSerialPort::EvenParity));
    m_parityBox->addItem(QStringLiteral("奇校验 (Odd)"), int(QSerialPort::OddParity));
    m_parityBox->addItem(QStringLiteral("标记 (Mark)"), int(QSerialPort::MarkParity));
    m_parityBox->addItem(QStringLiteral("空格 (Space)"), int(QSerialPort::SpaceParity));
    m_parityBox->setCurrentIndex(m_parityBox->findData(int(QSerialPort::NoParity)));

    m_flowBox = new QComboBox(settingsGroup);
    m_flowBox->addItem(QStringLiteral("无"), int(QSerialPort::NoFlowControl));
    m_flowBox->addItem(QStringLiteral("硬件 (RTS/CTS)"), int(QSerialPort::HardwareControl));
    m_flowBox->addItem(QStringLiteral("软件 (XON/XOFF)"), int(QSerialPort::SoftwareControl));
    m_flowBox->setCurrentIndex(m_flowBox->findData(int(QSerialPort::NoFlowControl)));

    m_encodingBox = new QComboBox(settingsGroup);
    m_encodingBox->addItem(QStringLiteral("系统编码"));
    m_encodingBox->addItem(QStringLiteral("UTF-8"));
    m_encodingBox->addItem(QStringLiteral("Latin-1"));
    m_encodingBox->setToolTip(QStringLiteral("接收解码与发送编码使用的字符集"));

    m_dtrCheck = new QCheckBox(QStringLiteral("DTR"), settingsGroup);
    m_rtsCheck = new QCheckBox(QStringLiteral("RTS"), settingsGroup);
    auto *lineWidget = new QWidget(settingsGroup);
    auto *lineLayout = new QHBoxLayout(lineWidget);
    lineLayout->setContentsMargins(0, 0, 0, 0);
    lineLayout->setSpacing(12);
    lineLayout->addWidget(m_dtrCheck);
    lineLayout->addWidget(m_rtsCheck);
    lineLayout->addStretch();

    m_openButton = new QPushButton(QStringLiteral("打开串口"), settingsGroup);
    m_openButton->setObjectName(QStringLiteral("openButton"));
    m_openButton->setCursor(Qt::PointingHandCursor);
    m_openButton->setMinimumWidth(120);
    m_openButton->setProperty("open", false);
    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::togglePort);

    addField(QStringLiteral("串口"), portWidget, 0, 0);
    addField(QStringLiteral("波特率"), m_baudBox, 0, 2);
    addField(QStringLiteral("数据位"), m_dataBitsBox, 0, 4);
    addField(QStringLiteral("停止位"), m_stopBitsBox, 1, 0);
    addField(QStringLiteral("校验位"), m_parityBox, 1, 2);
    addField(QStringLiteral("流控"), m_flowBox, 1, 4);
    addField(QStringLiteral("编码"), m_encodingBox, 2, 0);

    auto *lineLabel = new QLabel(QStringLiteral("控制线"), settingsGroup);
    lineLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(lineLabel, 2, 2);
    grid->addWidget(lineWidget, 2, 3, 1, 3);
    grid->addWidget(m_openButton, 0, 6, 3, 1);

    grid->setColumnStretch(1, 3);
    grid->setColumnStretch(3, 2);
    grid->setColumnStretch(5, 2);

    // ---------- 接收区 ----------
    auto *receiveGroup = new QGroupBox(QStringLiteral("接收区"), central);
    auto *receiveLayout = new QVBoxLayout(receiveGroup);
    receiveLayout->setContentsMargins(12, 16, 12, 10);
    receiveLayout->setSpacing(6);

    auto *receiveBar = new QHBoxLayout;
    receiveBar->setSpacing(12);
    m_hexDisplayCheck = new QCheckBox(QStringLiteral("HEX 显示"), receiveGroup);
    m_timestampCheck = new QCheckBox(QStringLiteral("时间戳"), receiveGroup);
    m_autoScrollCheck = new QCheckBox(QStringLiteral("自动滚动"), receiveGroup);
    m_autoScrollCheck->setChecked(true);
    m_pauseCheck = new QCheckBox(QStringLiteral("暂停显示"), receiveGroup);
    m_pauseCheck->setToolTip(QStringLiteral("暂停后仍继续接收和计数，只是不再刷新界面"));
    m_clearReceiveButton = new QPushButton(QStringLiteral("清空接收"), receiveGroup);
    m_saveButton = new QPushButton(QStringLiteral("保存数据"), receiveGroup);

    receiveBar->addWidget(m_hexDisplayCheck);
    receiveBar->addWidget(m_timestampCheck);
    receiveBar->addWidget(m_autoScrollCheck);
    receiveBar->addWidget(m_pauseCheck);
    receiveBar->addStretch();
    receiveBar->addWidget(m_clearReceiveButton);
    receiveBar->addWidget(m_saveButton);
    receiveLayout->addLayout(receiveBar);

    m_receiveEdit = new QPlainTextEdit(receiveGroup);
    m_receiveEdit->setReadOnly(true);
    m_receiveEdit->setFont(monoFont);
    m_receiveEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_receiveEdit->setMaximumBlockCount(kMaxDisplayBlocks);
    m_receiveEdit->setPlaceholderText(QStringLiteral("接收到的数据会显示在这里 …"));
    receiveLayout->addWidget(m_receiveEdit, 1);

    connect(m_clearReceiveButton, &QPushButton::clicked, this, &MainWindow::clearReceive);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::saveReceiveData);

    // ---------- 发送区 ----------
    auto *sendGroup = new QGroupBox(QStringLiteral("发送区"), central);
    auto *sendLayout = new QVBoxLayout(sendGroup);
    sendLayout->setContentsMargins(12, 16, 12, 10);
    sendLayout->setSpacing(6);

    auto *sendBar = new QHBoxLayout;
    sendBar->setSpacing(12);
    m_hexSendCheck = new QCheckBox(QStringLiteral("HEX 发送"), sendGroup);
    m_hexSendCheck->setToolTip(QStringLiteral("按十六进制发送，例如：AA 55 01 0D 0A"));
    m_newlineCheck = new QCheckBox(QStringLiteral("发送新行 (\\r\\n)"), sendGroup);
    m_autoSendCheck = new QCheckBox(QStringLiteral("定时发送"), sendGroup);
    m_autoSendInterval = new QSpinBox(sendGroup);
    m_autoSendInterval->setRange(10, 3600000);
    m_autoSendInterval->setValue(kAutoSendDefaultMs);
    m_autoSendInterval->setSuffix(QStringLiteral(" ms"));
    m_autoSendInterval->setMinimumWidth(100);
    m_clearSendButton = new QPushButton(QStringLiteral("清空输入"), sendGroup);
    m_sendButton = new QPushButton(QStringLiteral("发送"), sendGroup);
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);

    sendBar->addWidget(m_hexSendCheck);
    sendBar->addWidget(m_newlineCheck);
    sendBar->addWidget(m_autoSendCheck);
    sendBar->addWidget(m_autoSendInterval);
    sendBar->addStretch();
    sendBar->addWidget(m_clearSendButton);
    sendBar->addWidget(m_sendButton);
    sendLayout->addLayout(sendBar);

    m_sendEdit = new QPlainTextEdit(sendGroup);
    m_sendEdit->setFont(monoFont);
    m_sendEdit->setPlaceholderText(QStringLiteral("输入要发送的数据，Ctrl+Enter 快速发送 …"));
    sendLayout->addWidget(m_sendEdit, 1);

    connect(m_clearSendButton, &QPushButton::clicked, this, &MainWindow::clearSend);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendData);
    connect(m_hexSendCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && m_autoSendTimer && m_autoSendTimer->isActive()) {
            m_autoSendCheck->setChecked(false);
        }
    });

    connect(m_autoSendCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_autoSendInterval->setEnabled(checked && m_serial->isOpen());
        if (!checked) {
            m_autoSendTimer->stop();
            return;
        }
        if (!m_serial->isOpen()) {
            statusBar()->showMessage(QStringLiteral("请先打开串口，再启用定时发送"), 4000);
            m_autoSendCheck->setChecked(false);
            return;
        }
        if (m_sendEdit->toPlainText().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("发送内容为空，无法定时发送"), 4000);
            m_autoSendCheck->setChecked(false);
            return;
        }
        if (m_hexSendCheck->isChecked()) {
            QByteArray payload;
            QString error;
            if (!parseHexString(m_sendEdit->toPlainText(), &payload, &error)) {
                QMessageBox::warning(this, QStringLiteral("HEX 数据格式错误"), error);
                m_autoSendCheck->setChecked(false);
                return;
            }
        }
        m_autoSendTimer->start(m_autoSendInterval->value());
    });

    connect(m_autoSendInterval, &QSpinBox::valueChanged, this, [this](int ms) {
        if (m_autoSendTimer->isActive()) {
            m_autoSendTimer->setInterval(ms);
        }
    });

    connect(m_dtrCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_serial->isOpen()) {
            m_serial->setDataTerminalReady(checked);
        }
    });
    connect(m_rtsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_serial->isOpen()) {
            m_serial->setRequestToSend(checked);
        }
    });
    connect(m_encodingBox, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_serial->isOpen()) {
            m_decoder = createDecoder();
        }
    });

    // ---------- 组装 ----------
    m_splitter = new QSplitter(Qt::Vertical, central);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(receiveGroup);
    m_splitter->addWidget(sendGroup);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({430, 180});

    rootLayout->addWidget(settingsGroup);
    rootLayout->addWidget(m_splitter, 1);
    setCentralWidget(central);

    m_stateLabel = new QLabel(this);
    m_counterLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_counterLabel);
    statusBar()->addPermanentWidget(m_stateLabel);

    auto *sendShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), this);
    connect(sendShortcut, &QShortcut::activated, this, &MainWindow::sendData);
    auto *clearShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(clearShortcut, &QShortcut::activated, this, &MainWindow::clearReceive);

    setStyleSheet(QStringLiteral(
        "QGroupBox { margin-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QPushButton#openButton { min-height: 32px; font-weight: 600; }"
        "QPushButton#openButton[open=\"true\"] { background-color: #d9534f; color: #ffffff;"
        " border: 1px solid #c9302c; border-radius: 4px; }"
        "QPushButton#sendButton { min-height: 30px; min-width: 92px; font-weight: 600;"
        " color: #ffffff; background-color: #2f80ed; border: none; border-radius: 4px; }"
        "QPushButton#sendButton:hover { background-color: #1f6fd8; }"
        "QPushButton#sendButton:pressed { background-color: #1a5fb4; }"
        "QPushButton#sendButton:disabled { background-color: #b8c2cf; color: #f2f2f2; }"));
}

// ---------------------------------------------------------------------------
// 串口操作
// ---------------------------------------------------------------------------
void MainWindow::refreshPorts()
{
    // 串口已打开，或用户正展开下拉列表时，不打断当前操作
    if (m_serial->isOpen() || (m_portBox->view() && m_portBox->view()->isVisible())) {
        return;
    }

    QString desired = m_portBox->currentData().toString();
    if (desired.isEmpty()) {
        desired = m_preferredPort;
    }

    const QSignalBlocker blocker(m_portBox);
    m_portBox->clear();

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString label = info.portName();
        if (!info.description().isEmpty()) {
            label += QStringLiteral(" (%1)").arg(info.description());
        }
        m_portBox->addItem(label, info.portName());
    }

    if (m_portBox->count() == 0) {
        m_portBox->addItem(QStringLiteral("未检测到串口"), QString());
        m_portBox->setToolTip(QStringLiteral("没有找到可用的串口设备"));
    } else {
        m_portBox->setToolTip(QStringLiteral("共 %1 个串口").arg(m_portBox->count()));
        const int index = m_portBox->findData(desired);
        if (index >= 0) {
            m_portBox->setCurrentIndex(index);
        }
    }

    updateUiState();
}

void MainWindow::togglePort()
{
    if (m_serial->isOpen()) {
        closePort();
    } else {
        openPort();
    }
}

bool MainWindow::openPort()
{
    const QString portName = m_portBox->currentData().toString();
    if (portName.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有可用的串口，请检查设备是否已连接"), 4000);
        return false;
    }

    bool baudOk = false;
    int baudRate = m_baudBox->currentText().trimmed().toInt(&baudOk);
    if (!baudOk || baudRate <= 0) {
        baudRate = kDefaultBaudRate;
        m_baudBox->setCurrentText(QString::number(baudRate));
    }

    m_serial->setPortName(portName);

    bool configured = m_serial->setBaudRate(baudRate);
    configured = m_serial->setDataBits(
                     static_cast<QSerialPort::DataBits>(m_dataBitsBox->currentData().toInt()))
                 && configured;
    configured = m_serial->setStopBits(
                     static_cast<QSerialPort::StopBits>(m_stopBitsBox->currentData().toInt()))
                 && configured;
    configured = m_serial->setParity(
                     static_cast<QSerialPort::Parity>(m_parityBox->currentData().toInt()))
                 && configured;
    configured = m_serial->setFlowControl(
                     static_cast<QSerialPort::FlowControl>(m_flowBox->currentData().toInt()))
                 && configured;

    if (!configured) {
        statusBar()->showMessage(
            QStringLiteral("串口参数设置失败：%1").arg(m_serial->errorString()), 5000);
    }

    if (!m_serial->open(QIODevice::ReadWrite)) {
        QMessageBox::warning(this, QStringLiteral("打开串口失败"),
                             QStringLiteral("无法打开串口 %1：\n%2\n\n请确认串口未被其他程序占用。")
                                 .arg(portName, m_serial->errorString()));
        return false;
    }

    m_serial->setDataTerminalReady(m_dtrCheck->isChecked());
    m_serial->setRequestToSend(m_rtsCheck->isChecked());
    m_decoder = createDecoder();
    m_portScanTimer->stop();

    updateUiState();
    statusBar()->showMessage(
        QStringLiteral("已打开 %1 · %2 8%3%4")
            .arg(portName)
            .arg(baudRate)
            .arg(m_parityBox->currentText().left(1))
            .arg(m_stopBitsBox->currentText()), 4000);
    return true;
}

void MainWindow::closePort()
{
    if (m_closing) {
        return;
    }
    m_closing = true;

    const QString portName = m_serial->portName();
    m_autoSendTimer->stop();
    m_autoSendCheck->setChecked(false);

    if (m_serial->isOpen()) {
        m_serial->close();
    }
    m_decoder.reset();
    m_atLineStart = true;

    updateUiState();
    if (!m_portScanTimer->isActive()) {
        m_portScanTimer->start();
    }
    if (!portName.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("已关闭 %1").arg(portName), 3000);
    }

    m_closing = false;
}

void MainWindow::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || m_closing) {
        return;
    }

    const QString message = QStringLiteral("串口错误：%1").arg(m_serial->errorString());

    switch (error) {
    case QSerialPort::ResourceError:
    case QSerialPort::DeviceNotFoundError:
    case QSerialPort::PermissionError:
    case QSerialPort::ReadError:
    case QSerialPort::WriteError:
        if (m_serial->isOpen()) {
            closePort();
        }
        break;
    default:
        break;
    }

    statusBar()->showMessage(message, 8000);
}

// ---------------------------------------------------------------------------
// 接收
// ---------------------------------------------------------------------------
void MainWindow::handleReadyRead()
{
    const QByteArray data = m_serial->readAll();
    if (data.isEmpty()) {
        return;
    }

    m_rxBytes += data.size();
    updateCounters();

    if (m_pauseCheck->isChecked()) {
        return;
    }

    if (m_hexDisplayCheck->isChecked()) {
        appendIncoming(toHexString(data) + QLatin1Char('\n'));
        return;
    }

    const QString text = decodeText(data);
    if (!text.isEmpty()) {
        appendIncoming(text);
    }
}

QString MainWindow::decodeText(const QByteArray &data)
{
    if (!m_decoder) {
        m_decoder = createDecoder();
    }
    if (!m_decoder || !m_decoder->isValid()) {
        return QString::fromLatin1(data);
    }

    const QString decoded = m_decoder->decode(QByteArrayView(data));
    if (m_decoder->hasError()) {
        m_decoder->resetState();
        return QString::fromLatin1(data);
    }
    return decoded;
}

void MainWindow::appendIncoming(const QString &text)
{
    QString content;
    if (m_timestampCheck->isChecked()) {
        if (!m_atLineStart) {
            content.append(QLatin1Char('\n'));
        }
        content.append(QStringLiteral("[%1] ")
                           .arg(QDateTime::currentDateTime().toString(
                               QStringLiteral("HH:mm:ss.zzz"))));
    }
    content.append(text);

    appendReceiveText(content);
    m_atLineStart = content.endsWith(QLatin1Char('\n'));
}

void MainWindow::appendReceiveText(const QString &text)
{
    QTextCursor cursor(m_receiveEdit->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);

    if (m_autoScrollCheck->isChecked()) {
        QScrollBar *bar = m_receiveEdit->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

// ---------------------------------------------------------------------------
// 发送
// ---------------------------------------------------------------------------
QByteArray MainWindow::encodeText(const QString &text) const
{
    switch (m_encodingBox->currentIndex()) {
    case 1:
        return text.toUtf8();
    case 2:
        return text.toLatin1();
    default: {
        QStringEncoder encoder(QStringConverter::System);
        if (!encoder.isValid()) {
            return text.toLocal8Bit();
        }
        return encoder.encode(QStringView(text));
    }
    }
}

std::unique_ptr<QStringDecoder> MainWindow::createDecoder() const
{
    switch (m_encodingBox->currentIndex()) {
    case 1:
        return std::make_unique<QStringDecoder>(QStringConverter::Utf8);
    case 2:
        return std::make_unique<QStringDecoder>(QStringConverter::Latin1);
    default:
        return std::make_unique<QStringDecoder>(QStringConverter::System);
    }
}

void MainWindow::sendData()
{
    if (!m_serial->isOpen()) {
        statusBar()->showMessage(QStringLiteral("请先打开串口"), 3000);
        if (m_autoSendCheck->isChecked()) {
            m_autoSendCheck->setChecked(false);
        }
        return;
    }

    const QString rawText = m_sendEdit->toPlainText();
    if (rawText.isEmpty()) {
        return;
    }

    QByteArray payload;
    if (m_hexSendCheck->isChecked()) {
        QString error;
        if (!parseHexString(rawText, &payload, &error)) {
            if (m_autoSendCheck->isChecked()) {
                statusBar()->showMessage(QStringLiteral("HEX 格式错误：%1").arg(error), 5000);
                m_autoSendCheck->setChecked(false);
            } else {
                QMessageBox::warning(this, QStringLiteral("HEX 数据格式错误"), error);
            }
            return;
        }
        if (m_newlineCheck->isChecked()) {
            payload.append("\r\n", 2);
        }
    } else {
        QString text = rawText;
        if (m_newlineCheck->isChecked()) {
            text.append(QStringLiteral("\r\n"));
        }
        payload = encodeText(text);
    }

    if (payload.isEmpty()) {
        return;
    }

    const qint64 written = m_serial->write(payload);
    if (written < 0) {
        statusBar()->showMessage(
            QStringLiteral("发送失败：%1").arg(m_serial->errorString()), 5000);
        return;
    }

    m_txBytes += written;
    updateCounters();
    if (!m_autoSendCheck->isChecked()) {
        statusBar()->showMessage(QStringLiteral("已发送 %1 字节").arg(written), 1500);
    }
}

void MainWindow::clearReceive()
{
    m_receiveEdit->clear();
    m_rxBytes = 0;
    m_atLineStart = true;
    updateCounters();
    statusBar()->showMessage(QStringLiteral("已清空接收区"), 2000);
}

void MainWindow::clearSend()
{
    m_sendEdit->clear();
    if (m_autoSendCheck->isChecked()) {
        m_autoSendCheck->setChecked(false);
    }
}

void MainWindow::saveReceiveData()
{
    const QString text = m_receiveEdit->toPlainText();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("接收区没有数据可保存"), 3000);
        return;
    }

    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested =
        QDir(documents).filePath(QStringLiteral("serial-%1.txt")
                                     .arg(QDateTime::currentDateTime().toString(
                                         QStringLiteral("yyyyMMdd-HHmmss"))));

    const QString fileName =
        QFileDialog::getSaveFileName(this, QStringLiteral("保存接收数据"), suggested,
                                     QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("无法写入文件：\n%1").arg(file.errorString()));
        return;
    }

    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("写入文件时出错：\n%1").arg(file.errorString()));
        return;
    }

    statusBar()->showMessage(QStringLiteral("已保存到 %1").arg(fileName), 4000);
}

// ---------------------------------------------------------------------------
// 状态与设置
// ---------------------------------------------------------------------------
void MainWindow::updateUiState()
{
    const bool open = m_serial->isOpen();
    const bool hasPort = !m_portBox->currentData().toString().isEmpty();

    const QList<QWidget *> configWidgets = {m_baudBox, m_dataBitsBox, m_stopBitsBox,
                                            m_parityBox, m_flowBox};
    for (QWidget *widget : configWidgets) {
        if (widget) {
            widget->setEnabled(!open);
        }
    }
    if (m_portBox) {
        m_portBox->setEnabled(!open && hasPort);
    }
    if (m_refreshButton) {
        m_refreshButton->setEnabled(!open);
    }

    m_dtrCheck->setEnabled(open);
    m_rtsCheck->setEnabled(open);
    m_sendButton->setEnabled(open);
    m_autoSendCheck->setEnabled(open);
    m_autoSendInterval->setEnabled(open && m_autoSendCheck->isChecked());

    m_openButton->setText(open ? QStringLiteral("关闭串口") : QStringLiteral("打开串口"));
    if (m_openButton->property("open").toBool() != open) {
        m_openButton->setProperty("open", open);
        m_openButton->style()->unpolish(m_openButton);
        m_openButton->style()->polish(m_openButton);
    }

    updateStatusLabel();
}

void MainWindow::updateStatusLabel()
{
    if (m_serial->isOpen()) {
        m_stateLabel->setText(
            QStringLiteral("状态：已连接 %1 · %2").arg(m_serial->portName()).arg(m_serial->baudRate()));
    } else {
        m_stateLabel->setText(QStringLiteral("状态：未连接"));
    }
}

void MainWindow::updateCounters()
{
    m_counterLabel->setText(
        QStringLiteral("接收 %1  |  发送 %2").arg(formatByteCount(m_rxBytes),
                                                     formatByteCount(m_txBytes)));
}

void MainWindow::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Serial"));
    m_preferredPort = settings.value(QStringLiteral("port")).toString();
    const QString baud = settings.value(QStringLiteral("baud")).toString();
    const int dataBits = settings.value(QStringLiteral("dataBits"), int(QSerialPort::Data8)).toInt();
    const int stopBits = settings.value(QStringLiteral("stopBits"), int(QSerialPort::OneStop)).toInt();
    const int parity = settings.value(QStringLiteral("parity"), int(QSerialPort::NoParity)).toInt();
    const int flow = settings.value(QStringLiteral("flow"), int(QSerialPort::NoFlowControl)).toInt();
    const int encoding = settings.value(QStringLiteral("encoding"), 0).toInt();
    settings.endGroup();

    if (!baud.isEmpty()) {
        m_baudBox->setCurrentText(baud);
    }

    auto selectByData = [](QComboBox *box, int value, int fallback) {
        int index = box->findData(value);
        if (index < 0) {
            index = box->findData(fallback);
        }
        if (index >= 0) {
            box->setCurrentIndex(index);
        }
    };
    selectByData(m_dataBitsBox, dataBits, int(QSerialPort::Data8));
    selectByData(m_stopBitsBox, stopBits, int(QSerialPort::OneStop));
    selectByData(m_parityBox, parity, int(QSerialPort::NoParity));
    selectByData(m_flowBox, flow, int(QSerialPort::NoFlowControl));

    if (encoding >= 0 && encoding < m_encodingBox->count()) {
        m_encodingBox->setCurrentIndex(encoding);
    }

    settings.beginGroup(QStringLiteral("Display"));
    m_hexDisplayCheck->setChecked(settings.value(QStringLiteral("hexDisplay"), false).toBool());
    m_timestampCheck->setChecked(settings.value(QStringLiteral("timestamp"), false).toBool());
    m_autoScrollCheck->setChecked(settings.value(QStringLiteral("autoScroll"), true).toBool());
    m_hexSendCheck->setChecked(settings.value(QStringLiteral("hexSend"), false).toBool());
    m_newlineCheck->setChecked(settings.value(QStringLiteral("newline"), false).toBool());
    m_autoSendInterval->setValue(settings.value(QStringLiteral("autoSendInterval"),
                                                kAutoSendDefaultMs).toInt());
    settings.endGroup();

    const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QByteArray splitterState = settings.value(QStringLiteral("splitter")).toByteArray();
    if (!splitterState.isEmpty() && m_splitter) {
        m_splitter->restoreState(splitterState);
    }
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Serial"));
    settings.setValue(QStringLiteral("port"), m_portBox->currentData().toString());
    settings.setValue(QStringLiteral("baud"), m_baudBox->currentText());
    settings.setValue(QStringLiteral("dataBits"), m_dataBitsBox->currentData().toInt());
    settings.setValue(QStringLiteral("stopBits"), m_stopBitsBox->currentData().toInt());
    settings.setValue(QStringLiteral("parity"), m_parityBox->currentData().toInt());
    settings.setValue(QStringLiteral("flow"), m_flowBox->currentData().toInt());
    settings.setValue(QStringLiteral("encoding"), m_encodingBox->currentIndex());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Display"));
    settings.setValue(QStringLiteral("hexDisplay"), m_hexDisplayCheck->isChecked());
    settings.setValue(QStringLiteral("timestamp"), m_timestampCheck->isChecked());
    settings.setValue(QStringLiteral("autoScroll"), m_autoScrollCheck->isChecked());
    settings.setValue(QStringLiteral("hexSend"), m_hexSendCheck->isChecked());
    settings.setValue(QStringLiteral("newline"), m_newlineCheck->isChecked());
    settings.setValue(QStringLiteral("autoSendInterval"), m_autoSendInterval->value());
    settings.endGroup();

    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    if (m_splitter) {
        settings.setValue(QStringLiteral("splitter"), m_splitter->saveState());
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    m_autoSendTimer->stop();
    m_portScanTimer->stop();
    if (m_serial->isOpen()) {
        m_serial->close();
    }
    QMainWindow::closeEvent(event);
}
