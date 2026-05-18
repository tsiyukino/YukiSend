#pragma once

#include <QWidget>
#include <optional>

#include "network/Discovery.h"  // for Peer
#include "model/UnreadStore.h"
#include "model/AppSettings.h"
#include "ui/PeerListWidget.h"  // for ExtendedPeer

class App;
class NavBar;
class SearchBar;
class PeerListWidget;
class ChatView;
class ChatInputBar;
class TrayIcon;
class SettingsPage;
class PeerPanel;

// Top-level window. Owns layout geometry; all drawing is in paintEvent().
class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(App *app, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    void layoutChildren();
    void restoreWindow();
    QList<ExtendedPeer> buildPeerList() const;

    App            *m_app;
    NavBar         *m_navBar;
    SearchBar      *m_searchBar;
    PeerListWidget *m_peerList;
    ChatView       *m_chatView;
    ChatInputBar   *m_inputBar;
    TrayIcon       *m_tray;
    SettingsPage   *m_settingsPage;
    PeerPanel      *m_peerPanel;

    UnreadStore         m_unread;
    AppSettings         m_settings;
    std::optional<Peer> m_currentPeer;
};
