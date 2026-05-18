#pragma once

#include <QObject>
#include <QString>

class QSystemTrayIcon;

// Owns the system tray icon and context menu.
// Call notify() to show a balloon/toast. showRequested() fires when the user
// clicks the icon or the balloon so the caller can restore the window.
class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);

    void notify(const QString &title, const QString &body);

signals:
    void showRequested();

private:
    QSystemTrayIcon *m_tray;
};
