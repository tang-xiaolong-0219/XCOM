#pragma once

#include "xcom/api/commands.h"
#include "xcom/api/ui.h"

#include <QObject>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace xcom {

class UiContext;
class ViewRegistry;

class ViewHost : public IViewHost
{
public:
    ViewHost(UiContext *ui, QString viewId, QString sessionId = QString());

    QVariantMap viewState() const override { return m_state; }
    void setViewState(const QVariantMap &state) override { m_state = state; }
    CommandResult executeCommand(const QString &commandId,
                                 const QVariantMap &args = QVariantMap()) override;
    void showView(const QString &viewId) override;
    QString sessionId() const override { return m_sessionId; }

private:
    UiContext *m_ui = nullptr;
    QString m_viewId;
    QString m_sessionId;
    QVariantMap m_state;
};

class ViewRegistry : public QObject, public IViewRegistry
{
    Q_OBJECT

public:
    explicit ViewRegistry(UiContext *ui, QObject *parent = nullptr);

    ISubscription *registerView(const ViewSpec &spec, IViewFactory *factory) override;
    bool show(const QString &viewId) override;
    QVector<ViewSpec> views() const override;

    bool contains(const QString &viewId) const;
    ViewSpec spec(const QString &viewId) const;
    QWidget *widget(const QString &viewId, QWidget *parent);

signals:
    void viewAdded(const QString &viewId);
    void viewRemoved(const QString &viewId);

private:
    struct Entry {
        ViewSpec spec;
        IViewFactory *factory = nullptr;
        QWidget *widget = nullptr;
        std::unique_ptr<ViewHost> host;
        ISubscription *subscription = nullptr;
    };

    void removeView(const QString &viewId);
    int indexOf(const QString &viewId) const;
    std::vector<std::unique_ptr<Entry>> m_entries;
    UiContext *m_ui = nullptr;
};

class MenuRegistry : public QObject, public IMenuRegistry
{
    Q_OBJECT

public:
    explicit MenuRegistry(QObject *parent = nullptr);

    ISubscription *addItem(const MenuContribution &contribution) override;
    QVector<MenuContribution> items() const override
    {
        QVector<MenuContribution> result;
        result.reserve(m_entries.size());
        for (const Entry &entry : m_entries) {
            result.append(entry.contribution);
        }
        return result;
    }

signals:
    void changed();

private:
    struct Entry {
        quint64 token = 0;
        MenuContribution contribution;
        ISubscription *subscription = nullptr;
    };

    void removeItem(quint64 token);
    QVector<Entry> m_entries;
    quint64 m_nextToken = 1;
};

class UiContext : public QObject, public IUiContext
{
    Q_OBJECT

public:
    explicit UiContext(QObject *parent = nullptr);

    IViewRegistry &views() override;
    IMenuRegistry &menus() override;
    void showView(const QString &viewId) override;
    void openSessionConsole(const QString &sessionId) override;
    void refreshMenus() override;

    ViewRegistry *viewRegistry() const { return m_views.get(); }
    MenuRegistry *menuRegistry() const { return m_menus.get(); }

    void setShowViewHandler(std::function<void(const QString &)> handler);
    void setOpenSessionHandler(std::function<void(const QString &)> handler);
    void setCommandHandler(
        std::function<CommandResult(const QString &, const QVariantMap &)> handler);
    CommandResult executeCommand(const QString &commandId,
                                 const QVariantMap &args = QVariantMap());

signals:
    void menuChanged();

private:
    std::unique_ptr<ViewRegistry> m_views;
    std::unique_ptr<MenuRegistry> m_menus;
    std::function<void(const QString &)> m_showViewHandler;
    std::function<void(const QString &)> m_openSessionHandler;
    std::function<CommandResult(const QString &, const QVariantMap &)> m_commandHandler;
};

} // namespace xcom
