#pragma once

#include "xcom/api/commands.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPlainTextEdit;
QT_END_NAMESPACE

namespace xcom {

class CommandRegistry;

class CommandTerminal : public QWidget
{
    Q_OBJECT

public:
    explicit CommandTerminal(CommandRegistry *commands, QWidget *parent = nullptr);

    void appendOutput(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void submit();
    void print(const QString &text, bool error = false);
    void printHelp(const QString &commandId);
    void printCommandList();
    QStringList tokenize(const QString &line, bool *ok) const;
    QVariant coerce(const QString &value, const CommandArg &arg) const;

    CommandRegistry *m_commands = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QLineEdit *m_input = nullptr;
    QStringList m_history;
    int m_historyIndex = 0;
};

} // namespace xcom
