#pragma once

#include <QPainter>
#include <QRect>
#include <QColor>
#include <QString>

#include "network/Discovery.h"

namespace PeerItemDelegate {

// hoverProgress:    0.0 = normal, 1.0 = fully hovered
// selectedProgress: 0.0 = normal, 1.0 = fully selected
// isOnline:         false → avatar rendered grey and name dimmed
// Both progress values are interpolated independently for cross-fade.
void draw(QPainter *painter, const QRect &rect, const Peer &peer,
          double hoverProgress, double selectedProgress,
          int unreadCount = 0, bool isOnline = true);

QString initials(const QString &displayName);
QColor  avatarColor(const QString &id);

} // namespace PeerItemDelegate
