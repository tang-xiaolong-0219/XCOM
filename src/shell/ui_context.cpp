#include "shell/ui_context.h"

#include "services/subscription.h"

#include <QWidget>

namespace xcom {

ViewHost::ViewHost(UiContext *ui, QString viewId, QString sessionId)
    : m_ui(ui)
    , m_viewId(std::move(viewId))
    , m_sessionId(std::move(sessionId))
{
}

CommandResult ViewHost::executeCommand(const QString &commandId, const QVariantMap &args)
{
    if (!m_ui) {
        return CommandResult::failure(QStringLiteral("UI 上下文不可用"));
    }
    return m_ui->executeCommand(commandId, args);
}

void ViewHost::showView(const QString &viewId)
{
    if (m_ui) {
        m_ui->showView(viewId);
    }
}

ViewRegistry::ViewRegistry(UiContext *ui, QObject *parent)
    : QObject(parent)
    , m_ui(ui)
{
}

ISubscription *ViewRegistry::registerView(const ViewSpec &spec, IViewFactory *factory)
{
    if (spec.id.isEmpty() || !factory || contains(spec.id)) {
        return nullptr;
    }
    auto entry = std::make_unique<Entry>();
    entry->spec = spec;
    entry->factory = factory;
    entry->subscription =
        new CallbackSubscription([this, id = spec.id] { removeView(id); }, this);
    m_entries.push_back(std::move(entry));
    emit viewAdded(spec.id);
    return m_entries.back()->subscription;
}

bool ViewRegistry::show(const QString &viewId)
{
    if (!contains(viewId) || !m_ui) {
        return false;
    }
    m_ui->showView(viewId);
    return true;
}

QVector<ViewSpec> ViewRegistry::views() const
{
    QVector<ViewSpec> result;
    result.reserve(static_cast<int>(m_entries.size()));
    for (const auto &entry : m_entries) {
        result.append(entry->spec);
    }
    return result;
}

bool ViewRegistry::contains(const QString &viewId) const
{
    return indexOf(viewId) >= 0;
}

ViewSpec ViewRegistry::spec(const QString &viewId) const
{
    const int index = indexOf(viewId);
    return index >= 0 ? m_entries.at(static_cast<size_t>(index))->spec : ViewSpec{};
}

QWidget *ViewRegistry::widget(const QString &viewId, QWidget *parent)
{
    const int index = indexOf(viewId);
    if (index < 0) {
        return nullptr;
    }
    Entry &entry = *m_entries.at(static_cast<size_t>(index));
    if (!entry.widget) {
        entry.host = std::make_unique<ViewHost>(m_ui, viewId);
        entry.widget = entry.factory->create(parent, entry.host.get());
        if (entry.widget) {
            entry.widget->setObjectName(QStringLiteral("pluginView_") + viewId);
            QObject::connect(entry.widget, &QObject::destroyed, this, [this, viewId] {
                const int current = indexOf(viewId);
                if (current >= 0) {
                    m_entries.at(static_cast<size_t>(current))->widget = nullptr;
                }
            });
        }
    }
    return entry.widget;
}

int ViewRegistry::indexOf(const QString &viewId) const
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i)->spec.id == viewId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ViewRegistry::removeView(const QString &viewId)
{
    const int index = indexOf(viewId);
    if (index < 0) {
        return;
    }
    emit viewRemoved(viewId);
    QWidget *widget = m_entries.at(static_cast<size_t>(index))->widget;
    m_entries.erase(m_entries.begin() + index);
    if (widget) {
        widget->deleteLater();
    }
}

MenuRegistry::MenuRegistry(QObject *parent)
    : QObject(parent)
{
}

ISubscription *MenuRegistry::addItem(const MenuContribution &contribution)
{
    if (contribution.menu.isEmpty() || contribution.command.isEmpty()) {
        return nullptr;
    }
    Entry entry;
    entry.token = m_nextToken++;
    entry.contribution = contribution;
    entry.subscription =
        new CallbackSubscription([this, token = entry.token] { removeItem(token); }, this);
    m_entries.append(entry);
    emit changed();
    return entry.subscription;
}

void MenuRegistry::removeItem(quint64 token)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).token == token) {
            m_entries.remove(i);
            emit changed();
            return;
        }
    }
}

UiContext::UiContext(QObject *parent)
    : QObject(parent)
    , m_views(std::make_unique<ViewRegistry>(this))
    , m_menus(std::make_unique<MenuRegistry>())
{
    connect(m_menus.get(), &MenuRegistry::changed, this, &UiContext::menuChanged);
}

IViewRegistry &UiContext::views()
{
    return *m_views;
}

IMenuRegistry &UiContext::menus()
{
    return *m_menus;
}

void UiContext::showView(const QString &viewId)
{
    if (m_showViewHandler) {
        m_showViewHandler(viewId);
    }
}

void UiContext::openSessionConsole(const QString &sessionId)
{
    if (m_openSessionHandler) {
        m_openSessionHandler(sessionId);
    }
}

void UiContext::refreshMenus()
{
    emit menuChanged();
}

void UiContext::setShowViewHandler(std::function<void(const QString &)> handler)
{
    m_showViewHandler = std::move(handler);
}

void UiContext::setOpenSessionHandler(std::function<void(const QString &)> handler)
{
    m_openSessionHandler = std::move(handler);
}

void UiContext::setCommandHandler(
    std::function<CommandResult(const QString &, const QVariantMap &)> handler)
{
    m_commandHandler = std::move(handler);
}

CommandResult UiContext::executeCommand(const QString &commandId, const QVariantMap &args)
{
    if (!m_commandHandler) {
        return CommandResult::failure(QStringLiteral("命令系统未初始化"));
    }
    return m_commandHandler(commandId, args);
}

} // namespace xcom
