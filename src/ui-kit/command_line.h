#pragma once

#include "xcom/api/commands.h"
#include "xcom/api/sessions.h"
#include "xcom/api/ui.h"

#include <QStringList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
QT_END_NAMESPACE

namespace xcom {

// 串口命令行：像终端一样一行一条命令，回车即发送。
// 所有发送仍然走 data.send 命令，与按钮、脚本、CLI 保持同一条链路。
class CommandLineView : public QWidget
{
    Q_OBJECT

public:
    CommandLineView(ISession *session, IViewHost *host, QWidget *parent = nullptr);

    void focusInput();
    void appendOutput(const QString &text, bool error = false);
    void clearOutput();

    bool isHexMode() const;
    void setHexMode(bool enabled);
    void setNewline(bool enabled);
    void setLocalEcho(bool enabled);

    QStringList history() const { return m_history; }

    // 供命令终端 / 命令面板复用：发送一行并返回结果。
    CommandResult sendLine(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void submit();
    void historyBack();
    void historyForward();
    void updateHint();

    ISession *m_session = nullptr;
    IViewHost *m_host = nullptr;

    QPlainTextEdit *m_output = nullptr;
    QLineEdit *m_input = nullptr;
    QCheckBox *m_hexCheck = nullptr;
    QCheckBox *m_newlineCheck = nullptr;
    QCheckBox *m_echoCheck = nullptr;
    QLabel *m_promptLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QToolButton *m_clearButton = nullptr;

    QStringList m_history;
    int m_historyIndex = 0;
    int m_historyLimit = 200;
};

} // namespace xcom
