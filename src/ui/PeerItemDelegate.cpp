#include "PeerItemDelegate.h"

#include <QFontMetrics>

#include "theme/Theme.h"
#include "theme/Fonts.h"

namespace PeerItemDelegate {

static const QColor kAvatarColors[] = {
    { 198,  55,  55 },
    { 210, 105,  30 },
    {  41, 128, 185 },
    {  39, 174,  96 },
    { 142,  68, 173 },
    {  22, 160, 133 },
    { 211,  84,   0 },
    {  52,  73,  94 },
};

QColor avatarColor(const QString &id) {
    uint hash = 0;
    for (const QChar ch : id)
        hash = hash * 31 + ch.unicode();
    return kAvatarColors[hash % std::size(kAvatarColors)];
}

QString initials(const QString &displayName) {
    const QStringList parts = displayName.trimmed().split(u' ', Qt::SkipEmptyParts);
    if (parts.isEmpty())   return QStringLiteral("?");
    if (parts.size() == 1) return parts[0].left(1).toUpper();
    return (parts[0].left(1) + parts[1].left(1)).toUpper();
}

// Linearly interpolate between two QColors.
static QColor lerp(const QColor &a, const QColor &b, double t) {
    return QColor(
        int(a.red()   + (b.red()   - a.red())   * t),
        int(a.green() + (b.green() - a.green()) * t),
        int(a.blue()  + (b.blue()  - a.blue())  * t)
    );
}

void draw(QPainter *painter, const QRect &rect, const Peer &peer,
          double hoverProgress, double selectedProgress,
          int unreadCount, bool isOnline)
{
    static constexpr int kBadgeH    = 20;  // pill height
    static constexpr int kBadgeMinW = 20;  // minimum width (single digit → circle)
    static constexpr int kBadgePadH =  4;  // horizontal text padding inside pill
    static constexpr int kBadgePadR = 12;  // right margin from item edge
    static constexpr int kBadgeFont = 11;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    // --- Background ---
    // Layer: normal → hover → selected, each cross-fading independently.
    // First blend hover over normal, then blend selected over that.
    if (hoverProgress > 0.0) {
        QColor bg = lerp(Theme::Color::SidebarBg, Theme::Color::ItemHover, hoverProgress);
        painter->fillRect(rect, bg);
    }
    if (selectedProgress > 0.0) {
        QColor selBg = Theme::Color::ItemSelected;
        selBg.setAlphaF(selectedProgress);
        painter->fillRect(rect, selBg);
    }

    // --- Avatar ---
    const int avatarSize = Theme::Space::AvatarSize;
    const int avatarX    = rect.left() + Theme::Space::ItemPadLeft;
    const int avatarY    = rect.top()  + (rect.height() - avatarSize) / 2;

    // Offline peers show a desaturated grey avatar.
    const QColor baseAvatarColor = isOnline
        ? avatarColor(peer.id)
        : QColor(180, 180, 180);
    painter->setBrush(baseAvatarColor);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(avatarX, avatarY, avatarSize, avatarSize);

    painter->setFont(Fonts::semiBold(Theme::Font::SizeCaption));
    painter->setPen(isOnline ? Qt::white : QColor(255, 255, 255, 180));
    painter->drawText(QRect(avatarX, avatarY, avatarSize, avatarSize),
                      Qt::AlignCenter, initials(peer.displayName));

    // --- Text ---
    // Reserve space on the right for the unread badge when present.
    static constexpr int kBadgeMaxW = 36;  // fits "99+" at 11px semibold with padding
    const int badgeReserve = unreadCount > 0 ? kBadgeMaxW + kBadgePadR : 0;
    const int textLeft  = rect.left() + Theme::Space::TextLeft;
    const int textRight = rect.right() - Theme::Space::ItemPadLeft - badgeReserve;
    const int textW     = textRight - textLeft;

    // Offline peers use a dimmer text color as base.
    const QColor basePrimary   = isOnline ? Theme::Color::TextPrimary   : Theme::Color::TextSecondary;
    const QColor baseSecondary = isOnline ? Theme::Color::TextSecondary : QColor(180, 180, 180);

    // Interpolate text colors: normal/hover → selected (white)
    const QColor nameColor = lerp(
        basePrimary,
        Theme::Color::TextOnAccent,
        selectedProgress
    );
    const QColor captionColor = lerp(
        baseSecondary,
        Theme::Color::TextOnAccent,
        selectedProgress
    );

    // Name — Inter Medium
    const QFont nameFont = Fonts::medium(Theme::Font::SizeBody);
    const QFontMetrics nameFm(nameFont);
    painter->setFont(nameFont);
    painter->setPen(nameColor);
    painter->drawText(
        textLeft,
        rect.top() + Theme::Space::NameTop + nameFm.ascent(),
        nameFm.elidedText(peer.displayName, Qt::ElideRight, textW)
    );

    // Subtitle — Inter Regular ("Offline" when not reachable, IP otherwise)
    const QFont captionFont = Fonts::regular(Theme::Font::SizeCaption);
    const QFontMetrics captionFm(captionFont);
    const QString subtitle = isOnline
        ? peer.address
        : QStringLiteral("Offline · ") + peer.address;
    painter->setFont(captionFont);
    painter->setPen(captionColor);
    painter->drawText(
        textLeft,
        rect.top() + Theme::Space::SubtitleTop + captionFm.ascent(),
        captionFm.elidedText(subtitle, Qt::ElideRight, textW)
    );

    // --- Unread badge ---
    if (unreadCount > 0) {
        const QString label = unreadCount > 99
            ? QStringLiteral("99+")
            : QString::number(unreadCount);

        // Measure text to decide pill width; minimum is a circle.
        const QFont badgeFont = Fonts::semiBold(kBadgeFont);
        const int textW_b = QFontMetrics(badgeFont).horizontalAdvance(label);
        const int pillW   = qMax(kBadgeMinW, textW_b + kBadgePadH * 2);

        // Right edge of item, vertically centered.
        const int bx = rect.right() - kBadgePadR - pillW;
        const int by = rect.top()   + (rect.height() - kBadgeH) / 2;

        // Badge background: white on selected (accent bg), accent on normal.
        const QColor badgeBg = selectedProgress > 0.5
            ? Qt::white
            : Theme::Color::Accent;
        const QColor badgeFg = selectedProgress > 0.5
            ? Theme::Color::Accent
            : Qt::white;

        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeBg);
        painter->drawRoundedRect(bx, by, pillW, kBadgeH, kBadgeH / 2, kBadgeH / 2);

        painter->setFont(badgeFont);
        painter->setPen(badgeFg);
        painter->drawText(QRect(bx, by, pillW, kBadgeH), Qt::AlignCenter, label);
    }

    painter->restore();
}

} // namespace PeerItemDelegate
