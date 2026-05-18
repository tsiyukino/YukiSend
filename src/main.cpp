#include <QApplication>
#include <QGuiApplication>
#include <QIcon>

#include "app/App.h"
#include "theme/Fonts.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("YukiSend"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    app.setWindowIcon(QIcon(QStringLiteral(":/icons/icon.png")));

    Fonts::load();

    App controller;
    controller.start();

    MainWindow window(&controller);
    window.show();

    return app.exec();
}
