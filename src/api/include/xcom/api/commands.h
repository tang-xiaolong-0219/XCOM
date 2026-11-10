#pragma once

#include "xcom/api/types.h"

#include <QVariant>
#include <QVector>

namespace xcom {

enum class CommandArgType { String, Int, Bool, Enum, SessionId, Hex, Path };

struct CommandArg {
    QString name;
    CommandArgType type = CommandArgType::String;
    bool required = false;
    QVariant defaultValue;
    QString description;
    QStringList choices;

    CommandArg() = default;
    // 提供构造函数（非聚合），允许 `{name, type, required, default, desc}` 这样的简写而
    // 不触发 -Wmissing-field-initializers。
    CommandArg(QString argName, CommandArgType argType, bool isRequired = false,
               QVariant defaultVal = QVariant(), QString argDescription = QString(),
               QStringList argChoices = QStringList())
        : name(std::move(argName))
        , type(argType)
        , required(isRequired)
        , defaultValue(std::move(defaultVal))
        , description(std::move(argDescription))
        , choices(std::move(argChoices))
    {
    }
};

struct CommandSpec {
    QString id;
    QString title;
    QString category;
    QString icon;
    QVector<CommandArg> args;
    QString enablement;
    bool hidden = false;
};

struct CommandInvocation {
    QString id;
    QVariantMap args;
    QString source = QStringLiteral("ui");
};

struct CommandResult {
    bool ok = false;
    QString message;
    QVariantMap data;

    static CommandResult success(const QString &message = QString(), const QVariantMap &data = {})
    {
        CommandResult result;
        result.ok = true;
        result.message = message;
        result.data = data;
        return result;
    }

    static CommandResult failure(const QString &message)
    {
        CommandResult result;
        result.ok = false;
        result.message = message;
        return result;
    }
};

class ICommandProgress
{
public:
    virtual ~ICommandProgress() = default;
    virtual void setProgress(int percent, const QString &message) = 0;
    virtual bool isCanceled() const = 0;
};

class ICommandHandler
{
public:
    virtual ~ICommandHandler() = default;
    virtual CommandResult execute(const CommandInvocation &invocation, ICommandProgress *progress) = 0;
};

class ICommandRegistry
{
public:
    virtual ~ICommandRegistry() = default;

    virtual ISubscription *registerCommand(const CommandSpec &spec, ICommandHandler *handler) = 0;
    virtual bool execute(const CommandInvocation &invocation, CommandResult *result = nullptr,
                         ICommandProgress *progress = nullptr) = 0;
    virtual bool contains(const QString &commandId) const = 0;
    virtual QVector<CommandSpec> commands() const = 0;
};

} // namespace xcom
