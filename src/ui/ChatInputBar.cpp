#include "ChatInputBar.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QFontMetrics>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>

#include "theme/Theme.h"
#include "theme/Fonts.h"

static constexpr int kBarH    = Theme::Space::InputBarHeight;
static constexpr int kPadH    = Theme::Space::PaddingL;  // 16px — horizontal edge padding
static constexpr int kBtnW    = Theme::Space::InputBtnW;
static constexpr int kBtnH    = Theme::Space::InputBtnH;
static constexpr int kBtnGap  = Theme::Space::PaddingM;  // 8px — gap between File/Folder buttons
static constexpr int kHoverMs = 120;

// Image compose mode dimensions
static constexpr int kThumbSize   = 36; // preview thumbnail height in the bar
static constexpr int kCancelSize  = 16; // ✕ button size
static constexpr int kThumbRadius =  4;

ChatInputBar::ChatInputBar(QWidget *parent)
    : QWidget(parent)
    , m_focusAnim(this)
    , m_fileAnim(this)
    , m_folderAnim(this)
{
    setFixedHeight(kBarH);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
}

QString ChatInputBar::text() const { return m_text; }
void    ChatInputBar::clear() { updateText({}); }

// ── Geometry helpers ──────────────────────────────────────────────────────────

QRect ChatInputBar::folderButtonRect() const {
    return QRect(width() - kPadH - kBtnW, (kBarH - kBtnH) / 2, kBtnW, kBtnH);
}

QRect ChatInputBar::fileButtonRect() const {
    return QRect(folderButtonRect().left() - kBtnGap - kBtnW,
                 (kBarH - kBtnH) / 2, kBtnW, kBtnH);
}

QRect ChatInputBar::inputRect() const {
    return QRect(kPadH, 0, fileButtonRect().left() - kPadH - 8, kBarH);
}

// Thumbnail shown in compose mode — left side of bar
QRect ChatInputBar::imagePreviewRect() const {
    return QRect(kPadH, (kBarH - kThumbSize) / 2, kThumbSize, kThumbSize);
}

// ✕ cancel button — sits to the right of the thumbnail
QRect ChatInputBar::cancelButtonRect() const {
    const QRect thumb = imagePreviewRect();
    return QRect(thumb.right() + 8, thumb.top() + (kThumbSize - kCancelSize) / 2,
                 kCancelSize, kCancelSize);
}

// ── Compose mode helpers ──────────────────────────────────────────────────────

void ChatInputBar::enterImageCompose(const QImage &image) {
    m_pendingImage = image;
    m_cancelHover  = false;
    update();
}

void ChatInputBar::cancelImageCompose() {
    m_pendingImage = QImage();
    m_cancelHover  = false;
    update();
}

// ── Selection helpers ─────────────────────────────────────────────────────────

void ChatInputBar::moveCursor(int pos, bool resetSel) {
    m_cursorPos = qBound(0, pos, m_text.size());
    if (resetSel) m_selStart = m_cursorPos;
    update();
}

int ChatInputBar::charAtX(int x) const {
    const QFontMetrics fm(Fonts::regular(Theme::Font::SizeBody));
    const int relX = x - inputRect().left();
    if (relX <= 0) return 0;
    for (int i = 1; i <= m_text.size(); ++i)
        if (fm.horizontalAdvance(m_text.left(i)) > relX) return i - 1;
    return m_text.size();
}

void ChatInputBar::deleteSelection() {
    if (!hasSelection()) return;
    QString t = m_text;
    t.remove(selFrom(), selTo() - selFrom());
    m_cursorPos = selFrom();
    m_selStart  = m_cursorPos;
    updateText(t);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void ChatInputBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    p.fillRect(rect(), Theme::Color::WindowBg);

    // Top border: static hairline, no colour change on focus
    p.fillRect(0, 0, width(), 1, Theme::Color::Divider);

    if (!m_pendingImage.isNull()) {
        // ── Image compose mode ────────────────────────────────────────────────

        // Thumbnail
        const QRect thumb = imagePreviewRect();
        {
            QPainterPath clip;
            clip.addRoundedRect(thumb, kThumbRadius, kThumbRadius);
            p.save();
            p.setClipPath(clip);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(thumb, m_pendingImage.scaled(
                thumb.size() * 2, Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation));
            p.restore();
        }

        // ✕ cancel button
        const QRect cancel = cancelButtonRect();
        const QColor cancelBg = m_cancelHover
            ? QColor(200, 60, 60)
            : QColor(160, 160, 160);
        p.setPen(Qt::NoPen);
        p.setBrush(cancelBg);
        p.drawEllipse(cancel);
        p.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap));
        const int m = 4;
        p.drawLine(cancel.left() + m, cancel.top() + m,
                   cancel.right() - m, cancel.bottom() - m);
        p.drawLine(cancel.right() - m, cancel.top() + m,
                   cancel.left() + m, cancel.bottom() - m);

        // Label to the right of cancel button
        const int labelX = cancel.right() + 10;
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(Theme::Color::Placeholder);
        p.drawText(QRect(labelX, 0, fileButtonRect().left() - labelX - 8, kBarH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Press Enter to send image, Esc to cancel"));

    } else {
        // ── Normal text input mode ────────────────────────────────────────────

        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        const QRect ir = inputRect();
        if (m_text.isEmpty() && m_preedit.isEmpty()) {
            p.setPen(Theme::Color::Placeholder);
            p.drawText(ir, Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("Send a message..."));
        } else {
            const QFontMetrics fm(p.font());
            const int textX = ir.left();
            const int textY = ir.top() + (kBarH + fm.ascent() - fm.descent()) / 2;

            p.setClipRect(ir);

            // Selection highlight behind text
            if (hasSelection()) {
                const int x0 = textX + fm.horizontalAdvance(m_text.left(selFrom()));
                const int x1 = textX + fm.horizontalAdvance(m_text.left(selTo()));
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::Color::Accent.lighter(170));
                p.drawRect(QRect(x0, textY - fm.ascent(),
                                 x1 - x0, fm.ascent() + fm.descent()));
            }

            p.setPen(Theme::Color::TextPrimary);
            p.drawText(textX, textY, m_text);

            // Preedit (IME composition) drawn after committed text at cursor
            if (!m_preedit.isEmpty()) {
                const int preeditX = textX + fm.horizontalAdvance(m_text.left(m_cursorPos));
                p.drawText(preeditX, textY, m_preedit);
                // Underline to indicate it's not yet committed
                const int pw = fm.horizontalAdvance(m_preedit);
                p.setPen(QPen(Theme::Color::TextPrimary, 1));
                p.drawLine(preeditX, textY + fm.descent() - 1,
                           preeditX + pw, textY + fm.descent() - 1);
            }

            p.setClipping(false);
        }

        if (m_focused && m_preedit.isEmpty()) {
            const QFontMetrics fm(Fonts::regular(Theme::Font::SizeBody));
            const int cx = inputRect().left()
                         + fm.horizontalAdvance(m_text.left(m_cursorPos));
            p.setPen(QPen(Theme::Color::TextPrimary, 1.0));
            p.drawLine(cx, kBarH / 2 - 8, cx, kBarH / 2 + 8);
        }
    }

    // Vertical separator between text input and action buttons
    {
        const int sepX = fileButtonRect().left() - 8;
        const int sepY1 = (kBarH - 20) / 2;
        const int sepY2 = sepY1 + 20;
        p.setPen(QPen(Theme::Color::Divider, 1));
        p.drawLine(sepX, sepY1, sepX, sepY2);
    }

    // File / Folder buttons: secondary → primary on hover (no blue)
    auto drawBtn = [&](const QRect &r, double hov, const QString &label) {
        const QColor a = Theme::Color::TextSecondary;
        const QColor b = Theme::Color::TextPrimary;
        p.setFont(Fonts::medium(Theme::Font::SizeCaption));
        p.setPen(QColor(int(a.red()   + (b.red()   - a.red())   * hov),
                        int(a.green() + (b.green() - a.green()) * hov),
                        int(a.blue()  + (b.blue()  - a.blue())  * hov)));
        p.drawText(r, Qt::AlignCenter, label);
    };
    drawBtn(fileButtonRect(),   m_fileAnim.value(m_fileHover   ? 1.0 : 0.0),
            QStringLiteral("File"));
    drawBtn(folderButtonRect(), m_folderAnim.value(m_folderHover ? 1.0 : 0.0),
            QStringLiteral("Folder"));
}

// ── Mouse events ──────────────────────────────────────────────────────────────

void ChatInputBar::mouseMoveEvent(QMouseEvent *event) {
    if (!m_pendingImage.isNull()) {
        const bool ch = cancelButtonRect().contains(event->pos());
        if (ch != m_cancelHover) {
            m_cancelHover = ch;
            setCursor(ch ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
        return;
    }

    // Extend selection while dragging in the input area
    if (m_dragging && m_focused) {
        m_cursorPos = charAtX(event->pos().x());
        update();
        return;
    }

    const bool fh  = fileButtonRect().contains(event->pos());
    const bool flh = folderButtonRect().contains(event->pos());
    const bool ih  = inputRect().contains(event->pos());

    if (fh != m_fileHover) {
        m_fileHover = fh;
        setCursor(fh ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_fileAnim.start([this] { update(); }, fh ? 0.0 : 1.0,
                         fh ? 1.0 : 0.0, kHoverMs);
    }
    if (flh != m_folderHover) {
        m_folderHover = flh;
        if (!fh) setCursor(flh ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_folderAnim.start([this] { update(); }, flh ? 0.0 : 1.0,
                           flh ? 1.0 : 0.0, kHoverMs);
    }
    if (!fh && !flh)
        setCursor(ih ? Qt::IBeamCursor : Qt::ArrowCursor);
}

void ChatInputBar::leaveEvent(QEvent *) {
    if (m_cancelHover) { m_cancelHover = false; update(); }
    if (m_fileHover) {
        m_fileHover = false;
        m_fileAnim.start([this] { update(); }, 1.0, 0.0, kHoverMs);
    }
    if (m_folderHover) {
        m_folderHover = false;
        m_folderAnim.start([this] { update(); }, 1.0, 0.0, kHoverMs);
    }
    setCursor(Qt::ArrowCursor);
}

void ChatInputBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;

    if (!m_pendingImage.isNull()) {
        if (cancelButtonRect().contains(event->pos()))
            cancelImageCompose();
        return;
    }

    if (fileButtonRect().contains(event->pos())) {
        emit fileSendRequested();
    } else if (folderButtonRect().contains(event->pos())) {
        emit folderSendRequested();
    } else if (inputRect().contains(event->pos())) {
        setFocus();
        const int idx = charAtX(event->pos().x());
        m_cursorPos = idx;
        m_selStart  = idx;
        m_dragging  = true;
        update();
    } else {
        setFocus();
    }
}

void ChatInputBar::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
}

// ── Focus ─────────────────────────────────────────────────────────────────────

void ChatInputBar::focusInEvent(QFocusEvent *e) {
    QWidget::focusInEvent(e);
    m_focused = true;
    m_focusAnim.start([this] { update(); }, 0.0, 1.0, 150);
    QApplication::inputMethod()->show();
}

void ChatInputBar::focusOutEvent(QFocusEvent *e) {
    QWidget::focusOutEvent(e);
    m_focused = false;
    m_focusAnim.start([this] { update(); }, 1.0, 0.0, 150);
}

// ── Keyboard ──────────────────────────────────────────────────────────────────

void ChatInputBar::keyPressEvent(QKeyEvent *event) {
    // Image compose mode intercepts Enter and Escape
    if (!m_pendingImage.isNull()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            emit imageSendRequested(m_pendingImage);
            cancelImageCompose();
        } else if (event->key() == Qt::Key_Escape) {
            cancelImageCompose();
        }
        return;
    }

    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool ctrl  = event->modifiers() & Qt::ControlModifier;

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!m_text.trimmed().isEmpty()) {
            emit messageSent(m_text.trimmed());
            clear();
        }
        break;
    case Qt::Key_Backspace:
        if (hasSelection()) {
            deleteSelection();
        } else if (m_cursorPos > 0) {
            QString t = m_text;
            t.remove(m_cursorPos - 1, 1);
            moveCursor(m_cursorPos - 1);
            updateText(t);
        }
        break;
    case Qt::Key_Delete:
        if (hasSelection()) {
            deleteSelection();
        } else if (m_cursorPos < m_text.size()) {
            QString t = m_text;
            t.remove(m_cursorPos, 1);
            updateText(t);
        }
        break;
    case Qt::Key_Left:
        if (hasSelection() && !shift) {
            moveCursor(selFrom()); // collapse to left of selection
        } else if (m_cursorPos > 0) {
            m_cursorPos--;
            if (!shift) m_selStart = m_cursorPos;
            update();
        }
        break;
    case Qt::Key_Right:
        if (hasSelection() && !shift) {
            moveCursor(selTo()); // collapse to right of selection
        } else if (m_cursorPos < m_text.size()) {
            m_cursorPos++;
            if (!shift) m_selStart = m_cursorPos;
            update();
        }
        break;
    case Qt::Key_Home:
        m_cursorPos = 0;
        if (!shift) m_selStart = 0;
        update();
        break;
    case Qt::Key_End:
        m_cursorPos = m_text.size();
        if (!shift) m_selStart = m_cursorPos;
        update();
        break;
    case Qt::Key_Escape:
        if (hasSelection()) {
            m_selStart = m_cursorPos; // collapse selection
            update();
        } else {
            clear();
            clearFocus();
        }
        break;
    default: {
        if (ctrl) {
            switch (event->key()) {
            case Qt::Key_A:
                m_selStart  = 0;
                m_cursorPos = m_text.size();
                update();
                break;
            case Qt::Key_C:
                QApplication::clipboard()->setText(
                    hasSelection() ? selectedText() : m_text);
                break;
            case Qt::Key_X:
                if (hasSelection()) {
                    QApplication::clipboard()->setText(selectedText());
                    deleteSelection();
                } else {
                    QApplication::clipboard()->setText(m_text);
                    clear();
                }
                break;
            case Qt::Key_V: {
                const QMimeData *mime = QApplication::clipboard()->mimeData();
                if (mime->hasImage()) {
                    const QImage img = qvariant_cast<QImage>(mime->imageData());
                    if (!img.isNull()) { enterImageCompose(img); setFocus(); break; }
                }
                const QString pasted = QApplication::clipboard()->text();
                if (!pasted.isEmpty()) {
                    if (hasSelection()) deleteSelection();
                    QString t = m_text;
                    t.insert(m_cursorPos, pasted);
                    m_cursorPos += pasted.size();
                    m_selStart   = m_cursorPos;
                    updateText(t);
                }
                break;
            }
            default: break;
            }
        } else {
            const QString ch = event->text();
            if (!(event->modifiers() & Qt::AltModifier)
                    && !ch.isEmpty() && ch[0].isPrint()) {
                if (hasSelection()) deleteSelection();
                QString t = m_text;
                t.insert(m_cursorPos, ch);
                m_cursorPos += ch.size();
                m_selStart   = m_cursorPos;
                updateText(t);
            }
        }
        break;
    }
    }
}

void ChatInputBar::inputMethodEvent(QInputMethodEvent *event) {
    if (!m_pendingImage.isNull()) return;

    // Replace any active selection before touching text
    if (!event->commitString().isEmpty() || !event->preeditString().isEmpty()) {
        if (hasSelection()) deleteSelection();
    }

    // Commit: insert finalized characters and clear preedit
    if (!event->commitString().isEmpty()) {
        QString t = m_text;
        t.insert(m_cursorPos, event->commitString());
        m_cursorPos += event->commitString().size();
        m_selStart   = m_cursorPos;
        m_preedit.clear();
        updateText(t);
        return;
    }

    // Preedit update (composition in progress)
    m_preedit = event->preeditString();
    update();
}

QVariant ChatInputBar::inputMethodQuery(Qt::InputMethodQuery query) const {
    switch (query) {
    case Qt::ImCursorPosition:   return m_cursorPos;
    case Qt::ImAnchorPosition:   return m_selStart;
    case Qt::ImSurroundingText:  return m_text;
    case Qt::ImCurrentSelection: return selectedText();
    default: return QWidget::inputMethodQuery(query);
    }
}

void ChatInputBar::updateText(const QString &t) {
    if (m_text == t) return;
    m_text      = t;
    m_cursorPos = std::clamp(m_cursorPos, 0, static_cast<int>(m_text.size()));
    update();
}
