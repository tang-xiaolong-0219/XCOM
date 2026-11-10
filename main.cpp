#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("X-COM"));
    QApplication::setOrganizationDomain(QStringLiteral("x-com.local"));
    QApplication::setApplicationName(QStringLiteral("X-COM"));
    QApplication::setApplicationDisplayName(QStringLiteral("X-COM 串口助手"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    MainWindow window;
    window.show();
    return app.exec();
}
