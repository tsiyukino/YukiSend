#include "SearchBar.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QFontMetrics>
#include <QApplication>
#include <QClipboard>

#include "theme/Theme.h"
#include "theme/Fonts.h"

// From dialogs.style: filter pill height=35, vpad=8 top + 8 bottom = 51, plus 1px divider below
static constexpr int kPillH   = Theme::Space::SearchHeight;
static constexpr int kVPad    = Theme::Space::SearchVPad;
static constexpr int kRadius  = Theme::Space::SearchRadius;
static constexpr int kBorderW = Theme::Space::SearchBorderW;
static constexpr int kIconAreaW    = 36; // space reserved for magnifier
static constexpr int kIconSize     = 14;
static constexpr int kFocusDuration = 150;

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
    , m_focusAnim(this)
{
    setFixedHeight(kPillH + kVPad * 2); // = SearchHeight + SearchVPad*2 = 48px
    setCursor(Qt::IBeamCursor);
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
}

QString SearchBar::text() const { return m_text; }

void SearchBar::clear() {
    updateText(QString());
}

// The pill sits inset from the widget edges by a small margin
QRect SearchBar::pillRect() const {
    const int margin = 8;
    return QRect(margin, kVPad, width() - margin * 2, kPillH);
}

QRect SearchBar::inputRect() const {
    const QRect pill = pillRect();
    return QRect(
        pill.left() + kIconAreaW,
        pill.top(),
        pill.width() - kIconAreaW - 8,
        pill.height()
    );
}

void SearchBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const double focusProg = m_focusAnim.value(m_focused ? 1.0 : 0.0);
    const QRect pill = pillRect();

    // Pill background — #f1f1f1 (searchedBarBg)
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::Color::SearchBg);
    p.drawRoundedRect(pill, kRadius, kRadius);

    // Focus border — #54c3f3, animates in/out
    if (focusProg > 0.001) {
        QColor border = Theme::Color::SearchBorder;
        border.setAlphaF(focusProg);
        p.setPen(QPen(border, kBorderW));
        p.setBrush(Qt::NoBrush);
        // Inset by half border width so it draws inside the pill
        const QRectF borderRect = QRectF(pill).adjusted(
            kBorderW * 0.5, kBorderW * 0.5,
            -kBorderW * 0.5, -kBorderW * 0.5
        );
        p.drawRoundedRect(borderRect, kRadius - kBorderW * 0.5, kRadius - kBorderW * 0.5);
    }

    // Magnifier icon — centred in the left icon area
    const int iconX = pill.left() + (kIconAreaW - kIconSize) / 2;
    const int iconY = pill.top()  + (kPillH - kIconSize) / 2;
    const int circD = kIconSize * 7 / 10;

    p.setPen(QPen(Theme::Color::SearchIcon, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(iconX, iconY, circD, circD);
    // Handle from bottom-right of circle
    const int hx1 = iconX + circD - 1;
    const int hy1 = iconY + circD - 1;
    p.drawLine(hx1, hy1, iconX + kIconSize, iconY + kIconSize);

    // Text / placeholder
    p.setFont(Fonts::regular(Theme::Font::SizeNormal));

    const QRect ir = inputRect();

    if (m_text.isEmpty()) {
        p.setPen(Theme::Color::Placeholder);
        p.drawText(ir, Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Search"));
    } else {
        p.setPen(Theme::Color::TextPrimary);
        p.drawText(ir, Qt::AlignVCenter | Qt::AlignLeft, m_text);
    }

    // Text cursor
    if (m_focused) {
        const QFont f = Fonts::regular(Theme::Font::SizeNormal);
        const QFontMetrics fm(f);
        const int cx = ir.left() + fm.horizontalAdvance(m_text.left(m_cursorPos));
        const int cy1 = pill.top()    + 6;
        const int cy2 = pill.bottom() - 6;
        p.setPen(QPen(Theme::Color::Accent, 1.5));
        p.drawLine(cx, cy1, cx, cy2);
    }
}

void SearchBar::mousePressEvent(QMouseEvent *) { setFocus(); }

void SearchBar::focusInEvent(QFocusEvent *event) {
    QWidget::focusInEvent(event);
    m_focused = true;
    m_focusAnim.start([this] { update(); }, 0.0, 1.0, kFocusDuration);
    QApplication::inputMethod()->show();
}

void SearchBar::focusOutEvent(QFocusEvent *event) {
    QWidget::focusOutEvent(event);
    m_focused = false;
    m_focusAnim.start([this] { update(); }, 1.0, 0.0, kFocusDuration);
}

void SearchBar::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Backspace:
        if (m_cursorPos > 0) {
            QString t = m_text;
            t.remove(m_cursorPos - 1, 1);
            --m_cursorPos;
            updateText(t);
        }
        break;
    case Qt::Key_Delete:
        if (m_cursorPos < m_text.size()) {
            QString t = m_text;
            t.remove(m_cursorPos, 1);
            updateText(t);
        }
        break;
    case Qt::Key_Left:
        if (m_cursorPos > 0) { --m_cursorPos; update(); }
        break;
    case Qt::Key_Right:
        if (m_cursorPos < m_text.size()) { ++m_cursorPos; update(); }
        break;
    case Qt::Key_Home:  m_cursorPos = 0;               update(); break;
    case Qt::Key_End:   m_cursorPos = m_text.size();   update(); break;
    case Qt::Key_Escape: clear(); clearFocus(); break;
    case Qt::Key_A:
        if (event->modifiers() & Qt::ControlModifier) {
            m_cursorPos = m_text.size();
            update();
        }
        break;
    case Qt::Key_C:
        if (event->modifiers() & Qt::ControlModifier)
            QApplication::clipboard()->setText(m_text);
        break;
    case Qt::Key_X:
        if (event->modifiers() & Qt::ControlModifier) {
            QApplication::clipboard()->setText(m_text);
            clear();
        }
        break;
    case Qt::Key_V:
        if (event->modifiers() & Qt::ControlModifier) {
            const QString pasted = QApplication::clipboard()->text();
            if (!pasted.isEmpty()) {
                QString t = m_text;
                t.insert(m_cursorPos, pasted);
                m_cursorPos += pasted.size();
                updateText(t);
            }
        }
        break;
    default: {
        const QString ch = event->text();
        const bool hasCtrlAlt = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier);
        if (!hasCtrlAlt && !ch.isEmpty() && ch[0].isPrint()) {
            QString t = m_text;
            t.insert(m_cursorPos, ch);
            m_cursorPos += ch.size();
            updateText(t);
        }
        break;
    }
    }
}

void SearchBar::inputMethodEvent(QInputMethodEvent *event) {
    if (!event->commitString().isEmpty()) {
        QString t = m_text;
        t.insert(m_cursorPos, event->commitString());
        m_cursorPos += event->commitString().size();
        updateText(t);
    }
}

QVariant SearchBar::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImCursorPosition) return m_cursorPos;
    if (query == Qt::ImSurroundingText) return m_text;
    return QWidget::inputMethodQuery(query);
}

void SearchBar::updateText(const QString &text) {
    if (m_text == text) return;
    m_text      = text;
    m_cursorPos = std::clamp(m_cursorPos, 0, static_cast<int>(m_text.size()));
    update();
    emit textChanged(m_text);
}
