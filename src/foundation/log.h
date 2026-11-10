#pragma once

#include <QString>

namespace xcom {

// 应用数据目录（%APPDATA%/X-COM 或平台等价路径），不存在时自动创建。
QString appDataDir();

// 日志目录，不存在时自动创建。
QString appLogDir();

// 确保目录存在，返回绝对路径；失败返回空。
QString ensureDir(const QString &path);

// 安装日志处理器：同时输出到 stderr 与按天滚动的日志文件。
void initLogging(const QString &logDir = QString());

} // namespace xcom
