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

void NavBar::buildIconCache() const {
    // Render at physical pixel size for crisp results on HiDPI screens.
    const qreal dpr   = devicePixelRatioF();
    const int   phys  = qRound(kIconSize * dpr);  // physical pixels

    const QColor tints[2] = { Theme::Color::TextSecondary, QColor(26, 26, 26) };
    for (int i = 0; i < 4; ++i) {
        QSvgRenderer svg(QString::fromLatin1(kIconPaths[i]));
        if (!svg.isValid()) continue;
        for (int t = 0; t < 2; ++t) {
            QPixmap pix(phys, phys);
            pix.setDevicePixelRatio(dpr);
            pix.fill(Qt::transparent);
            QPainter pp(&pix);
            pp.setRenderHint(QPainter::Antialiasing);
            pp.setRenderHint(QPainter::SmoothPixmapTransform);
            svg.render(&pp, QRectF(0, 0, kIconSize, kIconSize));
            pp.end();

            QPainter tp(&pix);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(QRect(0, 0, phys, phys), tints[t]);
            tp.end();

            m_iconCache[i][t] = pix;
        }
    }
    m_cacheValid = true;
}

void NavBar::paintEvent(QPaintEvent *) {
    if (!m_cacheValid) buildIconCache();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Theme::Color::NavBarBg);

    // Right border hairline
    p.fillRect(width() - 1, 0, 1, height(), Theme::Color::Divider);

    for (int i = 0; i < 4; ++i) {
        const bool   active = (i == m_currentPage);
        const double hov    = m_hoverAnim[i].value((i == m_hoverIndex) ? 1.0 : 0.0);
        const QRect  btn    = iconRect(i);

        // Hover: fade in ItemHover background; active: ItemSelected (no blue)
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Color::ItemSelected);
            p.drawRoundedRect(btn, kRadius, kRadius);
        } else if (hov > 0.0) {
            QColor bg = Theme::Color::ItemHover;
            bg.setAlphaF(hov);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(btn, kRadius, kRadius);
        }

        // Active indicator: 2px near-black left stripe inside button rect
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(26, 26, 26, 200));
            p.drawRoundedRect(QRect(btn.left(), btn.top() + 6, 2, btn.height() - 12),
                              1, 1);
        }

        // Draw cached pixmap — [1] = near-black (active), [0] = grey
        const QPixmap &pix = m_iconCache[i][active ? 1 : 0];
        if (!pix.isNull()) {
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

void NavBar::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    // Screen change (e.g. dragged to HiDPI monitor) — rebuild at new DPR.
    if (event->type() == QEvent::ScreenChangeInternal) {
        m_cacheValid = false;
        update();
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
