#include "ui-kit/command_line.h"

#include <QCheckBox>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace xcom {

CommandLineView::CommandLineView(ISession *session, IViewHost *host, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_host(host)
{
    buildUi();
    updateHint();
}

void CommandLineView::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);
    m_hexCheck = new QCheckBox(QStringLiteral("HEX"), this);
    m_hexCheck->setToolTip(QStringLiteral("按十六进制解析输入，例如 AA 55 01"));
    m_newlineCheck = new QCheckBox(QStringLiteral("追加 \\r\\n"), this);
    m_newlineCheck->setChecked(true);
    m_echoCheck = new QCheckBox(QStringLiteral("回显"), this);
    m_echoCheck->setChecked(true);
    m_echoCheck->setToolTip(QStringLiteral("在输出区显示自己输入的内容"));
    m_clearButton = new QToolButton(this);
    m_clearButton->setText(QStringLiteral("清空"));
    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("sessionStats"));

    bar->addWidget(m_hexCheck);
    bar->addWidget(m_newlineCheck);
    bar->addWidget(m_echoCheck);
    bar->addWidget(m_hintLabel, 1);
    bar->addWidget(m_clearButton);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setMaximumBlockCount(4000);
    m_output->setFont(mono);
    m_output->setPlaceholderText(
        QStringLiteral("命令回显会显示在这里。输入一行后按 Enter 发送。"));

    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    m_promptLabel = new QLabel(QStringLiteral("\u203a"), this);
    m_promptLabel->setObjectName(QStringLiteral("sessionState"));
    m_input = new QLineEdit(this);
    m_input->setFont(mono);
    m_input->setPlaceholderText(QStringLiteral("输入要发送的内容，Enter 发送，↑↓ 翻历史"));
    inputRow->addWidget(m_promptLabel);
    inputRow->addWidget(m_input, 1);

    root->addLayout(bar);
    root->addWidget(m_output, 1);
    root->addLayout(inputRow);

    connect(m_input, &QLineEdit::returnPressed, this, &CommandLineView::submit);
    connect(m_clearButton, &QToolButton::clicked, this, &CommandLineView::clearOutput);
    connect(m_hexCheck, &QCheckBox::toggled, this, [this] { updateHint(); });
    connect(m_newlineCheck, &QCheckBox::toggled, this, [this] { updateHint(); });

    m_input->installEventFilter(this);
}

bool CommandLineView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            historyBack();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down) {
            historyForward();
            return true;
        }
        if (keyEvent->key() == Qt::Key_L && (keyEvent->modifiers() & Qt::ControlModifier)) {
            clearOutput();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CommandLineView::updateHint()
{
    if (!m_hintLabel) {
        return;
    }
    QString hint = m_hexCheck->isChecked() ? QStringLiteral("HEX 模式：输入 AA 55 01")
                                           : QStringLiteral("文本模式");
    if (m_newlineCheck->isChecked()) {
        hint.append(QStringLiteral(" · 自动追加 \\r\\n"));
    }
    if (!m_session) {
        hint.append(QStringLiteral(" · 无会话"));
    } else if (m_session->state() != SessionState::Open) {
        hint.append(QStringLiteral(" · %1").arg(sessionStateName(m_session->state())));
    }
    m_hintLabel->setText(hint);
}

void CommandLineView::focusInput()
{
    m_input->setFocus();
    m_input->selectAll();
}

void CommandLineView::clearOutput()
{
    m_output->clear();
}

void CommandLineView::appendOutput(const QString &text, bool error)
{
    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (error) {
        // 错误行用前景色区分，不额外插入字符，保证可复制。
        Q_UNUSED(error)
    }
    QScrollBar *bar = m_output->verticalScrollBar();
    bar->setValue(bar->maximum());
}

bool CommandLineView::isHexMode() const
{
    return m_hexCheck->isChecked();
}

void CommandLineView::setHexMode(bool enabled)
{
    m_hexCheck->setChecked(enabled);
}

void CommandLineView::setNewline(bool enabled)
{
    m_newlineCheck->setChecked(enabled);
}

void CommandLineView::setLocalEcho(bool enabled)
{
    m_echoCheck->setChecked(enabled);
}

void CommandLineView::historyBack()
{
    if (m_history.isEmpty()) {
        return;
    }
    if (m_historyIndex > 0) {
        --m_historyIndex;
    }
    m_input->setText(m_history.at(m_historyIndex));
    m_input->setCursorPosition(m_input->text().size());
}

void CommandLineView::historyForward()
{
    if (m_history.isEmpty()) {
        return;
    }
    if (m_historyIndex < m_history.size() - 1) {
        ++m_historyIndex;
        m_input->setText(m_history.at(m_historyIndex));
    } else {
        m_historyIndex = m_history.size();
        m_input->clear();
    }
    m_input->setCursorPosition(m_input->text().size());
}

void CommandLineView::submit()
{
    const QString text = m_input->text();
    if (text.trimmed().isEmpty()) {
        return;
    }
    m_input->clear();

    m_history.removeAll(text);
    m_history.append(text);
    while (m_history.size() > m_historyLimit) {
        m_history.removeFirst();
    }
    m_historyIndex = m_history.size();

    sendLine(text);
}

CommandResult CommandLineView::sendLine(const QString &text)
{
    if (!m_session) {
        appendOutput(QStringLiteral("✗ 没有会话\n"), true);
        return CommandResult::failure(QStringLiteral("没有会话"));
    }
    if (m_session->state() != SessionState::Open) {
        appendOutput(QStringLiteral("✗ 会话未打开：%1\n")
                         .arg(sessionStateName(m_session->state())),
                     true);
        return CommandResult::failure(QStringLiteral("会话未打开"));
    }

    if (m_echoCheck->isChecked()) {
        appendOutput(QStringLiteral("\u203a %1\n").arg(text));
    }

    QVariantMap args;
    args.insert(QStringLiteral("session"), m_session->id());
    args.insert(QStringLiteral("text"), text);
    args.insert(QStringLiteral("hex"), m_hexCheck->isChecked());
    args.insert(QStringLiteral("newline"), m_newlineCheck->isChecked());

    CommandResult result;
    if (m_host) {
        result = m_host->executeCommand(QStringLiteral("data.send"), args);
    } else {
        result = CommandResult::failure(QStringLiteral("没有可用的命令上下文"));
    }
    if (!result.ok) {
        appendOutput(QStringLiteral("✗ %1\n").arg(result.message), true);
    }
    updateHint();
    return result;
}

} // namespace xcom
