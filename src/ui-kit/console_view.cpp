#include "ui-kit/console_view.h"

#include "xcom/api/commands.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace xcom {
namespace {

class SendInputEdit final : public QPlainTextEdit
{
public:
    using QPlainTextEdit::QPlainTextEdit;

    void reserveClearButtonArea()
    {
        setViewportMargins(0, 0, 28, 26);
    }
};

QString formatBytes(quint64 bytes)
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

ConsoleView::ConsoleView(ISession *session, IViewHost *host, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_host(host)
{
    buildUi();

    if (m_session) {
        const QString encoding = m_session->encoding();
        QStringConverter::Encoding converterEncoding = QStringConverter::System;
        if (encoding == QStringLiteral("utf8")) {
            converterEncoding = QStringConverter::Utf8;
        } else if (encoding == QStringLiteral("latin1")) {
            converterEncoding = QStringConverter::Latin1;
        }
        m_decoder = std::make_unique<QStringDecoder>(converterEncoding);
        m_subscription = m_session->subscribe(this);
    }

    updateTitle();
    updateState();
    refreshPortOptions();
    updateStats();
}

ConsoleView::~ConsoleView()
{
    m_timedTimer->stop();
    m_fileSendTimer->stop();
    m_portRefreshTimer->stop();
    if (m_sendFile) m_sendFile->close();
    m_flushTimer->stop();
    if (m_subscription) {
        m_subscription->unsubscribe();
        m_subscription = nullptr;
    }
}

void ConsoleView::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    auto *topBar = new QHBoxLayout;
    topBar->setSpacing(8);

    m_stateButton = new QPushButton(this);
    m_stateButton->setObjectName(QStringLiteral("sessionState"));
    m_stateButton->setFlat(true);
    m_stateButton->setFocusPolicy(Qt::NoFocus);
    m_stateButton->setCursor(Qt::PointingHandCursor);
    m_stateButton->setToolTip(QStringLiteral("打开或关闭当前会话"));
    m_portCombo = new QComboBox(this);
    m_portCombo->setObjectName(QStringLiteral("consolePort"));
    m_portCombo->setFixedWidth(142);
    m_portCombo->setToolTip(QStringLiteral("切换当前会话使用的串口"));
    m_baudCombo = new QComboBox(this);
    m_baudCombo->setObjectName(QStringLiteral("consoleBaudRate"));
    m_baudCombo->setEditable(true);
    m_baudCombo->setFixedWidth(104);
    for (int rate : {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200,
                     230400, 460800, 921600}) {
        m_baudCombo->addItem(QString::number(rate), rate);
    }
    m_baudCombo->setToolTip(QStringLiteral("选择波特率；选择后立即应用，或输入后按 Enter"));
    m_statsLabel = new QLabel(this);
    m_statsLabel->setObjectName(QStringLiteral("sessionStats"));

    m_showSendCheck = new QCheckBox(QStringLiteral("显示发送"), this);
    m_showSendCheck->setToolTip(QStringLiteral("在接收区回显已发送的数据"));
    m_hexDisplayCheck = new QCheckBox(QStringLiteral("HEX 接收"), this);
    m_hexDisplayCheck->setToolTip(QStringLiteral("以十六进制显示接收数据"));
    m_timestampCheck = new QCheckBox(QStringLiteral("时间戳"), this);
    m_autoScrollCheck = new QCheckBox(QStringLiteral("自动滚动"), this);
    m_autoScrollCheck->setChecked(true);
    m_pauseCheck = new QCheckBox(QStringLiteral("暂停显示"), this);

    auto *clearButton = new QPushButton(QStringLiteral("清空"), this);
    auto *saveButton = new QPushButton(QStringLiteral("保存"), this);

    topBar->addWidget(m_stateButton);
    topBar->addWidget(m_portCombo);
    topBar->addWidget(m_baudCombo);
    topBar->addWidget(m_statsLabel);
    topBar->addStretch();
    topBar->addWidget(m_showSendCheck);
    topBar->addWidget(m_hexDisplayCheck);
    topBar->addWidget(m_timestampCheck);
    topBar->addWidget(m_autoScrollCheck);
    topBar->addWidget(m_pauseCheck);
    topBar->addWidget(clearButton);
    topBar->addWidget(saveButton);

    m_receive = new QPlainTextEdit(this);
    m_receive->setReadOnly(true);
    m_receive->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_receive->setMaximumBlockCount(10000);
    m_receive->setPlaceholderText(QStringLiteral("打开会话后，接收到的数据会显示在这里…"));
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_receive->setFont(mono);

    auto *sendPanel = new QWidget(this);
    auto *sendLayout = new QVBoxLayout(sendPanel);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->setSpacing(4);

    auto *sendBar = new QHBoxLayout;
    sendBar->setSpacing(8);
    m_hexSendCheck = new QCheckBox(QStringLiteral("HEX 发送"), sendPanel);
    m_newlineCheck = new QCheckBox(QStringLiteral("新行 \\r\\n"), sendPanel);
    m_timedCheck = new QCheckBox(QStringLiteral("定时发送"), sendPanel);
    m_intervalSpin = new QSpinBox(sendPanel);
    m_intervalSpin->setRange(10, 3600000);
    m_intervalSpin->setValue(1000);
    m_intervalSpin->setSuffix(QStringLiteral(" ms"));
    m_intervalSpin->setMinimumWidth(96);
    m_intervalSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_sendFileButton = new QPushButton(QStringLiteral("发送文件"), sendPanel);
    m_sendFileButton->setObjectName(QStringLiteral("sendFileButton"));
    m_sendFileButton->setToolTip(QStringLiteral("选择文件并按原始字节发送；发送中可点击停止"));
    m_hintLabel = new QLabel(sendPanel);
    m_dtrCheck = new QCheckBox(QStringLiteral("DTR"), sendPanel);
    m_dtrCheck->setObjectName(QStringLiteral("dtrToggle"));
    m_dtrCheck->setToolTip(QStringLiteral("控制数据终端就绪信号"));
    m_rtsCheck = new QCheckBox(QStringLiteral("RTS"), sendPanel);
    m_rtsCheck->setObjectName(QStringLiteral("rtsToggle"));
    m_rtsCheck->setToolTip(QStringLiteral("控制请求发送信号"));
    m_sendButton = new QPushButton(QStringLiteral("发送"), sendPanel);
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);

    sendBar->addWidget(m_hexSendCheck);
    sendBar->addWidget(m_newlineCheck);
    sendBar->addWidget(m_timedCheck);
    sendBar->addWidget(m_intervalSpin);
    sendBar->addWidget(m_sendFileButton);
    sendBar->addWidget(m_hintLabel, 1);
    sendBar->addWidget(m_dtrCheck);
    sendBar->addWidget(m_rtsCheck);
    sendBar->addWidget(m_sendButton);

    auto *sendInput = new QWidget(sendPanel);
    auto *sendInputLayout = new QGridLayout(sendInput);
    sendInputLayout->setContentsMargins(0, 0, 0, 0);
    sendInputLayout->setSpacing(0);
    auto *sendEdit = new SendInputEdit(sendInput);
    sendEdit->reserveClearButtonArea();
    m_send = sendEdit;
    m_send->setFont(mono);
    m_send->setPlaceholderText(QStringLiteral("输入要发送的数据（Ctrl+Enter 快速发送）"));
    m_send->setMinimumHeight(80);
    auto *clearSendButton = new QToolButton(sendInput);
    clearSendButton->setObjectName(QStringLiteral("clearSendButton"));
    clearSendButton->setIcon(sendPanel->style()->standardIcon(QStyle::SP_TrashIcon));
    clearSendButton->setIconSize(QSize(16, 16));
    clearSendButton->setFixedSize(24, 24);
    clearSendButton->setAutoRaise(true);
    clearSendButton->setCursor(Qt::PointingHandCursor);
    clearSendButton->setToolTip(QStringLiteral("清空输入"));
    auto *clearOpacity = new QGraphicsOpacityEffect(clearSendButton);
    clearOpacity->setOpacity(0.55);
    clearSendButton->setGraphicsEffect(clearOpacity);
    sendInputLayout->addWidget(m_send, 0, 0);
    sendInputLayout->addWidget(clearSendButton, 0, 0, Qt::AlignRight | Qt::AlignBottom);

    sendLayout->addLayout(sendBar);
    sendLayout->addWidget(sendInput, 1);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_receive);
    splitter->addWidget(sendPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({420, 160});

    root->addLayout(topBar);
    root->addWidget(splitter, 1);

    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(30);
    connect(m_flushTimer, &QTimer::timeout, this, &ConsoleView::flushPending);

    m_timedTimer = new QTimer(this);
    connect(m_timedTimer, &QTimer::timeout, this, &ConsoleView::sendNow);

    m_fileSendTimer = new QTimer(this);
    m_fileSendTimer->setSingleShot(true);
    connect(m_fileSendTimer, &QTimer::timeout, this, &ConsoleView::sendNextFileChunk);

    m_portRefreshTimer = new QTimer(this);
    m_portRefreshTimer->setInterval(1500);
    connect(m_portRefreshTimer, &QTimer::timeout, this, &ConsoleView::refreshPortOptions);
    m_portRefreshTimer->start();

    connect(clearButton, &QPushButton::clicked, this, &ConsoleView::clearDisplay);
    connect(saveButton, &QPushButton::clicked, this, &ConsoleView::saveToFile);
    connect(m_stateButton, &QPushButton::clicked, this, [this] {
        if (!m_session) return;
        const bool connected = m_session->state() != SessionState::Open;
        const CommandResult result = execute(
            QStringLiteral("serial.setConnected"),
            {{QStringLiteral("session"), m_session->id()},
             {QStringLiteral("connected"), connected}});
        if (!result.ok) showHint(result.message, true);
    });
    connect(m_portCombo, qOverload<int>(&QComboBox::activated), this, [this](int) {
        if (!m_session) return;
        const QString port = m_portCombo->currentData().toString();
        const CommandResult result = execute(
            QStringLiteral("serial.setPort"),
            {{QStringLiteral("session"), m_session->id()},
             {QStringLiteral("port"), port}});
        if (!result.ok) {
            showHint(result.message, true);
            refreshPortOptions();
        } else {
            updateState();
        }
    });
    auto applyBaudRate = [this] {
        if (!m_session) return;
        const int baudRate = m_baudCombo->currentText().trimmed().toInt();
        const CommandResult result = execute(
            QStringLiteral("serial.setBaudRate"),
            {{QStringLiteral("session"), m_session->id()},
             {QStringLiteral("baud"), baudRate}});
        if (!result.ok) showHint(result.message, true);
        updateState();
    };
    connect(m_baudCombo, qOverload<int>(&QComboBox::activated), this,
            [applyBaudRate](int) { applyBaudRate(); });
    connect(m_baudCombo->lineEdit(), &QLineEdit::returnPressed, this, applyBaudRate);
    connect(m_sendButton, &QPushButton::clicked, this, &ConsoleView::sendNow);
    connect(m_sendFileButton, &QPushButton::clicked, this, [this] {
        if (m_sendFile) stopFileSend(QStringLiteral("已停止文件发送"));
        else sendFile();
    });
    connect(clearSendButton, &QPushButton::clicked, m_send, &QPlainTextEdit::clear);
    connect(m_dtrCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!m_session) return;
        const CommandResult result = execute(
            QStringLiteral("serial.setDtr"),
            {{QStringLiteral("session"), m_session->id()},
             {QStringLiteral("enabled"), enabled}});
        if (!result.ok) {
            const QSignalBlocker blocker(m_dtrCheck);
            m_dtrCheck->setChecked(m_session->config().dtr);
            showHint(result.message, true);
        }
    });
    connect(m_rtsCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!m_session) return;
        const CommandResult result = execute(
            QStringLiteral("serial.setRts"),
            {{QStringLiteral("session"), m_session->id()},
             {QStringLiteral("enabled"), enabled}});
        if (!result.ok) {
            const QSignalBlocker blocker(m_rtsCheck);
            m_rtsCheck->setChecked(m_session->config().rts);
            showHint(result.message, true);
        }
    });
    connect(m_timedCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            if (!m_session || m_session->state() != SessionState::Open) {
                showHint(QStringLiteral("会话未打开，无法定时发送"), true);
                m_timedCheck->setChecked(false);
                return;
            }
            m_timedTimer->start(m_intervalSpin->value());
        } else {
            m_timedTimer->stop();
        }
    });
    connect(m_intervalSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_timedTimer->isActive()) {
            m_timedTimer->setInterval(value);
        }
    });
    connect(m_send, &QPlainTextEdit::textChanged, this, [this] { updateSendEnabled(); });

    // 占位提示承诺了 Ctrl+Enter 快速发送，这里实现它。
    m_send->installEventFilter(this);
}

bool ConsoleView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_send && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool isEnter = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
        if (isEnter && (keyEvent->modifiers() & Qt::ControlModifier)) {
            sendNow();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString ConsoleView::sessionId() const
{
    return m_session ? m_session->id() : QString();
}

void ConsoleView::onData(const DataChunk &chunk)
{
    if (!m_session || (chunk.direction == Direction::Tx && !m_showSendCheck->isChecked())) {
        return;
    }
    if (m_pauseCheck->isChecked()) {
        return;
    }
    m_pending.append(chunk);
    scheduleFlush();
}

void ConsoleView::onStateChanged(SessionState state, const Error &reason)
{
    Q_UNUSED(state)
    Q_UNUSED(reason)
    updateState();
}

void ConsoleView::onError(const Error &error)
{
    showHint(error.message, true);
}

void ConsoleView::scheduleFlush()
{
    if (!m_flushTimer->isActive()) {
        m_flushTimer->start();
    }
}

void ConsoleView::flushPending()
{
    if (m_pending.isEmpty()) {
        return;
    }
    const QVector<DataChunk> chunks = m_pending;
    m_pending.clear();

    QTextCursor cursor(m_receive->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat receiveFormat;
    receiveFormat.setForeground(m_receive->palette().color(QPalette::Text));
    QTextCharFormat transmitFormat = receiveFormat;
    transmitFormat.setForeground(QColor(QStringLiteral("#4FC1FF")));
    for (const DataChunk &chunk : chunks) {
        const QString text = formatChunk(chunk);
        if (!text.isEmpty()) {
            cursor.insertText(text, chunk.direction == Direction::Tx
                                        ? transmitFormat : receiveFormat);
        }
    }
    if (m_autoScrollCheck->isChecked() && !m_pauseCheck->isChecked()) {
        QScrollBar *bar = m_receive->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
    updateStats();
}

QString ConsoleView::formatChunk(const DataChunk &chunk)
{
    QString body;
    if (m_hexDisplayCheck->isChecked()) {
        body = QString::fromLatin1(chunk.bytes.toHex(' ')).toUpper();
        body.append(QLatin1Char('\n'));
    } else if (chunk.direction == Direction::Tx) {
        body = decodeTransmit(chunk.bytes);
    } else {
        body = decode(chunk.bytes);
    }

    if (body.isEmpty() && !m_hexDisplayCheck->isChecked()) {
        return {};
    }

    QString result;
    bool atLineStart = m_atLineStart;
    if (!atLineStart) {
        result.append(QLatin1Char('\n'));
        atLineStart = true;
    }
    if (m_timestampCheck->isChecked()) {
        if (!atLineStart) {
            result.append(QLatin1Char('\n'));
        }
        result.append(QStringLiteral("[%1] ")
                          .arg(QDateTime::fromMSecsSinceEpoch(chunk.wallClockMs)
                                   .toString(QStringLiteral("HH:mm:ss.zzz"))));
    }
    result.append(body);
    if (!result.endsWith(QLatin1Char('\n'))) {
        result.append(QLatin1Char('\n'));
    }
    m_atLineStart = result.endsWith(QLatin1Char('\n'));
    return result;
}

QString ConsoleView::decode(const QByteArray &bytes)
{
    if (!m_decoder || !m_decoder->isValid()) {
        return QString::fromLatin1(bytes);
    }
    const QString text = m_decoder->decode(QByteArrayView(bytes));
    if (m_decoder->hasError()) {
        m_decoder->resetState();
        return QString::fromLatin1(bytes);
    }
    return text;
}

QString ConsoleView::decodeTransmit(const QByteArray &bytes) const
{
    QStringConverter::Encoding converterEncoding = QStringConverter::System;
    if (m_session && m_session->encoding() == QStringLiteral("utf8")) {
        converterEncoding = QStringConverter::Utf8;
    } else if (m_session && m_session->encoding() == QStringLiteral("latin1")) {
        converterEncoding = QStringConverter::Latin1;
    }
    QStringDecoder decoder(converterEncoding);
    const QString text = decoder.decode(QByteArrayView(bytes));
    return decoder.hasError() ? QString::fromLatin1(bytes) : text;
}

void ConsoleView::clearDisplay()
{
    m_receive->clear();
    m_pending.clear();
    m_atLineStart = true;
    if (m_decoder) {
        m_decoder->resetState();
    }
}

void ConsoleView::toggleHexDisplay()
{
    m_hexDisplayCheck->toggle();
}

void ConsoleView::toggleTimestamp()
{
    m_timestampCheck->toggle();
}

bool ConsoleView::isHexDisplay() const
{
    return m_hexDisplayCheck->isChecked();
}

bool ConsoleView::isTimestampEnabled() const
{
    return m_timestampCheck->isChecked();
}

void ConsoleView::saveToFile()
{
    const QString text = m_receive->toPlainText();
    if (text.isEmpty()) {
        showHint(QStringLiteral("接收区没有数据"), true);
        return;
    }
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested =
        QDir(documents).filePath(QStringLiteral("%1-%2.txt")
                                     .arg(m_session ? m_session->name() : QStringLiteral("serial"),
                                          QDateTime::currentDateTime().toString(
                                              QStringLiteral("yyyyMMdd-HHmmss"))));
    const QString fileName =
        QFileDialog::getSaveFileName(this, QStringLiteral("保存接收数据"), suggested,
                                     QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        showHint(QStringLiteral("无法写入文件"), true);
        return;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        showHint(QStringLiteral("写入文件失败"), true);
        return;
    }
    showHint(QStringLiteral("已保存到 %1").arg(fileName));
}

void ConsoleView::focusSend()
{
    m_send->setFocus();
}

void ConsoleView::sendNow()
{
    if (m_sendFile) return;
    const QString text = m_send->toPlainText();
    if (text.isEmpty() || !m_session) {
        return;
    }
    QVariantMap args;
    args.insert(QStringLiteral("session"), m_session->id());
    args.insert(QStringLiteral("text"), text);
    args.insert(QStringLiteral("hex"), m_hexSendCheck->isChecked());
    args.insert(QStringLiteral("newline"), m_newlineCheck->isChecked());
    const CommandResult result = execute(QStringLiteral("data.send"), args);
    if (!result.ok) {
        showHint(result.message, true);
    } else {
        QByteArray payload;
        if (m_hexSendCheck->isChecked()) {
            QString digits;
            for (const QChar ch : text) {
                if (!ch.isSpace() && ch != QLatin1Char(',') && ch != QLatin1Char('-')) {
                    digits.append(ch);
                }
            }
            payload = QByteArray::fromHex(digits.toLatin1());
        } else {
            payload = text.toUtf8();
        }
        if (m_newlineCheck->isChecked()) {
            payload.append("\r\n", 2);
        }
        emit dataSent(payload);
    }
}

void ConsoleView::sendFile()
{
    if (!m_session || m_session->state() != SessionState::Open) {
        showHint(QStringLiteral("会话未打开，无法发送文件"), true);
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(this, QStringLiteral("选择要发送的文件"));
    if (fileName.isEmpty()) return;

    auto *file = new QFile(fileName, this);
    if (!file->open(QIODevice::ReadOnly)) {
        showHint(QStringLiteral("无法打开文件：%1").arg(file->errorString()), true);
        file->deleteLater();
        return;
    }
    if (file->size() <= 0) {
        file->close();
        file->deleteLater();
        showHint(QStringLiteral("文件为空，未发送"), true);
        return;
    }

    m_sendFile = file;
    m_fileBytesSent = 0;
    m_fileBytesTotal = file->size();
    m_timedCheck->setChecked(false);
    m_sendFileButton->setText(QStringLiteral("停止发送"));
    updateSendEnabled();
    m_fileSendTimer->start(0);
}

void ConsoleView::sendNextFileChunk()
{
    if (!m_sendFile) return;
    if (!m_session || m_session->state() != SessionState::Open) {
        stopFileSend(QStringLiteral("会话已断开，文件发送已停止"), true);
        return;
    }

    constexpr qint64 chunkSize = 1024;
    const QByteArray bytes = m_sendFile->read(chunkSize);
    if (bytes.isEmpty()) {
        if (m_sendFile->atEnd()) {
            stopFileSend(QStringLiteral("文件发送完成，共 %1 字节").arg(m_fileBytesSent));
        } else {
            stopFileSend(QStringLiteral("读取文件失败：%1").arg(m_sendFile->errorString()), true);
        }
        return;
    }

    Error error;
    if (!m_session->send(bytes, &error)) {
        stopFileSend(error.message, true);
        return;
    }
    m_fileBytesSent += bytes.size();
    m_hintLabel->setStyleSheet(QStringLiteral("color:#89d185;"));
    m_hintLabel->setText(QStringLiteral("发送文件 %1 / %2 字节")
                             .arg(m_fileBytesSent)
                             .arg(m_fileBytesTotal));

    const PortConfig config = m_session->config();
    const int baudRate = qMax(1, config.baudRate);
    const int bitsPerCharacter = 1 + config.dataBits + (config.parity == 0 ? 0 : 1)
                                 + qMax(1, config.stopBits);
    const qint64 bits = static_cast<qint64>(bytes.size()) * bitsPerCharacter;
    const int delayMs = qMax(1, static_cast<int>((bits * 1000 + baudRate - 1) / baudRate));
    m_fileSendTimer->start(delayMs);
}

void ConsoleView::stopFileSend(const QString &message, bool error)
{
    m_fileSendTimer->stop();
    if (m_sendFile) {
        m_sendFile->close();
        m_sendFile->deleteLater();
        m_sendFile = nullptr;
    }
    m_fileBytesSent = 0;
    m_fileBytesTotal = 0;
    m_sendFileButton->setText(QStringLiteral("发送文件"));
    updateSendEnabled();
    if (!message.isEmpty()) showHint(message, error);
}

void ConsoleView::importSendText()
{
    emit importCommandsRequested(m_send->toPlainText());
}

CommandResult ConsoleView::execute(const QString &commandId, const QVariantMap &args)
{
    if (m_host) {
        return m_host->executeCommand(commandId, args);
    }
    return CommandResult::failure(QStringLiteral("没有可用的命令上下文"));
}

void ConsoleView::updateState()
{
    if (!m_session) {
        m_stateButton->setText(QStringLiteral("无会话"));
        m_stateButton->setEnabled(false);
        m_portCombo->setEnabled(false);
        m_baudCombo->setEnabled(false);
        updateSendEnabled();
        return;
    }
    const SessionState state = m_session->state();
    m_stateButton->setText(QStringLiteral("%1 %2")
                               .arg(sessionStateIcon(state), sessionStateName(state)));
    m_stateButton->setEnabled(state != SessionState::Opening && state != SessionState::Closing);
    m_portCombo->setEnabled(state != SessionState::Opening && state != SessionState::Closing);
    m_stateButton->setToolTip(QStringLiteral("%1 · %2 · %3 8N1\n点击打开或关闭当前会话")
                                 .arg(m_session->name(),
                                      m_session->transport() == QStringLiteral("loopback")
                                          ? QStringLiteral("回环")
                                          : m_session->config().portName)
                                 .arg(m_session->config().baudRate));
    const PortConfig config = m_session->config();
    m_baudCombo->setEnabled(state != SessionState::Opening && state != SessionState::Closing);
    const QSignalBlocker baudBlocker(m_baudCombo);
    const int baudIndex = m_baudCombo->findData(config.baudRate);
    if (baudIndex >= 0) m_baudCombo->setCurrentIndex(baudIndex);
    else m_baudCombo->setCurrentText(QString::number(config.baudRate));
    const QSignalBlocker dtrBlocker(m_dtrCheck);
    const QSignalBlocker rtsBlocker(m_rtsCheck);
    m_dtrCheck->setChecked(config.dtr);
    m_rtsCheck->setChecked(config.rts);
    refreshPortOptions();
    updateSendEnabled();
}

void ConsoleView::updateStats()
{
    if (!m_session) {
        return;
    }
    const SessionStats stats = m_session->stats();
    m_statsLabel->setText(QStringLiteral("RX %1 · TX %2")
                              .arg(formatBytes(stats.rxBytes), formatBytes(stats.txBytes)));
}

void ConsoleView::updateSendEnabled()
{
    const bool open = m_session && m_session->state() == SessionState::Open;
    if (!open && m_sendFile) {
        stopFileSend(QStringLiteral("会话已关闭，文件发送已停止"), true);
    }
    const bool fileSending = m_sendFile != nullptr;
    m_sendButton->setEnabled(open && !fileSending);
    m_sendFileButton->setEnabled(open || fileSending);
    m_sendFileButton->setText(fileSending ? QStringLiteral("停止发送")
                                          : QStringLiteral("发送文件"));
    const bool canSetLines = open && !fileSending
                             && m_session->transport() != QStringLiteral("loopback");
    m_dtrCheck->setEnabled(canSetLines);
    m_rtsCheck->setEnabled(canSetLines);
    if (!open && m_timedCheck->isChecked()) {
        m_timedCheck->setChecked(false);
    }
}

void ConsoleView::updateTitle()
{
    if (m_session) {
        setWindowTitle(m_session->name());
    }
}

void ConsoleView::refreshPortOptions()
{
    if (!m_session || !m_portCombo || m_portCombo->view()->isVisible()) return;

    const CommandResult result = execute(
        QStringLiteral("serial.listPorts"),
        {{QStringLiteral("session"), m_session->id()}});
    if (!result.ok) return;

    const QString selected = m_session->transport() == QStringLiteral("loopback")
                                 ? QStringLiteral("LOOPBACK")
                                 : m_session->config().portName;
    const QSignalBlocker blocker(m_portCombo);
    m_portCombo->clear();
    for (const QVariant &entry : result.data.value(QStringLiteral("ports")).toList()) {
        const QVariantMap port = entry.toMap();
        m_portCombo->addItem(port.value(QStringLiteral("label")).toString(),
                             port.value(QStringLiteral("name")).toString());
    }
    if (!selected.isEmpty() && m_portCombo->findData(selected) < 0) {
        m_portCombo->addItem(QStringLiteral("%1 (当前会话)").arg(selected), selected);
    }
    m_portCombo->addItem(QStringLiteral("回环测试"), QStringLiteral("LOOPBACK"));
    const int selectedIndex = m_portCombo->findData(selected);
    if (selectedIndex >= 0) m_portCombo->setCurrentIndex(selectedIndex);
}

void ConsoleView::showHint(const QString &text, bool error)
{
    m_hintLabel->setText(text);
    m_hintLabel->setStyleSheet(error ? QStringLiteral("color:#f48771;")
                                     : QStringLiteral("color:#89d185;"));
    QTimer::singleShot(4000, m_hintLabel, [this] { m_hintLabel->clear(); });
}

} // namespace xcom
