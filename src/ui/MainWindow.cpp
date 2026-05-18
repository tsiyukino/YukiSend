#include "MainWindow.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QFileDialog>
#include <QApplication>
#include <QUrl>
#include <QFileInfo>

#include "NavBar.h"
#include "SearchBar.h"
#include "PeerListWidget.h"
#include "ChatView.h"
#include "ChatInputBar.h"
#include "TrayIcon.h"
#include "CloseDialog.h"
#include "FingerprintDialog.h"
#include "SettingsPage.h"
#include "PeerPanel.h"
#include "app/App.h"
#include "model/Message.h"
#include "model/PeerStore.h"
#include "theme/Theme.h"
#include "theme/Fonts.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

// SearchHeight(32) + SearchVPad(8)*2
static constexpr int kSearchBarH = Theme::Space::SearchHeight + Theme::Space::SearchVPad * 2;
static constexpr int kNavW       = Theme::Space::NavBarWidth;

MainWindow::MainWindow(App *app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
    , m_navBar(new NavBar(this))
    , m_searchBar(new SearchBar(this))
    , m_peerList(new PeerListWidget(this))
    , m_chatView(new ChatView(this))
    , m_inputBar(new ChatInputBar(this))
    , m_tray(new TrayIcon(this))
    , m_settingsPage(new SettingsPage(this))
    , m_peerPanel(new PeerPanel(this))
{
    setMinimumSize(Theme::Space::WindowMinWidth, Theme::Space::WindowMinHeight);
    resize(Theme::Space::WindowInitWidth, Theme::Space::WindowInitHeight);
    setWindowTitle(QStringLiteral("YukiSend"));

    // Hide chat area until a peer is selected
    m_chatView->hide();
    m_inputBar->hide();
    m_settingsPage->hide();

    m_peerList->setUnreadStore(&m_unread);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::Color::WindowBg);
    setPalette(pal);

    // Tray icon → restore window
    connect(m_tray, &TrayIcon::showRequested, this, &MainWindow::restoreWindow);

    // Search → filter peer list
    connect(m_searchBar, &SearchBar::textChanged,
            m_peerList,  &PeerListWidget::setFilter);

    // Peer selected → show chat area, load history, clear unread badge
    connect(m_peerList, &PeerListWidget::peerSelected,
            this, [this](const Peer &peer) {
                m_currentPeer = peer;
                m_unread.clear(peer.id);
                m_peerList->update();
                m_chatView->setPeer(peer);
                m_chatView->setMessages(m_app->messages(peer.id));
                m_chatView->show();
                m_inputBar->show();
                layoutChildren();
            });

    // Peer deselected (clicked again or went offline) — hide the chat area.
    connect(m_peerList, &PeerListWidget::peerDeselected, this, [this] {
        m_currentPeer.reset();
        m_chatView->hide();
        m_inputBar->hide();
        layoutChildren();
    });

    // Discovery updates — merge online + saved-offline, exclude blocked.
    connect(m_app, &App::peersChanged, this, [this] {
        m_peerList->setPeers(buildPeerList());
    });

    // First contact with a new peer — ask the user whether to trust or block.
    connect(m_app, &App::unknownPeerFingerprint, this, [this](const Peer &peer) {
        auto *dlg = FingerprintDialog::forUnknownPeer(peer, this);
        dlg->exec();
        switch (dlg->userResult()) {
        case FingerprintDialog::UserResult::Trust:
            m_app->peerStore()->trustPeer(peer.id, peer.fingerprint);
            break;
        case FingerprintDialog::UserResult::Block:
            m_app->setBlocked(peer.id, true);
            break;
        case FingerprintDialog::UserResult::Later:
            break;
        }
    });

    // Known peer whose fingerprint changed — warn the user.
    connect(m_app, &App::fingerprintMismatch, this,
            [this](const Peer &peer, const QString &knownFp, const QString &seenFp) {
                auto *dlg = FingerprintDialog::forMismatch(peer, knownFp, seenFp, this);
                dlg->exec();
                switch (dlg->userResult()) {
                case FingerprintDialog::UserResult::Trust:
                    m_app->peerStore()->trustPeer(peer.id, peer.fingerprint);
                    break;
                case FingerprintDialog::UserResult::Block:
                    m_app->setBlocked(peer.id, true);
                    break;
                case FingerprintDialog::UserResult::Later:
                    break;
                }
            });

    // New message from App
    connect(m_app, &App::messageAdded, this, [this](const Message &msg) {
        const bool isCurrentPeer = m_currentPeer.has_value()
                                   && msg.peerId == m_currentPeer->id;
        if (isCurrentPeer)
            m_chatView->appendMessage(msg);

        // Notify and badge for incoming messages when this peer is not active.
        if (!msg.outgoing && !isCurrentPeer) {
            m_unread.increment(msg.peerId);
            m_peerList->update();

            // Find peer display name for the notification title.
            QString peerName;
            for (const Peer &p : m_app->peers()) {
                if (p.id == msg.peerId) { peerName = p.displayName; break; }
            }
            const QString title = peerName.isEmpty() ? QStringLiteral("YukiSend") : peerName;
            const QString body  = (msg.type == MessageType::Text)
                ? msg.text.left(80)
                : QStringLiteral("Incoming file request");
            m_tray->notify(title, body);
        }
    });

    connect(m_app, &App::messageUpdated, this, [this](const Message &msg) {
        if (m_currentPeer.has_value() && msg.peerId == m_currentPeer->id)
            m_chatView->updateMessage(msg);
    });

    // Input bar signals
    connect(m_inputBar, &ChatInputBar::messageSent,
            this, [this](const QString &text) {
                if (!m_currentPeer.has_value()) return;
                m_app->sendMessage(m_currentPeer->id, text);
            });

    connect(m_inputBar, &ChatInputBar::fileSendRequested,
            this, [this] {
                if (!m_currentPeer.has_value()) return;
                const QString path = QFileDialog::getOpenFileName(
                    this, QStringLiteral("Select file to send"));
                if (!path.isEmpty())
                    m_app->sendFile(m_currentPeer->id, path);
            });

    connect(m_inputBar, &ChatInputBar::folderSendRequested,
            this, [this] {
                if (!m_currentPeer.has_value()) return;
                const QString path = QFileDialog::getExistingDirectory(
                    this, QStringLiteral("Select folder to send"));
                if (!path.isEmpty())
                    m_app->sendFolder(m_currentPeer->id, path);
            });

    connect(m_inputBar, &ChatInputBar::imageSendRequested,
            this, [this](const QImage &image) {
                if (!m_currentPeer.has_value()) return;
                m_app->sendImage(m_currentPeer->id, image);
            });

    // Chat view action signals → App
    connect(m_chatView, &ChatView::acceptRequested,
            this, [this](qint64 id) { m_app->acceptTransfer(id); });

    connect(m_chatView, &ChatView::acceptToRequested,
            this, [this](qint64 id) {
                const QString dir = QFileDialog::getExistingDirectory(
                    this, QStringLiteral("Save file to…"));
                if (!dir.isEmpty())
                    m_app->acceptTransferTo(id, dir);
            });

    connect(m_chatView, &ChatView::denyRequested,
            this, [this](qint64 id) { m_app->denyTransfer(id); });
    connect(m_chatView, &ChatView::requestAgainRequested,
            this, [this](qint64 id) { m_app->requestAgain(id); });

    connect(m_chatView, &ChatView::filesDropped,
            this, [this](const QList<QUrl> &urls) {
                if (!m_currentPeer.has_value()) return;
                for (const QUrl &url : urls) {
                    if (!url.isLocalFile()) continue;
                    const QString path = url.toLocalFile();
                    const QFileInfo info(path);
                    if (info.isDir())
                        m_app->sendFolder(m_currentPeer->id, path);
                    else if (info.isFile())
                        m_app->sendFile(m_currentPeer->id, path);
                }
            });

    // ── NavBar page switching ─────────────────────────────────────────────────
    connect(m_navBar, &NavBar::pageChanged, this, [this](int page) {
        const bool isSettings = (page == 3);
        m_searchBar->setVisible(!isSettings);
        m_peerList->setVisible(!isSettings);
        // Chat area only if a peer is already selected AND not on settings page.
        const bool chatVisible = !isSettings && m_currentPeer.has_value();
        m_chatView->setVisible(chatVisible);
        m_inputBar->setVisible(chatVisible);
        m_settingsPage->setVisible(isSettings);
        // Close peer panel when leaving chat page
        if (isSettings && m_peerPanel->isOpen())
            m_peerPanel->slideOut();
        // Refresh history list when entering settings
        if (isSettings)
            m_settingsPage->refreshHistory(
                m_app->chatStore()->peersWithHistory());
        layoutChildren();
    });

    // ── ChatView hamburger → open PeerPanel ──────────────────────────────────
    connect(m_chatView, &ChatView::peerPanelRequested, this, [this] {
        if (!m_currentPeer.has_value()) return;
        const StorageStrategy s = m_app->chatStore()->strategy(m_currentPeer->id);
        m_peerPanel->openFor(*m_currentPeer, m_app->peerStore(), s);
        m_peerPanel->slideIn();
    });

    // ── PeerPanel signals → App + refresh peer list ───────────────────────────
    connect(m_peerPanel, &PeerPanel::savedChanged,
            this, [this](const QString &peerId, bool saved) {
                m_app->setSaved(peerId, saved);
                m_peerList->setPeers(buildPeerList());
            });

    connect(m_peerPanel, &PeerPanel::favoriteChanged,
            this, [this](const QString &peerId, bool favorite) {
                m_app->setFavorite(peerId, favorite);
                m_peerList->setPeers(buildPeerList());
            });

    connect(m_peerPanel, &PeerPanel::blockedChanged,
            this, [this](const QString &peerId, bool blocked) {
                m_app->setBlocked(peerId, blocked);
                m_peerList->setPeers(buildPeerList());
                // If the currently open chat peer was just blocked, close it.
                if (blocked && m_currentPeer.has_value()
                    && m_currentPeer->id == peerId)
                {
                    m_currentPeer.reset();
                    m_chatView->hide();
                    m_inputBar->hide();
                    m_peerPanel->slideOut();
                    layoutChildren();
                }
            });

    connect(m_peerPanel, &PeerPanel::strategyChanged,
            this, [this](const QString &peerId, StorageStrategy s) {
                m_app->setPeerStorageStrategy(peerId, s);
            });

    // ── Settings page: populate from persisted values ─────────────────────────
    m_settingsPage->setDisplayName(m_settings.displayName());
    m_settingsPage->setDownloadDir(m_settings.downloadDir());
    m_settingsPage->setDataDir(m_settings.dataDir());
    m_settingsPage->setCloseAction(m_settings.closeAction());
    m_settingsPage->setLaunchAtStartup(m_settings.launchAtStartup());
    m_settingsPage->setDefaultStorageStrategy(m_settings.defaultStorageStrategy());

    // ── Settings page signals → App + AppSettings ─────────────────────────────
    connect(m_settingsPage, &SettingsPage::displayNameChanged,
            this, [this](const QString &name) {
                m_app->setDisplayName(name);
            });

    connect(m_settingsPage, &SettingsPage::downloadDirChanged,
            this, [this](const QString &dir) {
                m_settings.setDownloadDir(dir);
                m_app->setDownloadDir(dir);
            });

    connect(m_settingsPage, &SettingsPage::dataDirChanged,
            this, [this](const QString &dir) {
                m_settings.setDataDir(dir);
                // Data dir change takes effect after restart — no live action needed.
            });

    connect(m_settingsPage, &SettingsPage::closeActionChanged,
            this, [this](CloseAction action) {
                m_settings.setCloseAction(action);
            });

    connect(m_settingsPage, &SettingsPage::launchAtStartupChanged,
            this, [this](bool enable) {
                m_settings.setLaunchAtStartup(enable);
            });

    connect(m_settingsPage, &SettingsPage::defaultStorageStrategyChanged,
            this, [this](StorageStrategy s) {
                m_app->setDefaultStorageStrategy(s);
            });

    connect(m_settingsPage, &SettingsPage::deleteHistoryRequested,
            this, [this](const QStringList &peerIds) {
                m_app->deleteHistory(peerIds);
            });

    // ── Settings page: peer list removals ─────────────────────────────────────
    m_settingsPage->setPeerStore(m_app->peerStore());

    connect(m_settingsPage, &SettingsPage::unsavePeerRequested,
            this, [this](const QStringList &peerIds) {
                for (const QString &id : peerIds)
                    m_app->setSaved(id, false);
                m_peerList->setPeers(buildPeerList());
                m_settingsPage->refreshPeers();
            });

    connect(m_settingsPage, &SettingsPage::unfavoritePeerRequested,
            this, [this](const QStringList &peerIds) {
                for (const QString &id : peerIds)
                    m_app->setFavorite(id, false);
                m_peerList->setPeers(buildPeerList());
                m_settingsPage->refreshPeers();
            });

    connect(m_settingsPage, &SettingsPage::unblockPeerRequested,
            this, [this](const QStringList &peerIds) {
                for (const QString &id : peerIds)
                    m_app->setBlocked(id, false);
                m_peerList->setPeers(buildPeerList());
                m_settingsPage->refreshPeers();
            });

    // Populate peer list immediately with saved peers so they show at startup
    // even before any discovery beacons arrive.
    m_peerList->setPeers(buildPeerList());

    layoutChildren();
}

MainWindow::~MainWindow() = default;

static void applyWhiteTitleBar(WId wid) {
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(wid);
    BOOL darkMode = FALSE;
    DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
    COLORREF white = 0x00FFFFFF;
    DwmSetWindowAttribute(hwnd, 35, &white, sizeof(white));
#else
    Q_UNUSED(wid)
#endif
}

void MainWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    applyWhiteTitleBar(winId());
}

void MainWindow::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        applyWhiteTitleBar(winId());
        // Minimize to tray: hide the window instead of shrinking to taskbar.
        if (isMinimized()) {
            QMetaObject::invokeMethod(this, &QWidget::hide, Qt::QueuedConnection);
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    CloseAction action = m_settings.closeAction();

    if (action == CloseAction::Unset) {
        CloseDialog dlg(this);
        dlg.exec();
        action = dlg.choice();
        if (dlg.rememberChoice())
            m_settings.setCloseAction(action);
    }

    if (action == CloseAction::MinimizeToTray) {
        event->ignore();
        hide();
    } else {
        event->accept();
        QApplication::quit();
    }
}

void MainWindow::restoreWindow() {
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void MainWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Theme::Color::WindowBg);

    const int sidebarLeft = kNavW;
    const int sidebarW    = Theme::Space::SidebarWidth;

    // Divider between nav and sidebar
    p.fillRect(sidebarLeft + sidebarW, 0,
               Theme::Space::DividerThick, height(),
               Theme::Color::Divider);

    // Horizontal divider under search bar
    p.fillRect(sidebarLeft, kSearchBarH,
               sidebarW, Theme::Space::DividerThick,
               Theme::Color::Divider);

}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    layoutChildren();
    // Keep peer panel anchored to right edge on resize
    if (m_peerPanel->isOpen()) {
        m_peerPanel->resize(PeerPanel::kW, height());
        m_peerPanel->move(width() - PeerPanel::kW, 0);
    }
}

QList<ExtendedPeer> MainWindow::buildPeerList() const {
    PeerStore *ps = m_app->peerStore();

    // Index online peers by ID for fast lookup.
    const QList<Peer> online = m_app->peers();
    QHash<QString, Peer> onlineById;
    for (const Peer &p : online)
        onlineById.insert(p.id, p);

    QList<ExtendedPeer> result;

    // Add online peers first (excludes blocked).
    for (const Peer &p : online) {
        if (ps->isBlocked(p.id)) continue;
        ExtendedPeer ep;
        ep.peer       = p;
        ep.isOnline   = true;
        ep.isFavorite = ps->isFavorite(p.id);
        result.append(ep);
    }

    // Add saved-offline peers (those not currently online, not blocked).
    for (const Peer &p : ps->savedPeers()) {
        if (onlineById.contains(p.id)) continue;  // already listed as online
        if (ps->isBlocked(p.id))       continue;
        ExtendedPeer ep;
        ep.peer       = p;
        ep.isOnline   = false;
        ep.isFavorite = ps->isFavorite(p.id);
        result.append(ep);
    }

    return result;
}

void MainWindow::layoutChildren() {
    const int sw = Theme::Space::SidebarWidth;
    const int dv = Theme::Space::DividerThick;

    m_navBar->setGeometry(0, 0, kNavW, height());

    m_searchBar->setGeometry(kNavW, 0, sw, kSearchBarH);

    m_peerList->setGeometry(kNavW, kSearchBarH + dv,
                            sw, height() - kSearchBarH - dv);

    const int chatLeft = kNavW + sw + dv;
    const int chatW    = width() - chatLeft;
    const int inputH   = m_inputBar->height(); // fixed by ChatInputBar

    m_chatView->setGeometry(chatLeft, 0, chatW, height() - inputH);
    m_inputBar->setGeometry(chatLeft, height() - inputH, chatW, inputH);

    // Settings page occupies the full content area (right of NavBar).
    m_settingsPage->setGeometry(kNavW, 0, width() - kNavW, height());
}
