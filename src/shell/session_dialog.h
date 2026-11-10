#pragma once

#include "xcom/api/types.h"

#include <QDialog>
#include <QStringList>

#include <functional>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QPushButton;
class QDialogButtonBox;
class QTimer;
QT_END_NAMESPACE

namespace xcom {

class SessionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SessionDialog(const QString &defaultPort, QWidget *parent = nullptr);
    explicit SessionDialog(const SessionDescriptor &initial, QWidget *parent = nullptr);
    void setEmbeddedMode(bool embedded);
    void setBusyPortsProvider(std::function<QStringList()> provider);

    SessionDescriptor descriptor() const;

signals:
    void configurationApplied(const xcom::SessionDescriptor &descriptor);

private:
    void refreshPorts();
    void updateDefaultName();

    QComboBox *m_port = nullptr;
    QComboBox *m_baud = nullptr;
    QComboBox *m_dataBits = nullptr;
    QComboBox *m_stopBits = nullptr;
    QComboBox *m_parity = nullptr;
    QComboBox *m_flow = nullptr;
    QLineEdit *m_name = nullptr;
    QPushButton *m_applyButton = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QString m_preferredPort;
    std::function<QStringList()> m_busyPortsProvider;
    bool m_embedded = false;
    bool m_nameEdited = false;
    bool m_autoSelectPort = false;
};

} // namespace xcom
