#include "TrayIcon.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QPixmap>

// Recolor every opaque pixel of src to white, preserving alpha.
// Used to produce a white tray icon that stays visible on dark taskbars.
static QIcon makeWhiteIcon(const QString &resourcePath)
{
    QImage img(resourcePath);
    if (img.isNull()) return QIcon(resourcePath);
    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            line[x] = qRgba(255, 255, 255, a);
        }
    }
    return QIcon(QPixmap::fromImage(img));
}

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
    , m_tray(new QSystemTrayIcon(makeWhiteIcon(QStringLiteral(":/icons/icon.png")), this))
{
    auto *menu   = new QMenu();
    auto *show   = new QAction(QStringLiteral("Show"), menu);
    auto *quit   = new QAction(QStringLiteral("Quit"), menu);
    menu->addAction(show);
    menu->addSeparator();
    menu->addAction(quit);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QStringLiteral("YukiSend"));
    m_tray->show();

    connect(show, &QAction::triggered, this, &TrayIcon::showRequested);
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    connect(m_tray, &QSystemTrayIcon::activated,
            this, [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::MiddleClick ||
                    reason == QSystemTrayIcon::DoubleClick)
                    emit showRequested();
            });

    connect(m_tray, &QSystemTrayIcon::messageClicked,
            this, &TrayIcon::showRequested);
}

void TrayIcon::notify(const QString &title, const QString &body) {
    m_tray->showMessage(title, body, QSystemTrayIcon::NoIcon, 4000);
}
