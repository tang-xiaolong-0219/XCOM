#include "services/command_registry.h"

#include "services/subscription.h"

#include <QDebug>

#include <exception>

namespace xcom {
namespace {

class NullProgress : public ICommandProgress
{
public:
    void setProgress(int percent, const QString &message) override
    {
        Q_UNUSED(percent)
        Q_UNUSED(message)
    }
    bool isCanceled() const override { return false; }
};

} // namespace

CommandRegistry::CommandRegistry(QObject *parent)
    : QObject(parent)
{
}

ISubscription *CommandRegistry::registerCommand(const CommandSpec &spec, ICommandHandler *handler)
{
    if (spec.id.isEmpty() || !handler) {
        qWarning() << "command registration rejected: empty id or handler";
        return nullptr;
    }
    if (contains(spec.id)) {
        qWarning() << "duplicate command id:" << spec.id;
        return nullptr;
    }

    Entry entry;
    entry.spec = spec;
    entry.handler = handler;
    entry.subscription = new CallbackSubscription([this, id = spec.id] { removeById(id); }, this);
    m_entries.append(entry);
    return entry.subscription;
}

bool CommandRegistry::execute(const CommandInvocation &invocation, CommandResult *result,
                              ICommandProgress *progress)
{
    NullProgress nullProgress;
    ICommandProgress *effectiveProgress = progress ? progress : &nullProgress;

    const int index = indexOf(invocation.id);
    if (index < 0) {
        if (result) {
            *result = CommandResult::failure(QStringLiteral("命令不存在：%1").arg(invocation.id));
        }
        return false;
    }

    CommandResult commandResult;
    try {
        commandResult = m_entries[index].handler->execute(invocation, effectiveProgress);
    } catch (const std::exception &exception) {
        commandResult = CommandResult::failure(
            QStringLiteral("命令执行异常：%1").arg(QString::fromUtf8(exception.what())));
    } catch (...) {
        commandResult = CommandResult::failure(QStringLiteral("命令执行发生未知异常"));
    }

    if (result) {
        *result = commandResult;
    }
    return commandResult.ok;
}

bool CommandRegistry::contains(const QString &commandId) const
{
    return indexOf(commandId) >= 0;
}

QVector<CommandSpec> CommandRegistry::commands() const
{
    QVector<CommandSpec> result;
    result.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        result.append(entry.spec);
    }
    return result;
}

int CommandRegistry::indexOf(const QString &commandId) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).spec.id == commandId) {
            return i;
        }
    }
    return -1;
}

void CommandRegistry::removeById(const QString &commandId)
{
    const int index = indexOf(commandId);
    if (index < 0) {
        return;
    }
    m_entries.remove(index);
}

} // namespace xcom
