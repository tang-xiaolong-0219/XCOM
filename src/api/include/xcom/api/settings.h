#pragma once

#include "xcom/api/types.h"

#include <QVariant>

namespace xcom {

class ISettingsService
{
public:
    virtual ~ISettingsService() = default;

    virtual QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const = 0;
    virtual void setValue(const QString &key, const QVariant &value) = 0;
    virtual void sync() = 0;
    virtual QString filePath() const = 0;
};

} // namespace xcom
