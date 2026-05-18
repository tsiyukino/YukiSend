#include "ChatView.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>

#include "MediaViewer.h"
#include "PeerItemDelegate.h"
#include "theme/Theme.h"
#include "theme/Fonts.h"

static constexpr int kScrollStep   = 40;
static constexpr int kBtnThreshold = 40; // px above bottom before button appears

ChatView::ChatView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAcceptDrops(true);

    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    m_flashTimer->setInterval(600);
    connect(m_flashTimer, &QTimer::timeout, this, [this] {
        m_flashMsgIdx = -1;
        update();
    });

}

void ChatView::setPeer(const Peer &peer) {
    m_peer = peer;
    m_delegate.clearCache();
    update();
}

void ChatView::setMessages(const QList<Message> &messages) {
    m_messages = messages;
    rebuildLayout();
    scrollToBottom();
    update();
}

void ChatView::appendMessage(const Message &msg) {
    // Remember whether we were at the bottom before the new message arrives.
    const int maxBefore = qMax(0, contentHeight() - (height() - listTop()));
    const bool wasAtBottom = (m_scrollOffset >= maxBefore - kBtnThreshold);

    m_messages.append(msg);
    rebuildLayout();

    if (wasAtBottom)
        scrollToBottom();
    else
        updateScrollButtonState();

    update();
}

void ChatView::updateMessage(const Message &msg) {
    for (auto &m : m_messages) {
        if (m.id != msg.id) continue;
        const bool wasDone = (m.status == MessageStatus::Done);
        m.status           = msg.status;
        m.bytesTransferred = msg.bytesTransferred;
        if (msg.fileSize > 0)        m.fileSize  = msg.fileSize;
        if (!msg.filePath.isEmpty()) m.filePath  = msg.filePath;

        // Invalidate thumbnail cache when a transfer completes
        if (!wasDone && m.status == MessageStatus::Done)
            m_delegate.invalidate(m.id);

        rebuildLayout();
        update();
        return;
    }
}

// ── Layout ────────────────────────────────────────────────────────────────────

void ChatView::rebuildLayout() {
    m_rowOffsets.clear();
    m_rowOffsets.reserve(m_messages.size());
    int y = 0;
    for (const auto &msg : m_messages) {
        m_rowOffsets.append(y);
        y += m_delegate.rowHeight(msg);
    }
}

int ChatView::contentHeight() const {
    if (m_messages.isEmpty()) return 0;
    const int last = m_messages.size() - 1;
    return m_rowOffsets[last] + m_delegate.rowHeight(m_messages[last]);
}

int ChatView::listTop() const { return Theme::Space::ChatHeaderH; }

void ChatView::clampScroll() {
    const int maxScroll = qMax(0, contentHeight() - (height() - listTop()));
    m_scrollOffset = std::clamp(m_scrollOffset, 0, maxScroll);
    updateScrollButtonState();
}

void ChatView::scrollToBottom() {
    m_scrollOffset = qMax(0, contentHeight() - (height() - listTop()));
    updateScrollButtonState();
}

QRect ChatView::scrollButtonRect() const {
    return QRect(width()  - Theme::Space::ScrollBtnMarginR - Theme::Space::ScrollBtnSize,
                 height() - Theme::Space::ScrollBtnMarginB - Theme::Space::ScrollBtnSize,
                 Theme::Space::ScrollBtnSize, Theme::Space::ScrollBtnSize);
}

void ChatView::updateScrollButtonState() {
    const int maxScroll = qMax(0, contentHeight() - (height() - listTop()));
    const bool shouldShow = (maxScroll > 0) &&
                            (m_scrollOffset < maxScroll - kBtnThreshold);
    const double current = m_btnAnim.value(shouldShow ? 1.0 : 0.0);
    const double target  = shouldShow ? 1.0 : 0.0;
    if (qAbs(current - target) > 0.001)
        m_btnAnim.start([this] { update(); }, current, target, 150);
}

int ChatView::messageAt(int y) const {
    const int contentY = (y - listTop()) + m_scrollOffset;
    if (contentY < 0) return -1;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_rowOffsets[i] <= contentY) {
            if (contentY < m_rowOffsets[i] + m_delegate.rowHeight(m_messages[i]))
                return i;
            break;
        }
    }
    return -1;
}

// ── Events ────────────────────────────────────────────────────────────────────

void ChatView::wheelEvent(QWheelEvent *event) {
    m_scrollOffset -= event->angleDelta().y() / 8 * kScrollStep / 15;
    clampScroll();
    update();
}

void ChatView::mouseMoveEvent(QMouseEvent *event) {
    const int idx = messageAt(event->pos().y());
    const int x   = event->pos().x();

    // Update selection if dragging — anchor stays fixed, current end moves
    if (m_selecting && m_selMsgIdx >= 0 && m_selMsgIdx < m_messages.size()) {
        const Message &smsg = m_messages[m_selMsgIdx];
        if (smsg.type == MessageType::Text) {
            const int cur = MessageDelegateRenderer::charAtX(
                smsg, x, textContentLeft(m_selMsgIdx));
            m_selFrom = qMin(m_selAnchor, cur);
            m_selTo   = qMax(m_selAnchor, cur);
            update();
        }
    }

    // Scroll button hover
    const bool btnHit = (m_btnAnim.value(0.0) > 0.0) &&
                        scrollButtonRect().contains(event->pos());
    if (btnHit != m_btnHovered) {
        m_btnHovered = btnHit;
        update();
    }

    // Hamburger button hover (only when a peer is selected)
    const bool hamHit = m_peer.has_value() &&
                        hamburgerBtnRect().contains(event->pos());
    if (hamHit != m_hamburgerHovered) {
        m_hamburgerHovered = hamHit;
        update();
    }

    // Cursor: pointer over the scroll/hamburger button, ibeam over text, arrow otherwise
    if (btnHit || hamHit) {
        setCursor(Qt::PointingHandCursor);
    } else if (idx >= 0 && idx < m_messages.size()
            && m_messages[idx].type == MessageType::Text) {
        setCursor(Qt::IBeamCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    if (idx != m_hoverIndex || x != m_hoverX) {
        m_hoverIndex = idx;
        m_hoverX     = (idx >= 0) ? x : -1;
        update();
    }
}

void ChatView::leaveEvent(QEvent *) {
    m_hoverIndex       = -1;
    m_hoverX           = -1;
    m_btnHovered       = false;
    m_hamburgerHovered = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void ChatView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        m_selecting = false;
}

void ChatView::keyPressEvent(QKeyEvent *event) {
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_C) {
        if (m_selMsgIdx >= 0 && m_selMsgIdx < m_messages.size()
         && m_selFrom < m_selTo) {
            const Message &msg = m_messages[m_selMsgIdx];
            QApplication::clipboard()->setText(
                msg.text.mid(m_selFrom, m_selTo - m_selFrom));
        }
    }
}

// Returns the pixel x where msg.text starts for the message at msgIdx.
int ChatView::textContentLeft(int msgIdx) const {
    if (msgIdx < 0 || msgIdx >= m_messages.size()) return 0;
    const Message &msg = m_messages[msgIdx];
    const int rowTop  = m_rowOffsets[msgIdx] - m_scrollOffset + listTop();
    const QRect rowRect(0, rowTop, width(), m_delegate.rowHeight(msg));

    // Mirror the contentLeft calculation from MessageDelegateRenderer::draw()
    static constexpr int kAvatarColW = MessageDelegate::kAvatarSize + 10;
    if (msg.outgoing) {
        // Outgoing: text is right-aligned. Return the left edge of the text.
        // We need QFontMetrics to find the text width, so compute textX here.
        const QFontMetrics fm(Fonts::regular(Theme::Font::SizeBody));
        const int contentRight = rowRect.right()
                               - MessageDelegate::kRowPadH - kAvatarColW;
        return contentRight - fm.horizontalAdvance(msg.text);
    } else {
        return rowRect.left() + MessageDelegate::kRowPadH + kAvatarColW;
    }
}

void ChatView::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;

    // Scroll-to-bottom button
    if (m_btnAnim.value(0.0) > 0.0 && scrollButtonRect().contains(event->pos())) {
        scrollToBottom();
        update();
        return;
    }

    // Hamburger button → open per-peer settings panel
    if (m_peer.has_value() && hamburgerBtnRect().contains(event->pos())) {
        emit peerPanelRequested();
        return;
    }

    const int idx = messageAt(event->pos().y());
    if (idx < 0 || idx >= m_messages.size()) return;

    const Message &msg  = m_messages[idx];
    const int rowTop    = m_rowOffsets[idx] - m_scrollOffset + listTop();
    const QRect rowRect(0, rowTop, width(), m_delegate.rowHeight(msg));

    const QString senderName = msg.outgoing
        ? QStringLiteral("You")
        : (m_peer.has_value() ? m_peer->displayName : msg.peerId);
    const int hit = m_delegate.hitTestAction(
        rowRect, msg, event->pos().x(), event->pos().y(), senderName);

    // Any click clears existing selection
    m_selMsgIdx = -1;
    m_selFrom   = 0;
    m_selTo     = 0;
    m_selecting = false;

    // Start text selection if clicking on a text message body (not the copy glyph)
    if (msg.type == MessageType::Text && hit == 0) {
        const int charIdx = MessageDelegateRenderer::charAtX(
            msg, event->pos().x(), textContentLeft(idx));
        m_selMsgIdx  = idx;
        m_selAnchor  = charIdx;
        m_selFrom    = charIdx;
        m_selTo      = charIdx;
        m_selecting  = true;
        setFocus();
        update();
        return;
    }

    switch (hit) {
    case  1: emit acceptRequested(msg.id);       break;
    case  3: emit acceptToRequested(msg.id);     break;
    case -1: emit denyRequested(msg.id);         break;
    case  2: emit requestAgainRequested(msg.id); break;
    case  5: {
        // Copy — copy selection if active for this message, else full text
        const QString toCopy = (m_selMsgIdx == idx && m_selFrom < m_selTo)
            ? msg.text.mid(m_selFrom, m_selTo - m_selFrom)
            : msg.text;
        QApplication::clipboard()->setText(toCopy);
        // Flash feedback: accent-color glyph for 600ms
        m_flashMsgIdx = idx;
        m_flashTimer->start();
        update();
        break;
    }
    case  4: {
        // Open image in fullscreen viewer
        if (!m_viewer) {
            m_viewer = new MediaViewer(this);
            m_viewer->setGeometry(rect());
            connect(m_viewer, &MediaViewer::dismissed, this, [this] { update(); });
        }
        QImage full;
        if (!msg.filePath.isEmpty())
            full.load(msg.filePath);
        if (!full.isNull())
            m_viewer->showImage(full);
        break;
    }
    default: break;
    }
}

void ChatView::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_viewer) m_viewer->setGeometry(rect());
    clampScroll();
    update();
}

// ── Painting ──────────────────────────────────────────────────────────────────

QRect ChatView::hamburgerBtnRect() const {
    static constexpr int kHamSize  = 32;
    static constexpr int kHamMarR  = 12;
    const int x = width() - kHamMarR - kHamSize;
    const int y = (Theme::Space::ChatHeaderH - kHamSize) / 2;
    return QRect(x, y, kHamSize, kHamSize);
}

void ChatView::paintHeader(QPainter &p) const {
    p.fillRect(0, 0, width(), Theme::Space::ChatHeaderH, Theme::Color::WindowBg);
    p.fillRect(0, Theme::Space::ChatHeaderH - 1, width(), 1, Theme::Color::Divider);

    if (!m_peer.has_value()) return;

    // --- Hamburger button (right side) ---
    const QRect hbtn = hamburgerBtnRect();
    if (m_hamburgerHovered) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 18));
        p.drawEllipse(hbtn);
        p.restore();
    }
    // Three horizontal lines centred in the button
    {
        QPen linePen(Theme::Color::TextSecondary, 1.5, Qt::SolidLine, Qt::RoundCap);
        p.save();
        p.setPen(linePen);
        const int cx = hbtn.center().x();
        const int cy = hbtn.center().y();
        p.drawLine(cx - 7, cy - 5, cx + 7, cy - 5);
        p.drawLine(cx - 7, cy,     cx + 7, cy);
        p.drawLine(cx - 7, cy + 5, cx + 7, cy + 5);
        p.restore();
    }

    // --- Avatar ---
    const QColor aCol = PeerItemDelegate::avatarColor(m_peer->id);
    const int aSize = 32;
    const int aX    = 16;
    const int aY    = (Theme::Space::ChatHeaderH - aSize) / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(aCol);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawEllipse(aX, aY, aSize, aSize);
    p.setFont(Fonts::semiBold(Theme::Font::SizeCaption));
    p.setPen(Qt::white);
    p.drawText(QRect(aX, aY, aSize, aSize), Qt::AlignCenter,
               PeerItemDelegate::initials(m_peer->displayName));

    // --- Name + IP (truncated to leave room for hamburger button) ---
    const int textX    = aX + aSize + 10;
    const int textMaxW = hbtn.left() - 8 - textX;
    const QFont nameFont = Fonts::semiBold(Theme::Font::SizeBody);
    const QFont ipFont   = Fonts::regular(Theme::Font::SizeCaption);
    const QFontMetrics nameFm(nameFont);
    const QFontMetrics ipFm(ipFont);
    const int lineGap  = 4;
    const int blockH   = nameFm.height() + lineGap + ipFm.height();
    const int blockTop = (Theme::Space::ChatHeaderH - blockH) / 2;

    p.setFont(nameFont);
    p.setPen(Theme::Color::TextPrimary);
    p.drawText(textX, blockTop + nameFm.ascent(),
               nameFm.elidedText(m_peer->displayName, Qt::ElideRight, textMaxW));

    p.setFont(ipFont);
    p.setPen(Theme::Color::TextSecondary);
    p.drawText(textX, blockTop + nameFm.height() + lineGap + ipFm.ascent(),
               ipFm.elidedText(m_peer->address, Qt::ElideRight, textMaxW));
}

void ChatView::paintScrollButton(QPainter &p) const {
    const double opacity = m_btnAnim.value(0.0);
    if (opacity <= 0.0) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(opacity);

    const QRect btn = scrollButtonRect();

    // Drop shadow (medium elevation: 4px offset, 4px blur, 12% black)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 30));
    p.drawEllipse(btn.adjusted(2, 4, 2, 4));

    // Circle fill
    const QColor base = m_btnHovered ? Theme::Color::AccentHover : Theme::Color::Accent;
    p.setBrush(base);
    p.drawEllipse(btn);

    // Chevron-down (three lines forming a ˅ shape)
    const int cx = btn.center().x();
    const int cy = btn.center().y();
    QPen chevronPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(chevronPen);
    p.setBrush(Qt::NoBrush);
    // Left arm: top-left → centre
    p.drawLine(cx - 6, cy - 3, cx, cy + 4);
    // Right arm: centre → top-right
    p.drawLine(cx, cy + 4, cx + 6, cy - 3);

    p.restore();
}

void ChatView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), Theme::Color::WindowBg);

    paintHeader(p);

    if (m_messages.isEmpty()) {
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextSecondary);
        p.drawText(QRect(0, listTop(), width(), height() - listTop()),
                   Qt::AlignCenter, QStringLiteral("No messages yet"));
        paintScrollButton(p);
        return;
    }

    p.setClipRect(0, listTop(), width(), height() - listTop());

    const int visTop    = m_scrollOffset;
    const int visBottom = m_scrollOffset + height() - listTop();

    for (int i = 0; i < m_messages.size(); ++i) {
        const int rowTop = m_rowOffsets[i];
        const int rowH   = m_delegate.rowHeight(m_messages[i]);
        if (rowTop + rowH < visTop)    continue;
        if (rowTop        > visBottom) break;

        const Message &msg = m_messages[i];
        const QString senderName = msg.outgoing
            ? QStringLiteral("You")
            : (m_peer.has_value() ? m_peer->displayName : msg.peerId);
        const QColor avatarCol = msg.outgoing
            ? Theme::Color::Accent
            : PeerItemDelegate::avatarColor(msg.peerId);

        const QRect rowRect(0, rowTop - m_scrollOffset + listTop(), width(), rowH);
        const int hov = (i == m_hoverIndex) ? m_hoverX : -1;

        // Pass active selection only for the message currently being selected
        TextSelection sel;
        if (i == m_selMsgIdx) {
            sel.from = m_selFrom;
            sel.to   = m_selTo;
        }

        m_delegate.draw(&p, rowRect, msg, hov, senderName, avatarCol, sel,
                        i == m_flashMsgIdx);
    }

    // Draw the floating button above the clip region
    p.setClipping(false);
    paintScrollButton(p);

    // Drop overlay with frosted-glass effect (pure QPainter, no QGraphicsEffect)
    if (m_dropActive) {
        p.setRenderHint(QPainter::Antialiasing);

        // Simulate blur: stack semi-transparent white layers at expanding offsets
        // This is a cheap box-blur approximation — no pixel manipulation needed
        const QRect r = rect();
        const QColor frost(255, 255, 255, 18);
        for (int d = 1; d <= 6; ++d)
            p.fillRect(r.adjusted(-d, -d, d, d), frost);

        // Base tint
        p.fillRect(r, QColor(64, 167, 227, 55));
        // Additional white frost pass for the milky look
        p.fillRect(r, QColor(255, 255, 255, 60));

        // Dashed border
        const QRect border = r.adjusted(16, 16, -16, -16);
        QPen dashPen(Theme::Color::Accent, 2, Qt::DashLine);
        dashPen.setDashPattern({6, 4});
        p.setPen(dashPen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(border, 8, 8);

        // Label
        p.setPen(Theme::Color::Accent);
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.drawText(r, Qt::AlignCenter, QStringLiteral("Drop to send"));
    }
}

void ChatView::dragEnterEvent(QDragEnterEvent *event) {
    if (!event->mimeData()->hasUrls()) return;
    event->acceptProposedAction();
    m_dropActive = true;
    update();
}

void ChatView::dragMoveEvent(QDragMoveEvent *event) {
    event->acceptProposedAction();
}

void ChatView::dragLeaveEvent(QDragLeaveEvent *) {
    m_dropActive = false;
    update();
}

void ChatView::dropEvent(QDropEvent *event) {
    m_dropActive = false;
    update();
    if (!event->mimeData()->hasUrls()) return;
    event->acceptProposedAction();
    emit filesDropped(event->mimeData()->urls());
}
