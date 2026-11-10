#include "shell/command_palette.h"

#include "services/command_registry.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace xcom {

CommandPalette::CommandPalette(CommandRegistry *commands, QWidget *parent)
    : QDialog(parent)
    , m_commands(commands)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setFixedSize(720, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("输入命令名称或 id…（↑↓ 选择，Enter 执行，Esc 关闭）"));
    m_search->installEventFilter(this);
    QFont searchFont = m_search->font();
    searchFont.setPointSize(searchFont.pointSize() + 1);
    m_search->setFont(searchFont);
    m_search->setMinimumHeight(38);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    m_hint = new QLabel(this);
    m_hint->setContentsMargins(8, 4, 8, 4);
    m_hint->setMinimumHeight(24);

    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_hint);

    m_specs = m_commands ? m_commands->commands() : QVector<CommandSpec>();
    std::sort(m_specs.begin(), m_specs.end(), [](const CommandSpec &a, const CommandSpec &b) {
        if (a.category != b.category) {
            return a.category < b.category;
        }
        return a.title < b.title;
    });

    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::rebuild);
    connect(m_search, &QLineEdit::returnPressed, this, &CommandPalette::executeCurrent);
    connect(m_list, &QListWidget::itemActivated, this, &CommandPalette::executeItem);
    connect(m_list, &QListWidget::itemClicked, this, &CommandPalette::executeItem);
}

void CommandPalette::showPalette()
{
    // 命令可能由插件在运行期注册/撤销，每次打开都刷新一次。
    m_specs = m_commands ? m_commands->commands() : QVector<CommandSpec>();
    std::sort(m_specs.begin(), m_specs.end(), [](const CommandSpec &a, const CommandSpec &b) {
        if (a.category != b.category) {
            return a.category < b.category;
        }
        return a.title < b.title;
    });

    m_hint->clear();
    m_search->clear();
    rebuild(QString());
    m_search->setFocus();

    if (parentWidget()) {
        const QRect parentGeometry = parentWidget()->geometry();
        move(parentGeometry.center() - rect().center());
    } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
        move(screen->availableGeometry().center() - rect().center());
    }
    show();
    raise();
    activateWindow();
}

void CommandPalette::rebuild(const QString &filter)
{
    m_list->clear();
    const QString needle = filter.trimmed();

    for (const CommandSpec &spec : m_specs) {
        if (spec.hidden) {
            continue;
        }
        const QString haystack =
            (spec.title + QLatin1Char(' ') + spec.id + QLatin1Char(' ') + spec.category)
                .toLower();
        if (!needle.isEmpty() && !haystack.contains(needle.toLower())) {
            continue;
        }
        QString text = spec.title;
        if (!spec.category.isEmpty()) {
            text = QStringLiteral("%1  ·  %2").arg(spec.title, spec.category);
        }
        text += QStringLiteral("    %1").arg(spec.id);
        auto *item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, spec.id);
    }

    if (m_list->count() == 0) {
        auto *item = new QListWidgetItem(QStringLiteral("没有匹配的命令"), m_list);
        item->setFlags(Qt::NoItemFlags);
    } else {
        m_list->setCurrentRow(0);
    }
}

void CommandPalette::executeCurrent()
{
    executeItem(m_list->currentItem());
}

void CommandPalette::executeItem(QListWidgetItem *item)
{
    if (!item || !m_commands) {
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) {
        return;
    }

    CommandInvocation invocation;
    invocation.id = id;
    invocation.source = QStringLiteral("palette");
    CommandResult result;
    const bool ok = m_commands->execute(invocation, &result);
    if (ok) {
        accept();
        return;
    }

    m_hint->setStyleSheet(QStringLiteral("color:#f48771;"));
    m_hint->setText(result.message.isEmpty() ? QStringLiteral("命令执行失败") : result.message);
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
            const int row = m_list->currentRow();
            const int next =
                keyEvent->key() == Qt::Key_Down ? qMin(row + 1, m_list->count() - 1)
                                                : qMax(row - 1, 0);
            m_list->setCurrentRow(next);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace xcom
