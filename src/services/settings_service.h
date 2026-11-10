#pragma once

#include "xcom/api/settings.h"

#include <QHash>
#include <QObject>

class QTimer;

namespace xcom {

class SettingsService : public QObject, public ISettingsService
{
    Q_OBJECT

public:
    explicit SettingsService(const QString &filePath, QObject *parent = nullptr);
    ~SettingsService() override;

    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const override;
    void setValue(const QString &key, const QVariant &value) override;
    void sync() override;
    QString filePath() const override { return m_path; }

private:
    void load();
    void scheduleSync();

    QString m_path;
    QHash<QString, QVariant> m_values;
    QTimer *m_syncTimer = nullptr;
};

} // namespace xcom
