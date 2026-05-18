#include "PeerPanel.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPropertyAnimation>

#include "PeerItemDelegate.h"
#include "theme/Theme.h"
#include "theme/Fonts.h"

static constexpr int kToggleW  = 36;
static constexpr int kToggleH  = 20;
static constexpr int kRadioR   =  8;  // radio button outer radius
static constexpr int kPadL     = Theme::Space::PanelPadH;
static constexpr int kPadR     = Theme::Space::PanelPadH;

PeerPanel::PeerPanel(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    m_anim = new QPropertyAnimation(this, "pos", this);
    m_anim->setDuration(200);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);

    hide();
}

void PeerPanel::openFor(const Peer &peer, PeerStore *store, StorageStrategy strategy) {
    m_peer     = peer;
    m_store    = store;
    m_saved    = store->isSaved(peer.id);
    m_favorite = store->isFavorite(peer.id);
    m_blocked  = store->isBlocked(peer.id);
    m_strategy = strategy;

    // Reset hover state
    m_closeHover    = false;
    m_savedHover    = false;
    m_favoriteHover = false;
    m_blockedHover  = false;
    m_persistHover  = false;
    m_sessionHover  = false;

    update();
}

void PeerPanel::slideIn() {
    if (m_open) return;
    m_open = true;

    const int hiddenX  = parentWidget()->width();
    const int shownX   = parentWidget()->width() - kW;

    m_anim->stop();
    move(hiddenX, 0);
    resize(kW, parentWidget()->height());
    show();
    raise();

    m_anim->setStartValue(QPoint(hiddenX, 0));
    m_anim->setEndValue(QPoint(shownX, 0));
    m_anim->start();
}

void PeerPanel::slideOut() {
    if (!m_open) return;
    m_open = false;

    const int hiddenX = parentWidget()->width();
    const int shownX  = parentWidget()->width() - kW;

    m_anim->stop();
    // Disconnect any prior finished handler before connecting a fresh one.
    disconnect(m_anim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_anim, &QPropertyAnimation::finished, this, &PeerPanel::hide,
            Qt::SingleShotConnection);

    m_anim->setStartValue(QPoint(shownX, 0));
    m_anim->setEndValue(QPoint(hiddenX, 0));
    m_anim->start();

    emit closed();
}

// ── Geometry helpers ──────────────────────────────────────────────────────────

QRect PeerPanel::closeRect() const {
    static constexpr int kSz = 32;
    return QRect(width() - kPadR - kSz,
                 (Theme::Space::PeerPanelHeaderH - kSz) / 2, kSz, kSz);
}

// Rows start below the header.
// Order: saved, favorite, blocked, then section label, persist, session.
static int rowY(int index) {
    using namespace Theme::Space;
    // Layout: header | saved | favorite | blocked | gap+label | persist | session
    if (index < 3)
        return PeerPanelHeaderH + 1 + index * PeerPanelRowH;  // +1 for top divider
    if (index == 3)  // section label
        return PeerPanelHeaderH + 1 + 3 * PeerPanelRowH + 8;
    // indices 4,5 = radio rows
    return PeerPanelHeaderH + 1 + 3 * PeerPanelRowH + 8 + PeerPanelLabelH + (index - 4) * PeerPanelRowH;
}

QRect PeerPanel::savedRect()    const { return QRect(0, rowY(0), width(), Theme::Space::PeerPanelRowH); }
QRect PeerPanel::favoriteRect() const { return QRect(0, rowY(1), width(), Theme::Space::PeerPanelRowH); }
QRect PeerPanel::blockedRect()  const { return QRect(0, rowY(2), width(), Theme::Space::PeerPanelRowH); }
QRect PeerPanel::persistRect()  const { return QRect(0, rowY(4), width(), Theme::Space::PeerPanelRowH); }
QRect PeerPanel::sessionRect()  const { return QRect(0, rowY(5), width(), Theme::Space::PeerPanelRowH); }

// ── Painting ──────────────────────────────────────────────────────────────────

void PeerPanel::paintSectionLabel(QPainter &p, int y, const QString &text) const {
    p.setFont(Fonts::semiBold(Theme::Font::SizeCaption));
    p.setPen(Theme::Color::TextSecondary);
    p.drawText(QRect(kPadL, y, width() - kPadL - kPadR, Theme::Space::PeerPanelLabelH),
               Qt::AlignVCenter | Qt::AlignLeft,
               text.toUpper());
}

void PeerPanel::paintToggleRow(QPainter &p, const QRect &row,
                               bool on, bool hovered, const QString &label) const {
    if (hovered) {
        p.fillRect(row, Theme::Color::ItemHover);
    }

    // Label on the left
    p.setFont(Fonts::regular(Theme::Font::SizeBody));
    p.setPen(Theme::Color::TextPrimary);
    const int textMaxW = row.width() - kPadL - kPadR - kToggleW - 12;
    p.drawText(QRect(row.left() + kPadL, row.top(), textMaxW, row.height()),
               Qt::AlignVCenter | Qt::AlignLeft, label);

    // Toggle pill on the right
    const int tx = row.right() - kPadR - kToggleW;
    const int ty = row.top()  + (row.height() - kToggleH) / 2;
    const QColor pillColor = on ? Theme::Color::Accent : QColor(200, 200, 200);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(pillColor);
    p.drawRoundedRect(tx, ty, kToggleW, kToggleH, kToggleH / 2, kToggleH / 2);
    // Knob
    const int knobD = kToggleH - 4;
    const int knobX = on ? tx + kToggleW - 2 - knobD : tx + 2;
    p.setBrush(Qt::white);
    p.drawEllipse(knobX, ty + 2, knobD, knobD);
    p.restore();

    // Bottom divider
    p.fillRect(row.left() + kPadL, row.bottom(), row.width() - kPadL, 1,
               Theme::Color::Divider);
}

void PeerPanel::paintRadioRow(QPainter &p, const QRect &row,
                              bool checked, bool hovered, const QString &label) const {
    if (hovered) {
        p.fillRect(row, Theme::Color::ItemHover);
    }

    // Radio circle on the left
    const int rX = row.left() + kPadL;
    const int rY = row.top()  + (row.height() - kRadioR * 2) / 2;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(checked ? Theme::Color::Accent : Theme::Color::Divider, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(rX, rY, kRadioR * 2, kRadioR * 2);
    if (checked) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Color::Accent);
        p.drawEllipse(rX + 4, rY + 4, kRadioR * 2 - 8, kRadioR * 2 - 8);
    }
    p.restore();

    // Label
    p.setFont(Fonts::regular(Theme::Font::SizeBody));
    p.setPen(Theme::Color::TextPrimary);
    p.drawText(QRect(rX + kRadioR * 2 + 10, row.top(), row.width(), row.height()),
               Qt::AlignVCenter | Qt::AlignLeft, label);
}

void PeerPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Panel background
    p.fillRect(rect(), Theme::Color::WindowBg);

    // Left-edge shadow
    const int shadowW = 8;
    QLinearGradient shadow(0, 0, shadowW, 0);
    shadow.setColorAt(0, QColor(0, 0, 0, 40));
    shadow.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(0, 0, shadowW, height(), shadow);

    // ── Header ────────────────────────────────────────────────────────────────
    // Avatar
    const int aSize = 36;
    const int aX    = kPadL;
    const int aY    = (Theme::Space::PeerPanelHeaderH - aSize) / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(PeerItemDelegate::avatarColor(m_peer.id));
    p.drawEllipse(aX, aY, aSize, aSize);
    p.setFont(Fonts::semiBold(Theme::Font::SizeCaption));
    p.setPen(Qt::white);
    p.drawText(QRect(aX, aY, aSize, aSize), Qt::AlignCenter,
               PeerItemDelegate::initials(m_peer.displayName));

    // Peer name
    const int nameX = aX + aSize + 10;
    const int nameMW = closeRect().left() - 8 - nameX;
    p.setFont(Fonts::semiBold(Theme::Font::SizeBody));
    p.setPen(Theme::Color::TextPrimary);
    const QFontMetrics nameFm(p.font());
    p.drawText(nameX, (Theme::Space::PeerPanelHeaderH + nameFm.ascent() - nameFm.descent()) / 2,
               nameFm.elidedText(m_peer.displayName, Qt::ElideRight, nameMW));

    // Close button (×)
    const QRect cr = closeRect();
    if (m_closeHover) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Color::ItemHover);
        p.drawEllipse(cr);
    }
    QPen closePen(Theme::Color::TextSecondary, 1.5, Qt::SolidLine, Qt::RoundCap);
    p.setPen(closePen);
    const int cx = cr.center().x(), cy = cr.center().y(), cd = 6;
    p.drawLine(cx - cd, cy - cd, cx + cd, cy + cd);
    p.drawLine(cx + cd, cy - cd, cx - cd, cy + cd);

    // Header bottom divider
    p.fillRect(0, Theme::Space::PeerPanelHeaderH, width(), 1, Theme::Color::Divider);

    // ── Toggle rows ───────────────────────────────────────────────────────────
    paintToggleRow(p, savedRect(),    m_saved,    m_savedHover,    QStringLiteral("Keep after disconnect"));
    paintToggleRow(p, favoriteRect(), m_favorite, m_favoriteHover, QStringLiteral("Favorite"));
    paintToggleRow(p, blockedRect(),  m_blocked,  m_blockedHover,  QStringLiteral("Block"));

    // ── Strategy section ──────────────────────────────────────────────────────
    const int labelY = rowY(3);
    // Divider above section
    p.fillRect(0, labelY - 8, width(), 1, Theme::Color::Divider);
    paintSectionLabel(p, labelY, QStringLiteral("Message history"));
    paintRadioRow(p, persistRect(),
                  m_strategy == StorageStrategy::Persistent,
                  m_persistHover, QStringLiteral("Keep messages"));
    paintRadioRow(p, sessionRect(),
                  m_strategy == StorageStrategy::SessionOnly,
                  m_sessionHover, QStringLiteral("Delete on disconnect"));
}

// ── Input ──────────────────────────────────────────────────────────────────────

void PeerPanel::mouseMoveEvent(QMouseEvent *event) {
    const QPoint pos = event->pos();

    const bool ch = closeRect().contains(pos);
    const bool sh = savedRect().contains(pos);
    const bool fh = favoriteRect().contains(pos);
    const bool bh = blockedRect().contains(pos);
    const bool ph = persistRect().contains(pos);
    const bool seh = sessionRect().contains(pos);

    if (ch != m_closeHover || sh != m_savedHover || fh != m_favoriteHover
        || bh != m_blockedHover || ph != m_persistHover || seh != m_sessionHover)
    {
        m_closeHover    = ch;
        m_savedHover    = sh;
        m_favoriteHover = fh;
        m_blockedHover  = bh;
        m_persistHover  = ph;
        m_sessionHover  = seh;
        setCursor((ch || sh || fh || bh || ph || seh)
                  ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void PeerPanel::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const QPoint pos = event->pos();

    if (closeRect().contains(pos)) {
        slideOut();
        return;
    }
    if (savedRect().contains(pos)) {
        m_saved = !m_saved;
        if (m_store)
            m_store->setSaved(m_peer.id, m_peer.displayName,
                              m_peer.address, m_peer.port, m_saved);
        emit savedChanged(m_peer.id, m_saved);
        update();
        return;
    }
    if (favoriteRect().contains(pos)) {
        m_favorite = !m_favorite;
        if (m_store)
            m_store->setFavorite(m_peer.id, m_favorite);
        emit favoriteChanged(m_peer.id, m_favorite);
        // Favoriting implies saving
        if (m_favorite && !m_saved) {
            m_saved = true;
            emit savedChanged(m_peer.id, true);
        }
        update();
        return;
    }
    if (blockedRect().contains(pos)) {
        m_blocked = !m_blocked;
        if (m_store)
            m_store->setBlocked(m_peer.id, m_blocked);
        emit blockedChanged(m_peer.id, m_blocked);
        update();
        return;
    }
    if (persistRect().contains(pos)) {
        m_strategy = StorageStrategy::Persistent;
        emit strategyChanged(m_peer.id, m_strategy);
        update();
        return;
    }
    if (sessionRect().contains(pos)) {
        m_strategy = StorageStrategy::SessionOnly;
        emit strategyChanged(m_peer.id, m_strategy);
        update();
        return;
    }
}

void PeerPanel::leaveEvent(QEvent *) {
    m_closeHover    = false;
    m_savedHover    = false;
    m_favoriteHover = false;
    m_blockedHover  = false;
    m_persistHover  = false;
    m_sessionHover  = false;
    setCursor(Qt::ArrowCursor);
    update();
}
