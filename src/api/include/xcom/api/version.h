#pragma once

#include <QString>
#include <QtGlobal>

#define XCOM_API_VERSION_MAJOR 1
#define XCOM_API_VERSION_MINOR 0
#define XCOM_API_VERSION_STRING "1.0"
#define XCOM_PLUGIN_IID "com.xcom.plugin/1.0"

#if defined(XCOM_API_LIBRARY)
#  define XCOM_API_EXPORT Q_DECL_EXPORT
#else
#  define XCOM_API_EXPORT Q_DECL_IMPORT
#endif

namespace xcom {

XCOM_API_EXPORT QString apiVersion();
XCOM_API_EXPORT QString apiHash();

} // namespace xcom
