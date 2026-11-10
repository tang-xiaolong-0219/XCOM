#include "shell/plugin_manager_view.h"

#include "services/plugin_host.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace xcom {

PluginManagerView::PluginManagerView(IPluginHost *host, QWidget *parent)
    : QWidget(parent)
    , m_host(host)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("已安装插件"), this);
    title->setStyleSheet(QStringLiteral("font-weight:600;"));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(refreshButton);
    layout->addLayout(header);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({QStringLiteral("启用"), QStringLiteral("插件 ID"),
                                        QStringLiteral("名称"), QStringLiteral("版本"),
                                        QStringLiteral("状态"), QStringLiteral("说明")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    auto *hint = new QLabel(
        QStringLiteral("启停插件即时生效；DLL 卸载与版本升级需要重启宿主。"), this);
    hint->setStyleSheet(QStringLiteral("color:#9d9d9d;"));
    layout->addWidget(hint);

    connect(refreshButton, &QPushButton::clicked, this, &PluginManagerView::refresh);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_updating || !m_host || !item || item->column() != 0) {
            return;
        }
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            return;
        }
        const bool enabled = item->checkState() == Qt::Checked;
        m_host->setEnabled(id, enabled);
        refresh();
    });

    if (auto *concrete = dynamic_cast<PluginHost *>(m_host)) {
        connect(concrete, &PluginHost::pluginsChanged, this, &PluginManagerView::refresh);
    }

    refresh();
}

void PluginManagerView::refresh()
{
    if (!m_host) {
        return;
    }
    m_updating = true;
    const QVector<PluginInfo> plugins = m_host->plugins();
    m_table->setRowCount(plugins.size());
    for (int row = 0; row < plugins.size(); ++row) {
        const PluginInfo &info = plugins.at(row);

        auto *check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        check->setCheckState(info.enabled ? Qt::Checked : Qt::Unchecked);
        check->setData(Qt::UserRole, info.id);
        m_table->setItem(row, 0, check);

        m_table->setItem(row, 1, new QTableWidgetItem(info.id));
        m_table->setItem(row, 2, new QTableWidgetItem(info.name));
        m_table->setItem(row, 3, new QTableWidgetItem(info.version));

        auto *state = new QTableWidgetItem(info.state);
        if (info.state == QStringLiteral("Active")) {
            state->setForeground(QColor(0x89, 0xd1, 0x85));
        } else if (info.state == QStringLiteral("Failed")
                   || info.state == QStringLiteral("Incompatible")) {
            state->setForeground(QColor(0xf4, 0x87, 0x71));
        } else if (info.state == QStringLiteral("Disabled")) {
            state->setForeground(QColor(0x9d, 0x9d, 0x9d));
        }
        m_table->setItem(row, 4, state);

        const QString detail =
            info.error.isEmpty() ? info.description : info.error;
        auto *detailItem = new QTableWidgetItem(detail);
        detailItem->setToolTip(detail);
        m_table->setItem(row, 5, detailItem);
    }
    m_updating = false;
}

} // namespace xcom
