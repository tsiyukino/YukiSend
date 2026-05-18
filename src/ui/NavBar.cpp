#include "NavBar.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QSvgRenderer>

#include "theme/Theme.h"

static constexpr int kNavW     = Theme::Space::NavBarWidth;
static constexpr int kIconSize = 20;
static constexpr int kBtnSize  = 32;
static constexpr int kRadius   =  6;
static constexpr int kHoverMs  = 150;

static const char *kIconPaths[4] = {
    ":/icons/send.svg",
    ":/icons/cooperate.svg",
    ":/icons/git.svg",
    ":/icons/settings.svg",
};

NavBar::NavBar(QWidget *parent)
    : QWidget(parent)
    , m_hoverAnim{Animator(this), Animator(this), Animator(this), Animator(this)}
{
    setFixedWidth(kNavW);
    setMouseTracking(true);
}

void NavBar::setCurrentPage(int page) {
    m_currentPage = page;
    update();
}

int NavBar::currentPage() const { return m_currentPage; }

QRect NavBar::iconRect(int index) const {
    const int x   = (kNavW - kBtnSize) / 2;
    const int gap = 6;
    if (index < 3) {
        return QRect(x, 12 + index * (kBtnSize + gap), kBtnSize, kBtnSize);
    } else {
        // Settings pinned to bottom
        return QRect(x, height() - 12 - kBtnSize, kBtnSize, kBtnSize);
    }
}

void NavBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Theme::Color::NavBarBg);

    // Right border
    p.fillRect(width() - 1, 0, 1, height(), Theme::Color::Divider);

    for (int i = 0; i < 4; ++i) {
        const bool   active = (i == m_currentPage);
        const double hov    = m_hoverAnim[i].value((i == m_hoverIndex) ? 1.0 : 0.0);
        const QRect  btn    = iconRect(i);

        // Button background
        if (active || hov > 0.0) {
            QColor bg = active ? Theme::Color::Accent : Theme::Color::ItemHover;
            if (!active) bg.setAlphaF(hov);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(btn, kRadius, kRadius);
        }

        // SVG icon — tinted white when active, grey otherwise
        QSvgRenderer svg(QString::fromLatin1(kIconPaths[i]));
        if (svg.isValid()) {
            // Paint SVG into a pixmap so we can tint it
            QPixmap pix(kIconSize, kIconSize);
            pix.fill(Qt::transparent);
            QPainter pp(&pix);
            svg.render(&pp);
            pp.end();

            // Apply color tint via composition
            const QColor tint = active
                ? QColor(255, 255, 255)
                : QColor(Theme::Color::TextSecondary);
            QPainter tp(&pix);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(pix.rect(), tint);
            tp.end();

            const int ix = btn.left() + (kBtnSize - kIconSize) / 2;
            const int iy = btn.top()  + (kBtnSize - kIconSize) / 2;
            p.drawPixmap(ix, iy, pix);
        }
    }
}

void NavBar::mouseMoveEvent(QMouseEvent *event) {
    int hit = -1;
    for (int i = 0; i < 4; ++i)
        if (iconRect(i).contains(event->pos())) { hit = i; break; }

    if (hit != m_hoverIndex) {
        if (m_hoverIndex >= 0)
            m_hoverAnim[m_hoverIndex].start([this] { update(); }, 1.0, 0.0, kHoverMs);
        m_hoverIndex = hit;
        if (hit >= 0) {
            setCursor(Qt::PointingHandCursor);
            m_hoverAnim[hit].start([this] { update(); }, 0.0, 1.0, kHoverMs);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
}

void NavBar::leaveEvent(QEvent *) {
    if (m_hoverIndex >= 0) {
        m_hoverAnim[m_hoverIndex].start([this] { update(); }, 1.0, 0.0, kHoverMs);
        m_hoverIndex = -1;
        setCursor(Qt::ArrowCursor);
    }
}

void NavBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    for (int i = 0; i < 4; ++i) {
        if (iconRect(i).contains(event->pos())) {
            if (i != m_currentPage) {
                m_currentPage = i;
                emit pageChanged(i);
                update();
            }
            break;
        }
    }
}
