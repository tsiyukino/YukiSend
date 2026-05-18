#pragma once

#include <QWidget>
#include <QWheelEvent>
#include <QString>
#include <QStringList>
#include <QList>
#include <QScrollArea>

#include "model/CloseAction.h"
#include "model/ChatStore.h"   // StorageStrategy
#include "model/PeerStore.h"   // Peer, PeerStore
#include "network/Discovery.h" // Peer

class QLineEdit;

// ── PeerCheckList ─────────────────────────────────────────────────────────────
// Generic scrollable peer list with checkboxes. Used inside the Peers category
// for Saved / Favorites / Blocked sub-tabs.
class PeerCheckList : public QWidget {
    Q_OBJECT
public:
    explicit PeerCheckList(QWidget *parent = nullptr);

    void setPeers(const QList<Peer> &peers);
    QList<Peer> checkedPeers() const;
    void        checkAll(bool check);
    bool        allChecked() const;
    bool        isEmpty() const { return m_rows.isEmpty(); }

signals:
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    struct Row { Peer peer; bool checked = false; };
    QList<Row> m_rows;
    int        m_hoverIndex = -1;

    static constexpr int kRowH      = 52;
    static constexpr int kPadH      = 16;
    static constexpr int kCheckSize = 16;
};

// ── SettingsPage ──────────────────────────────────────────────────────────────
// Two-pane layout: 160px left nav sidebar + right content area.
// Seven categories; Peers has three sub-tabs (Saved/Favorites/Blocked).
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    // Called once at startup so the page can query peers directly.
    void setPeerStore(PeerStore *store);

    // Populate fields before first show.
    void setDisplayName(const QString &name);
    void setDownloadDir(const QString &dir);
    void setDataDir(const QString &dir);
    void setCloseAction(CloseAction action);
    void setLaunchAtStartup(bool enable);
    void setDefaultStorageStrategy(StorageStrategy s);

    // Call when the page becomes visible to refresh all live data.
    void refreshHistory(const QStringList &peerIds);
    void refreshPeers();

signals:
    void displayNameChanged(const QString &name);
    void downloadDirChanged(const QString &dir);
    void dataDirChanged(const QString &dir);
    void closeActionChanged(CloseAction action);
    void launchAtStartupChanged(bool enable);
    void defaultStorageStrategyChanged(StorageStrategy s);
    void deleteHistoryRequested(const QStringList &peerIds);
    // Peer list removals — MainWindow routes these to App/PeerStore
    void unsavePeerRequested(const QStringList &peerIds);
    void unfavoritePeerRequested(const QStringList &peerIds);
    void unblockPeerRequested(const QStringList &peerIds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // ── Categories ────────────────────────────────────────────────────────────
    enum Category { General=0, Window, Startup, Messages, History, Peers, About, kCatCount };
    enum PeersTab { Saved=0, Favorites, Blocked, kTabCount };

    // ── Sidebar geometry ──────────────────────────────────────────────────────
    static constexpr int kSideW    = 148;  // sidebar width
    static constexpr int kNavItemH =  36;  // sidebar row height

    QRect sidebarRect()  const;
    QRect contentRect()  const;
    QRect navItemRect(int cat) const;

    // ── Content geometry helpers ──────────────────────────────────────────────
    int  contentHeight(Category cat) const;
    void clampScroll();
    void layoutChildren();

    // Hit-test rects (in widget coords, scroll-adjusted)
    QRect browseBtnRect()     const;  // Download folder
    QRect browseDataBtnRect() const;  // Data folder
    QRect radioMinRect()    const;
    QRect radioQuitRect()   const;
    QRect toggleStartRect() const;
    QRect radioPersRect()   const;
    QRect radioSessRect()   const;
    // History buttons
    QRect histCheckAllRect() const;
    QRect histDeleteRect()   const;
    // Peers sub-tabs
    QRect peersTabRect(int tab)     const;
    QRect peersCheckAllRect()       const;
    QRect peersRemoveRect()         const;

    void browseForFolder();
    void browseForDataFolder();

    // ── Paint helpers ─────────────────────────────────────────────────────────
    void paintSidebar(QPainter &p) const;
    void paintGeneral(QPainter &p)  const;
    void paintWindow(QPainter &p)   const;
    void paintStartup(QPainter &p)  const;
    void paintMessages(QPainter &p) const;
    void paintHistory(QPainter &p)  const;
    void paintPeers(QPainter &p)    const;
    void paintAbout(QPainter &p)    const;

    void paintCard(QPainter &p, int top, int rowCount) const;
    void paintCustomCard(QPainter &p, int top, int h)  const;
    void paintRowDivider(QPainter &p, int y)           const;
    void paintSectionLabel(QPainter &p, int y, const QString &text) const;
    void paintRowLabel(QPainter &p, const QRect &row,
                       const QString &title, const QString &desc = {}) const;
    void paintRadioRow(QPainter &p, const QRect &row,
                       bool checked, bool hovered, const QString &label) const;
    void paintToggleRow(QPainter &p, const QRect &row,
                        bool on, bool hovered,
                        const QString &label, const QString &desc = {}) const;
    void paintActionBtn(QPainter &p, const QRect &r,
                        bool hover, const QString &label,
                        const QColor &bg, const QColor &hoverBg,
                        bool lightText = false) const;
    void paintPeersTab(QPainter &p, int tab,
                       bool selected, bool hovered, const QRect &r) const;

    // ── State ─────────────────────────────────────────────────────────────────
    PeerStore      *m_store        = nullptr;
    Category        m_category     = Category::General;
    PeersTab        m_peersTab     = PeersTab::Saved;
    int             m_scrollY[kCatCount] = {};

    CloseAction     m_closeAction     = CloseAction::MinimizeToTray;
    bool            m_launchAtStartup = false;
    StorageStrategy m_strategy        = StorageStrategy::Persistent;

    // Input fields (General)
    QLineEdit *m_nameEdit;
    QLineEdit *m_dirEdit;
    QLineEdit *m_dataDirEdit;

    // History list
    QScrollArea    *m_histScroll;
    class HistoryListWidget *m_histList;

    // Peers lists
    QScrollArea  *m_savedScroll;
    PeerCheckList *m_savedList;
    QScrollArea  *m_favScroll;
    PeerCheckList *m_favList;
    QScrollArea  *m_blockedScroll;
    PeerCheckList *m_blockedList;

    // ── Hover flags ───────────────────────────────────────────────────────────
    int  m_navHover        = -1;
    bool m_browseHover     = false;
    bool m_browseDataHover = false;
    bool m_minHover        = false;
    bool m_quitHover       = false;
    bool m_startHover      = false;
    bool m_persHover       = false;
    bool m_sessHover       = false;
    bool m_histChkAllHover = false;
    bool m_histDelHover    = false;
    int  m_peersTabHover   = -1;
    bool m_peersChkAllHov  = false;
    bool m_peersRemoveHov  = false;
};
