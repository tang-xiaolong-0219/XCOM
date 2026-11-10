#include "foundation/log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace xcom {
namespace {

QMutex g_logMutex;
QFile g_logFile;
QTextStream g_logStream;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO ";
    case QtWarningMsg:
        return "WARN ";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "INFO ";
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString line = QStringLiteral("[%1] [%2] [%3] %4\n")
                             .arg(QDateTime::currentDateTime().toString(
                                 QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                             .arg(QString::fromLatin1(levelName(type)))
                             .arg(QString::fromLatin1(context.category ? context.category : "xcom"))
                             .arg(message);

    {
        QMutexLocker locker(&g_logMutex);
        if (g_logFile.isOpen()) {
            g_logStream << line;
            g_logStream.flush();
        }
    }

    const QByteArray utf8 = line.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

QString ensureDir(const QString &path)
{
    QDir dir(path);
    if (!dir.exists() && !QDir().mkpath(path)) {
        return QString();
    }
    return dir.absolutePath();
}

QString appDataDir()
{
    // 不用 AppDataLocation，避免「组织名/应用名」重复成 X-COM/X-COM。
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.xcom");
        return ensureDir(base);
    }
    return ensureDir(base + QStringLiteral("/X-COM"));
}

QString appLogDir()
{
    return ensureDir(appDataDir() + QStringLiteral("/logs"));
}

void initLogging(const QString &logDir)
{
    const QString dir = logDir.isEmpty() ? appLogDir() : logDir;
    if (!dir.isEmpty()) {
        const QString filePath =
            QDir(dir).filePath(QStringLiteral("xcom-")
                               + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"))
                               + QStringLiteral(".log"));
        g_logFile.setFileName(filePath);
        if (g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            g_logStream.setDevice(&g_logFile);
        }
    }
    qInstallMessageHandler(messageHandler);
}

} // namespace xcom
