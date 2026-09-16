#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TypeHelper"));
    QApplication::setOrganizationName(QStringLiteral("TypeHelper"));
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion")))
        QApplication::setStyle(QStringLiteral("Fusion"));

    MainWindow w;
    w.show();
    return a.exec();
}
