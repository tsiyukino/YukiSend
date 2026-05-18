#include "PeerListWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <algorithm>

#include "PeerItemDelegate.h"
#include "model/UnreadStore.h"
#include "theme/Theme.h"
#include "theme/Fonts.h"

PeerListWidget::PeerListWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_animTimer.setInterval(1000 / kFps);
    connect(&m_animTimer, &QTimer::timeout, this, &PeerListWidget::animateTick);
}

void PeerListWidget::setPeers(const QList<ExtendedPeer> &peers) {
    // Remember which peer was selected so we can detect if it disappeared.
    QString selectedId;
    if (m_selectedIndex >= 0 && m_selectedIndex < m_visible.size())
        selectedId = m_visible[m_selectedIndex].peer.id;

    m_peers      = peers;
    m_hoverIndex = -1;
    m_selectedIndex = -1;
    m_hoverProgress.clear();
    m_selectedProgress.clear();
    rebuildVisible();

    if (!selectedId.isEmpty()) {
        bool found = false;
        for (int i = 0; i < m_visible.size(); ++i) {
            if (m_visible[i].peer.id == selectedId) {
                m_selectedIndex = i;
                found = true;
                break;
            }
        }
        if (!found)
            emit peerDeselected();
    }

    update();
}

void PeerListWidget::setUnreadStore(const UnreadStore *store) {
    m_unread = store;
    update();
}

void PeerListWidget::clearUnread(const QString &peerId) {
    if (m_unread)
        const_cast<UnreadStore *>(m_unread)->clear(peerId);
    update();
}

void PeerListWidget::setFilter(const QString &query) {
    m_filter        = query.trimmed().toLower();
    m_hoverIndex    = -1;
    m_selectedIndex = -1;
    m_hoverProgress.clear();
    m_selectedProgress.clear();
    rebuildVisible();
    update();
}

void PeerListWidget::rebuildVisible() {
    // Sort: favorites first (online favorites, then offline favorites),
    //       then online non-favorites, then offline non-favorites.
    // Each group is alphabetical by display name.
    QList<ExtendedPeer> sorted = m_peers;
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const ExtendedPeer &a, const ExtendedPeer &b) {
            // Favorites before non-favorites
            if (a.isFavorite != b.isFavorite) return a.isFavorite > b.isFavorite;
            // Online before offline within same favorite tier
            if (a.isOnline != b.isOnline)     return a.isOnline > b.isOnline;
            // Alphabetical within same tier
            return a.peer.displayName.toLower() < b.peer.displayName.toLower();
        });

    if (m_filter.isEmpty()) {
        m_visible = sorted;
        return;
    }

    m_visible.clear();
    for (const auto &ep : sorted) {
        if (ep.peer.displayName.toLower().contains(m_filter)
            || ep.peer.address.contains(m_filter))
            m_visible.append(ep);
    }
}

static double step(double current, double target, double speed) {
    if (current < target) return qMin(current + speed, target);
    if (current > target) return qMax(current - speed, target);
    return current;
}

void PeerListWidget::animateTick() {
    bool stillAnimating = false;

    for (int i = 0; i < m_visible.size(); ++i) {
        const double hTarget = (i == m_hoverIndex && i != m_selectedIndex) ? 1.0 : 0.0;
        const double sTarget = (i == m_selectedIndex) ? 1.0 : 0.0;

        double &hp = m_hoverProgress[i];
        double &sp = m_selectedProgress[i];

        const double newHp = step(hp, hTarget, kHoverSpeed);
        const double newSp = step(sp, sTarget, kSelectSpeed);

        if (newHp != hp || newSp != sp) {
            hp = newHp;
            sp = newSp;
            stillAnimating = true;
        }
    }

    update();

    if (!stillAnimating)
        m_animTimer.stop();
}

void PeerListWidget::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), Theme::Color::SidebarBg);

    if (m_visible.isEmpty()) {
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextSecondary);
        const QString msg = m_filter.isEmpty()
            ? QStringLiteral("No devices found")
            : QStringLiteral("No results");
        p.drawText(rect(), Qt::AlignCenter, msg);
        return;
    }

    for (int i = 0; i < m_visible.size(); ++i) {
        const QRect r = itemRect(i);
        if (!event->rect().intersects(r))
            continue;

        const double hp = m_hoverProgress.value(i, 0.0);
        const double sp = m_selectedProgress.value(i, 0.0);
        const auto  &ep = m_visible[i];

        const int unread = m_unread ? m_unread->count(ep.peer.id) : 0;
        PeerItemDelegate::draw(&p, r, ep.peer, hp, sp, unread, ep.isOnline);
    }
}

void PeerListWidget::mouseMoveEvent(QMouseEvent *event) {
    const int idx = itemAt(event->pos());
    if (idx != m_hoverIndex) {
        m_hoverIndex = idx;
        m_animTimer.start();
    }
}

void PeerListWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const int idx = itemAt(event->pos());
    if (idx < 0 || idx >= m_visible.size()) return;
    if (idx == m_selectedIndex) {
        m_selectedIndex = -1;
        m_animTimer.start();
        emit peerDeselected();
    } else {
        m_selectedIndex = idx;
        m_animTimer.start();
        emit peerSelected(m_visible[idx].peer);
    }
}

void PeerListWidget::leaveEvent(QEvent *) {
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        m_animTimer.start();
    }
}

int PeerListWidget::itemAt(const QPoint &pos) const {
    const int idx = pos.y() / Theme::Space::ItemHeight;
    if (idx < 0 || idx >= m_visible.size()) return -1;
    return idx;
}

QRect PeerListWidget::itemRect(int index) const {
    return QRect(0, index * Theme::Space::ItemHeight, width(), Theme::Space::ItemHeight);
}
