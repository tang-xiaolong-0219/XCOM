#pragma once

#include "xcom/api/commands.h"
#include "xcom/api/types.h"

class QWidget;

namespace xcom {

enum class ViewLocation { Sidebar, Panel, Editor };

struct ViewSpec {
    QString id;
    QString title;
    QString icon;
    ViewLocation location = ViewLocation::Sidebar;
    QString container;
    int order = 100;
    bool singleton = true;
};

class IViewHost
{
public:
    virtual ~IViewHost() = default;

    virtual QVariantMap viewState() const = 0;
    virtual void setViewState(const QVariantMap &state) = 0;
    virtual CommandResult executeCommand(const QString &commandId,
                                         const QVariantMap &args = QVariantMap()) = 0;
    virtual void showView(const QString &viewId) = 0;
    virtual QString sessionId() const = 0;
};

class IViewFactory
{
public:
    virtual ~IViewFactory() = default;

    // 返回的 QWidget 由宿主接管父子关系；插件需包含 QtWidgets 才能实现。
    virtual QWidget *create(QWidget *parent, IViewHost *host) = 0;
};

class IViewRegistry
{
public:
    virtual ~IViewRegistry() = default;

    virtual ISubscription *registerView(const ViewSpec &spec, IViewFactory *factory) = 0;
    virtual bool show(const QString &viewId) = 0;
    virtual QVector<ViewSpec> views() const = 0;
};

struct MenuContribution {
    QString menu;   // menu/file、menu/session、menu/view、menu/help
    QString group;
    int order = 100;
    QString command;
    QString label;
    QString when;
};

class IMenuRegistry
{
public:
    virtual ~IMenuRegistry() = default;

    virtual ISubscription *addItem(const MenuContribution &contribution) = 0;
    virtual QVector<MenuContribution> items() const = 0;
};

class IUiContext
{
public:
    virtual ~IUiContext() = default;

    virtual IViewRegistry &views() = 0;
    virtual IMenuRegistry &menus() = 0;
    virtual void showView(const QString &viewId) = 0;
    virtual void openSessionConsole(const QString &sessionId) = 0;
    virtual void refreshMenus() = 0;
};

} // namespace xcom
