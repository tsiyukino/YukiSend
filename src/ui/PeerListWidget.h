#pragma once

#include <QWidget>
#include <QList>
#include <QMap>
#include <QTimer>

#include "network/Discovery.h"

class UnreadStore;

// A Peer decorated with display-layer flags.
// The widget does not talk to PeerStore directly; the caller merges and passes these.
struct ExtendedPeer {
    Peer peer;
    bool isOnline   = true;
    bool isFavorite = false;
};

class PeerListWidget : public QWidget {
    Q_OBJECT
public:
    explicit PeerListWidget(QWidget *parent = nullptr);

    // Replace the full peer list (online + saved-offline, pre-filtered for blocked).
    // Favorites are sorted to the top internally; offline peers appear dimmed.
    void setPeers(const QList<ExtendedPeer> &peers);

    void setFilter(const QString &query);
    void setUnreadStore(const UnreadStore *store);
    void clearUnread(const QString &peerId);

signals:
    void peerSelected(const Peer &peer);
    void peerDeselected();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void rebuildVisible();
    void animateTick();
    int  itemAt(const QPoint &pos) const;
    QRect itemRect(int index) const;
    void nudge(int idx, double target);

    QList<ExtendedPeer>   m_peers;
    QList<ExtendedPeer>   m_visible;
    QString               m_filter;
    const UnreadStore    *m_unread = nullptr;

    int m_hoverIndex    = -1;
    int m_selectedIndex = -1;

    QMap<int, double> m_hoverProgress;
    QMap<int, double> m_selectedProgress;

    QTimer m_animTimer;
    static constexpr int    kFps         = 60;
    static constexpr double kHoverSpeed  = 1.0 / (0.15 * kFps);
    static constexpr double kSelectSpeed = 1.0 / (0.12 * kFps);
};
