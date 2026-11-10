#pragma once

#include "xcom/api/plugin.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QTableWidget;
QT_END_NAMESPACE

namespace xcom {

class PluginManagerView : public QWidget
{
    Q_OBJECT

public:
    explicit PluginManagerView(IPluginHost *host, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    IPluginHost *m_host = nullptr;
    QTableWidget *m_table = nullptr;
    bool m_updating = false;
};

} // namespace xcom
