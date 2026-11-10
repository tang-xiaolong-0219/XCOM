#pragma once

#include "xcom/api/commands.h"

#include <QObject>
#include <QVector>

namespace xcom {

class CallbackSubscription;

class CommandRegistry : public QObject, public ICommandRegistry
{
    Q_OBJECT

public:
    explicit CommandRegistry(QObject *parent = nullptr);

    ISubscription *registerCommand(const CommandSpec &spec, ICommandHandler *handler) override;
    bool execute(const CommandInvocation &invocation, CommandResult *result = nullptr,
                 ICommandProgress *progress = nullptr) override;
    bool contains(const QString &commandId) const override;
    QVector<CommandSpec> commands() const override;

private:
    struct Entry {
        CommandSpec spec;
        ICommandHandler *handler = nullptr;
        CallbackSubscription *subscription = nullptr;
    };

    int indexOf(const QString &commandId) const;
    void removeById(const QString &commandId);
    QVector<Entry> m_entries;
};

} // namespace xcom
