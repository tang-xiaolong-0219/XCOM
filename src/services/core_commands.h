#pragma once

#include "xcom/api/commands.h"
#include "xcom/api/sessions.h"

#include <QByteArray>
#include <QString>

namespace xcom {

class CommandRegistry;

// 十六进制解析：接受空格、逗号、分号、短横线、下划线分隔，以及 0x 前缀。
bool parseHexBytes(const QString &text, QByteArray *out, QString *error);

// 与界面无关的核心命令实现（CLI 与图形界面共用同一份行为）。
// 调用方持有实例，并负责在销毁前撤销注册。
class DataSendHandler : public ICommandHandler
{
public:
    explicit DataSendHandler(ISessionService *sessions)
        : m_sessions(sessions)
    {
    }

    CommandResult execute(const CommandInvocation &invocation,
                          ICommandProgress *progress) override;

    static CommandSpec spec();

private:
    ISessionService *m_sessions = nullptr;
};

class SessionListHandler : public ICommandHandler
{
public:
    explicit SessionListHandler(ISessionService *sessions)
        : m_sessions(sessions)
    {
    }

    CommandResult execute(const CommandInvocation &invocation,
                          ICommandProgress *progress) override;

    static CommandSpec spec();

private:
    ISessionService *m_sessions = nullptr;
};

} // namespace xcom
