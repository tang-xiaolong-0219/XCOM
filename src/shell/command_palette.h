#pragma once

#include "xcom/api/commands.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
QT_END_NAMESPACE

namespace xcom {

class CommandRegistry;

class CommandPalette : public QDialog
{
    Q_OBJECT

public:
    explicit CommandPalette(CommandRegistry *commands, QWidget *parent = nullptr);

    void showPalette();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild(const QString &filter);
    void executeCurrent();
    void executeItem(QListWidgetItem *item);

    CommandRegistry *m_commands = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;
    QVector<CommandSpec> m_specs;
};

} // namespace xcom
