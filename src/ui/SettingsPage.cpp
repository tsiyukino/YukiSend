#include "SettingsPage.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QLineEdit>
#include <QScrollArea>
#include <QFileDialog>
#include <QStandardPaths>

#include "theme/Theme.h"
#include "theme/Fonts.h"

// ── Design tokens ──────────────────────────────────────────────────────────────

static constexpr int kPagePadTop  =  16;
static constexpr int kPagePadH    =  20;
static constexpr int kCardRadius  =   6;
static constexpr int kCardPadH    =  12;
static constexpr int kRowH        =  44;
static constexpr int kLabelColW   = 120;
static constexpr int kBrowseW     =  72;
static constexpr int kToggleW     =  36;
static constexpr int kToggleH     =  20;
static constexpr int kRadioSize   =  16;
static constexpr int kSectionGap  =  16;
static constexpr int kSecFontSz   =  11;
static constexpr int kSecLineH    =  16;
static constexpr int kHistH       = 180;
static constexpr int kBtnH        =  28;
static constexpr int kBtnW        =  96;
static constexpr int kBtnGap      =   8;
static constexpr int kTabH        =  28;
static constexpr int kTabW        =  80;
static constexpr int kTabGap      =   4;

static const QColor kPageBg    { 251, 251, 250 };  // Notion page background (near-white warm)
static const QColor kCardBg    { 255, 255, 255 };
static const QColor kCardBorder{ 233, 233, 231 };  // matches Theme::Color::Divider
static const QColor kSecTitle  { 155, 154, 151 };  // matches Theme::Color::TextSecondary
static const QColor kDescText  { 155, 154, 151 };
static const QColor kSideBg   { 247, 246, 243 };  // matches Theme::Color::NavBarBg
static const QColor kSideHov  { 239, 239, 238 };  // matches Theme::Color::ItemHover

static const char *kCatLabels[] = {
    "General", "Window", "Startup", "Messages", "History", "Peers", "About"
};
static const char *kTabLabels[] = { "Saved", "Favorites", "Blocked" };

// ── ThemedLineEdit ─────────────────────────────────────────────────────────────

namespace {
class ThemedLineEdit : public QLineEdit {
public:
    explicit ThemedLineEdit(QWidget *parent = nullptr) : QLineEdit(parent) {
        setFrame(false);
        setAttribute(Qt::WA_TranslucentBackground);
        setFont(Fonts::regular(Theme::Font::SizeBody));
        setStyleSheet(QStringLiteral(
            "QLineEdit { background:transparent; color:rgb(26,26,26);"
            "border:none; padding:0px 8px;"
            "selection-background-color:rgb(210,210,208); }"));
    }
protected:
    void paintEvent(QPaintEvent *e) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool focused = hasFocus();
        p.setPen(Qt::NoPen);
        p.setBrush(kCardBg);
        p.drawRoundedRect(rect(), Theme::Space::RadiusS, Theme::Space::RadiusS);
        p.setBrush(Qt::NoBrush);
        // Focus: slightly darker grey border, never blue
        const QColor border = focused ? QColor(155, 154, 151) : kCardBorder;
        const int bw = 1;
        p.setPen(QPen(border, bw));
        p.drawRoundedRect(QRectF(rect()).adjusted(bw/2., bw/2., -bw/2., -bw/2.),
                          Theme::Space::RadiusS, Theme::Space::RadiusS);
        p.end();
        QLineEdit::paintEvent(e);
    }
};
} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// HistoryListWidget  (kept as before, internal to this TU)
// ══════════════════════════════════════════════════════════════════════════════

class HistoryListWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistoryListWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
    }
    void setPeers(const QStringList &peerIds) {
        m_rows.clear();
        for (const QString &id : peerIds) {
            Row r; r.peerId = id;
            const int at = id.indexOf(QLatin1Char('@'));
            r.name = at > 0 ? id.left(at) : id;
            r.ip   = at > 0 ? id.mid(at + 1) : QString{};
            m_rows.append(r);
        }
        setFixedHeight(qMax(1, m_rows.size()) * kRowH);
        update();
    }
    QStringList checkedPeers() const {
        QStringList r;
        for (const auto &row : m_rows) if (row.checked) r.append(row.peerId);
        return r;
    }
    void checkAll(bool c) { for (auto &r : m_rows) r.checked = c; update(); emit selectionChanged(); }
    bool allChecked() const {
        for (const auto &r : m_rows) if (!r.checked) return false;
        return !m_rows.isEmpty();
    }
signals:
    void selectionChanged();
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), kCardBg);
        for (int i = 0; i < m_rows.size(); ++i) {
            const auto &r = m_rows[i];
            const QRect row(0, i * kRowH, width(), kRowH);
            if (i == m_hoverIndex) { p.setPen(Qt::NoPen); p.setBrush(Theme::Color::ItemHover); p.drawRect(row); }
            if (i > 0) p.fillRect(16, i * kRowH, width()-32, 1, kCardBorder);
            const int cy = row.top() + (kRowH - 16) / 2;
            const QRect box(16, cy, 16, 16);
            p.setPen(QPen(r.checked ? Theme::Color::Accent : kCardBorder, 1.5));
            p.setBrush(r.checked ? Theme::Color::Accent : Qt::white);
            p.drawRoundedRect(box, 3, 3);
            if (r.checked) {
                p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                QPolygon t; t << QPoint(box.left()+3, box.top()+8)
                              << QPoint(box.left()+7, box.bottom()-3)
                              << QPoint(box.right()-3, box.top()+3);
                p.drawPolyline(t);
            }
            const int tx      = box.right() + 8;
            const int lineGap = 2;
            const int nameH   = 16;
            const int capH    = 14;
            const int blkTop  = r.ip.isEmpty()
                ? row.top() + (kRowH - nameH) / 2
                : row.top() + (kRowH - nameH - lineGap - capH) / 2;
            p.setFont(Fonts::regular(Theme::Font::SizeBody));
            p.setPen(Theme::Color::TextPrimary);
            p.drawText(QRect(tx, blkTop, width()-tx-16, nameH),
                       Qt::AlignLeft|Qt::AlignVCenter, r.name);
            if (!r.ip.isEmpty()) {
                p.setFont(Fonts::regular(Theme::Font::SizeCaption));
                p.setPen(kDescText);
                p.drawText(QRect(tx, blkTop+nameH+lineGap, width()-tx-16, capH),
                           Qt::AlignLeft|Qt::AlignVCenter, r.ip);
            }
        }
        if (m_rows.isEmpty()) {
            p.setFont(Fonts::regular(Theme::Font::SizeBody));
            p.setPen(kDescText);
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No history yet"));
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        const int idx = e->pos().y() / kRowH;
        const int hit = (idx >= 0 && idx < m_rows.size()) ? idx : -1;
        if (hit != m_hoverIndex) { m_hoverIndex = hit; update(); }
    }
    void leaveEvent(QEvent *) override { m_hoverIndex = -1; update(); }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) return;
        const int idx = e->pos().y() / kRowH;
        if (idx >= 0 && idx < m_rows.size()) {
            m_rows[idx].checked = !m_rows[idx].checked;
            update(); emit selectionChanged();
        }
    }
private:
    struct Row { QString peerId, name, ip; bool checked = false; };
    QList<Row> m_rows;
    int        m_hoverIndex = -1;
};

// ══════════════════════════════════════════════════════════════════════════════
// PeerCheckList
// ══════════════════════════════════════════════════════════════════════════════

PeerCheckList::PeerCheckList(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
}

void PeerCheckList::setPeers(const QList<Peer> &peers) {
    m_rows.clear();
    for (const Peer &p : peers)
        m_rows.append({ p, false });
    setFixedHeight(qMax(1, m_rows.size()) * kRowH);
    update();
}

QList<Peer> PeerCheckList::checkedPeers() const {
    QList<Peer> r;
    for (const auto &row : m_rows) if (row.checked) r.append(row.peer);
    return r;
}

void PeerCheckList::checkAll(bool c) {
    for (auto &r : m_rows) r.checked = c;
    update(); emit selectionChanged();
}

bool PeerCheckList::allChecked() const {
    for (const auto &r : m_rows) if (!r.checked) return false;
    return !m_rows.isEmpty();
}

void PeerCheckList::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), kCardBg);

    for (int i = 0; i < m_rows.size(); ++i) {
        const auto &r = m_rows[i];
        const QRect row(0, i * kRowH, width(), kRowH);

        if (i == m_hoverIndex) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Color::ItemHover);
            p.drawRect(row);
        }
        if (i > 0)
            p.fillRect(kPadH, i * kRowH, width() - kPadH * 2, 1, kCardBorder);

        // Checkbox
        const int cy = row.top() + (kRowH - kCheckSize) / 2;
        const QRect box(kPadH, cy, kCheckSize, kCheckSize);
        p.setPen(QPen(r.checked ? Theme::Color::Accent : kCardBorder, 1.5));
        p.setBrush(r.checked ? Theme::Color::Accent : Qt::white);
        p.drawRoundedRect(box, 3, 3);
        if (r.checked) {
            p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const int m = 3;
            QPolygon tick;
            tick << QPoint(box.left()+m,            box.top()+kCheckSize/2)
                 << QPoint(box.left()+kCheckSize/2-1, box.bottom()-m)
                 << QPoint(box.right()-m,            box.top()+m);
            p.drawPolyline(tick);
        }

        // Name + IP stacked, centred in kRowH
        const int tx      = box.right() + 8;
        const int lineGap = 2;
        const int nameH   = 16;
        const int capH    = 14;
        const int blkTop  = row.top() + (kRowH - nameH - lineGap - capH) / 2;
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextPrimary);
        p.drawText(QRect(tx, blkTop, width()-tx-kPadH, nameH),
                   Qt::AlignLeft|Qt::AlignVCenter, r.peer.displayName);
        p.setFont(Fonts::regular(Theme::Font::SizeCaption));
        p.setPen(kDescText);
        p.drawText(QRect(tx, blkTop+nameH+lineGap, width()-tx-kPadH, capH),
                   Qt::AlignLeft|Qt::AlignVCenter, r.peer.address);
    }

    if (m_rows.isEmpty()) {
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(kDescText);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Nothing here"));
    }
}

void PeerCheckList::mouseMoveEvent(QMouseEvent *e) {
    const int idx = e->pos().y() / kRowH;
    const int hit = (idx >= 0 && idx < m_rows.size()) ? idx : -1;
    if (hit != m_hoverIndex) {
        m_hoverIndex = hit;
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void PeerCheckList::leaveEvent(QEvent *) {
    m_hoverIndex = -1; update();
}

void PeerCheckList::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    const int idx = e->pos().y() / kRowH;
    if (idx >= 0 && idx < m_rows.size()) {
        m_rows[idx].checked = !m_rows[idx].checked;
        update(); emit selectionChanged();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SettingsPage
// ══════════════════════════════════════════════════════════════════════════════

static QScrollArea *makeScrollArea(QWidget *inner) {
    auto *sa = new QScrollArea;
    sa->setWidget(inner);
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setStyleSheet(QStringLiteral(
        "QScrollArea { background: white; border: none; }"
        "QScrollBar:vertical { width: 5px; background: transparent; }"
        "QScrollBar::handle:vertical { background: rgba(0,0,0,18%); border-radius: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));
    return sa;
}

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
    , m_nameEdit(new ThemedLineEdit(this))
    , m_dirEdit(new ThemedLineEdit(this))
    , m_histScroll(makeScrollArea(m_histList = new HistoryListWidget))
    , m_savedScroll(makeScrollArea(m_savedList = new PeerCheckList))
    , m_favScroll(makeScrollArea(m_favList = new PeerCheckList))
    , m_blockedScroll(makeScrollArea(m_blockedList = new PeerCheckList))
{
    setMouseTracking(true);

    m_histScroll->setParent(this);
    m_savedScroll->setParent(this);
    m_favScroll->setParent(this);
    m_blockedScroll->setParent(this);

    m_dirEdit->setReadOnly(true);
    m_dirEdit->setPlaceholderText(QStringLiteral("System default (Downloads)"));
    m_nameEdit->setPlaceholderText(QStringLiteral("Your hostname"));

    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] {
        emit displayNameChanged(m_nameEdit->text().trimmed());
    });
    connect(m_histList, &HistoryListWidget::selectionChanged, this, [this] { update(); });
    connect(m_savedList,   &PeerCheckList::selectionChanged, this, [this] { update(); });
    connect(m_favList,     &PeerCheckList::selectionChanged, this, [this] { update(); });
    connect(m_blockedList, &PeerCheckList::selectionChanged, this, [this] { update(); });
}

// ── Public setters ─────────────────────────────────────────────────────────────

void SettingsPage::setPeerStore(PeerStore *store) { m_store = store; }

void SettingsPage::setDisplayName(const QString &n)        { m_nameEdit->setText(n); }
void SettingsPage::setDownloadDir(const QString &d)        { m_dirEdit->setText(d); }
void SettingsPage::setCloseAction(CloseAction a)           { m_closeAction = a; update(); }
void SettingsPage::setLaunchAtStartup(bool e)              { m_launchAtStartup = e; update(); }
void SettingsPage::setDefaultStorageStrategy(StorageStrategy s) { m_strategy = s; update(); }

void SettingsPage::refreshHistory(const QStringList &peerIds) {
    m_histList->setPeers(peerIds);
    update();
}

void SettingsPage::refreshPeers() {
    if (!m_store) return;
    m_savedList->setPeers(m_store->savedPeers());
    m_favList->setPeers(m_store->favoritePeers());
    m_blockedList->setPeers(m_store->blockedPeers());
    update();
}

// ── Sidebar geometry ──────────────────────────────────────────────────────────

QRect SettingsPage::sidebarRect()  const {
    return QRect(0, 0, kSideW, height());
}
QRect SettingsPage::contentRect() const {
    return QRect(kSideW + 1, 0, width() - kSideW - 1, height());
}
QRect SettingsPage::navItemRect(int cat) const {
    return QRect(0, cat * kNavItemH, kSideW, kNavItemH);
}

// ── Content geometry ──────────────────────────────────────────────────────────

// Returns the content X (left edge inside content area, accounting for padding)
static int cxOf(int contentLeft) { return contentLeft + kPagePadH; }

// Convenience: content area left + padded width
static void contentGeom(int contentLeft, int contentW,
                        int &padX, int &padW) {
    padX = contentLeft + kPagePadH;
    padW = contentW - kPagePadH * 2;
}

int SettingsPage::contentHeight(Category cat) const {
    // Approximate total drawn height per category
    switch (cat) {
    case General:  return kPagePadTop + kSecLineH + kRowH + kSectionGap
                        + kSecLineH + kRowH + kPagePadTop;
    case Window:   return kPagePadTop + kSecLineH + kRowH*2 + kPagePadTop;
    case Startup:  return kPagePadTop + kSecLineH + kRowH   + kPagePadTop;
    case Messages: return kPagePadTop + kSecLineH + kRowH*2 + kPagePadTop;
    case History:  return kPagePadTop + kSecLineH + kHistH
                        + kBtnH + kBtnGap*2 + kPagePadTop;
    case Peers:    return kPagePadTop + kTabH + kBtnGap
                        + kHistH + kBtnH + kBtnGap*2 + kPagePadTop;
    case About:    return kPagePadTop + kSecLineH + kRowH + kPagePadTop;
    default:       return 0;
    }
}

void SettingsPage::clampScroll() {
    const int cr = contentRect().height();
    const int maxS = qMax(0, contentHeight(m_category) - cr);
    m_scrollY[m_category] = qBound(0, m_scrollY[m_category], maxS);
}

// ── Children layout ────────────────────────────────────────────────────────────

void SettingsPage::layoutChildren() {
    const QRect cr = contentRect();
    const int   sy = m_scrollY[m_category];

    // Hide all scroll areas first, then show the relevant one(s)
    m_nameEdit->hide();    m_dirEdit->hide();
    m_histScroll->hide();
    m_savedScroll->hide(); m_favScroll->hide(); m_blockedScroll->hide();

    const int padX = cr.left() + kPagePadH;
    const int padW = cr.width() - kPagePadH * 2;
    // Field starts after card-left-pad + label column + gap, ends before card-right-pad
    const int fieldX = padX + kCardPadH + kLabelColW + 8;
    const int fieldW = padX + padW - kCardPadH - fieldX;

    if (m_category == General) {
        // Identity row y
        const int idTop  = kPagePadTop + kSecLineH + kSectionGap/2;
        const int rowY1  = idTop - sy;
        m_nameEdit->setGeometry(fieldX, rowY1 + (kRowH-32)/2, fieldW, 32);
        m_nameEdit->show();

        // Files row y
        const int filesTop = idTop + kRowH + kSectionGap + kSecLineH + kSectionGap/2;
        const int rowY2    = filesTop - sy;
        m_dirEdit->setGeometry(fieldX, rowY2 + (kRowH-32)/2,
                               fieldW - kBrowseW - kBtnGap, 32);
        m_dirEdit->show();
    }

    if (m_category == History) {
        const int listTop = kPagePadTop + kSecLineH + kSectionGap/2 - sy;
        m_histScroll->setGeometry(cr.left() + kPagePadH, listTop,
                                  padW, kHistH);
        m_histScroll->show();
    }

    if (m_category == Peers) {
        // Peer list sits below the tab row
        const int listTop = kPagePadTop + kTabH + kBtnGap - sy;
        QScrollArea *activeScroll = nullptr;
        switch (m_peersTab) {
        case Saved:     activeScroll = m_savedScroll;   break;
        case Favorites: activeScroll = m_favScroll;     break;
        case Blocked:   activeScroll = m_blockedScroll; break;
        }
        if (activeScroll) {
            activeScroll->setGeometry(cr.left() + kPagePadH, listTop, padW, kHistH);
            activeScroll->show();
        }
    }
}

void SettingsPage::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    clampScroll();
    layoutChildren();
}

void SettingsPage::wheelEvent(QWheelEvent *event) {
    m_scrollY[m_category] -= event->angleDelta().y() / 8 * 3;
    clampScroll();
    layoutChildren();
    update();
    event->accept();
}

// ── Hit-test rects (in widget coords = content-area-relative + scroll offset) ──

// All rects are in *widget* coordinates (scroll already applied)
// Content area starts at cr.left() = kSideW + 1

static int crLeft() { return 149; }  // kSideW(148) + 1px divider

QRect SettingsPage::browseBtnRect() const {
    const int sy = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    // Files section is second section in General
    const int idTop    = kPagePadTop + kSecLineH + kSectionGap/2;
    const int filesTop = idTop + kRowH + kSectionGap + kSecLineH + kSectionGap/2;
    const int rowTop   = filesTop - sy;
    const int rowRight = padX + padW - kCardPadH;
    return QRect(rowRight - kBrowseW, rowTop + (kRowH-32)/2, kBrowseW, 32);
}
QRect SettingsPage::radioMinRect() const {
    const int sy = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const int top  = kPagePadTop + kSecLineH + kSectionGap/2 - sy;
    return QRect(padX, top, padW, kRowH);
}
QRect SettingsPage::radioQuitRect() const {
    const QRect r = radioMinRect();
    return r.translated(0, kRowH);
}
QRect SettingsPage::toggleStartRect() const {
    const int sy = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const int top  = kPagePadTop + kSecLineH + kSectionGap/2 - sy;
    return QRect(padX, top, padW, kRowH);
}
QRect SettingsPage::radioPersRect() const {
    const int sy = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const int top  = kPagePadTop + kSecLineH + kSectionGap/2 - sy;
    return QRect(padX, top, padW, kRowH);
}
QRect SettingsPage::radioSessRect() const {
    return radioPersRect().translated(0, kRowH);
}
QRect SettingsPage::histCheckAllRect() const {
    const int sy  = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int y   = kPagePadTop + kSecLineH + kSectionGap/2 + kHistH + kBtnGap - sy;
    return QRect(padX, y, kBtnW, kBtnH);
}
QRect SettingsPage::histDeleteRect() const {
    const QRect ca = histCheckAllRect();
    return QRect(ca.right() + kBtnGap, ca.top(), kBtnW, kBtnH);
}
QRect SettingsPage::peersTabRect(int tab) const {
    const int sy  = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int y   = kPagePadTop - sy;
    return QRect(padX + tab * (kTabW + kTabGap), y, kTabW, kTabH);
}
QRect SettingsPage::peersCheckAllRect() const {
    const int sy  = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int y   = kPagePadTop + kTabH + kBtnGap + kHistH + kBtnGap - sy;
    return QRect(padX, y, kBtnW, kBtnH);
}
QRect SettingsPage::peersRemoveRect() const {
    const QRect ca = peersCheckAllRect();
    return QRect(ca.right() + kBtnGap, ca.top(), kBtnW, kBtnH);
}

// ── Paint helpers ──────────────────────────────────────────────────────────────

void SettingsPage::paintCard(QPainter &p, int top, int rows) const {
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const QRect r(padX, top, padW, kRowH * rows);
    p.setBrush(kCardBg);
    p.setPen(QPen(kCardBorder, 1));
    p.drawRoundedRect(r, kCardRadius, kCardRadius);
}

void SettingsPage::paintCustomCard(QPainter &p, int top, int h) const {
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const QRect r(padX, top, padW, h);
    p.setBrush(kCardBg);
    p.setPen(QPen(kCardBorder, 1));
    p.drawRoundedRect(r, kCardRadius, kCardRadius);
}

void SettingsPage::paintRowDivider(QPainter &p, int y) const {
    const int padX = crLeft() + kPagePadH + kCardPadH;
    const int padW = width() - crLeft() - kPagePadH * 2 - kCardPadH * 2;
    p.fillRect(padX, y, padW, 1, kCardBorder);
}

void SettingsPage::paintSectionLabel(QPainter &p, int y, const QString &text) const {
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    p.setFont(Fonts::regular(kSecFontSz));
    p.setPen(kSecTitle);
    p.drawText(QRect(padX, y, padW, kSecLineH),
               Qt::AlignVCenter | Qt::AlignLeft, text);
}

void SettingsPage::paintRowLabel(QPainter &p, const QRect &row,
                                  const QString &title, const QString &desc) const {
    // Label occupies from kCardPadH to kCardPadH+kLabelColW inside the row.
    // Input field starts at kCardPadH+kLabelColW+kFieldGap — same formula as layoutChildren.
    const QRect lr(row.left() + kCardPadH, row.top(), kLabelColW, kRowH);
    if (desc.isEmpty()) {
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextPrimary);
        const QFontMetrics fm(p.font());
        p.drawText(lr, Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(title, Qt::ElideRight, lr.width()));
    } else {
        // Two-line label: title + desc stacked, centred in kRowH
        const int lineGap  = 2;
        const int nameH    = 16;
        const int captionH = 14;
        const int blockH   = nameH + lineGap + captionH;
        const int blockTop = lr.top() + (kRowH - blockH) / 2;
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextPrimary);
        const QFontMetrics fm(p.font());
        p.drawText(QRect(lr.left(), blockTop, lr.width(), nameH),
                   Qt::AlignLeft|Qt::AlignVCenter,
                   fm.elidedText(title, Qt::ElideRight, lr.width()));
        p.setFont(Fonts::regular(Theme::Font::SizeCaption));
        p.setPen(kDescText);
        const QFontMetrics cfm(p.font());
        p.drawText(QRect(lr.left(), blockTop + nameH + lineGap, lr.width(), captionH),
                   Qt::AlignLeft|Qt::AlignVCenter,
                   cfm.elidedText(desc, Qt::ElideRight, lr.width()));
    }
}

void SettingsPage::paintRadioRow(QPainter &p, const QRect &row,
                                  bool checked, bool hovered,
                                  const QString &label) const {
    if (hovered) { p.setPen(Qt::NoPen); p.setBrush(Theme::Color::ItemHover); p.drawRect(row); }
    const int cy = row.top() + (kRowH - kRadioSize) / 2;
    const QRect circle(row.right() - kCardPadH - kRadioSize, cy, kRadioSize, kRadioSize);
    p.setPen(QPen(checked ? Theme::Color::Accent : kCardBorder, 1.5));
    p.setBrush(Qt::white);
    p.drawEllipse(circle);
    if (checked) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Color::Accent);
        p.drawEllipse(circle.adjusted(4,4,-4,-4));
    }
    p.setFont(Fonts::regular(Theme::Font::SizeBody));
    p.setPen(Theme::Color::TextPrimary);
    p.drawText(QRect(row.left()+kCardPadH, row.top(),
                     row.width()-kCardPadH*2-kRadioSize-8, kRowH),
               Qt::AlignVCenter|Qt::AlignLeft, label);
}

void SettingsPage::paintToggleRow(QPainter &p, const QRect &row,
                                   bool on, bool hovered,
                                   const QString &label, const QString &desc) const {
    if (hovered) { p.setPen(Qt::NoPen); p.setBrush(Theme::Color::ItemHover); p.drawRect(row); }
    paintRowLabel(p, row, label, desc);
    const int ty = row.top() + (kRowH - kToggleH) / 2;
    const QRect pill(row.right() - kCardPadH - kToggleW, ty, kToggleW, kToggleH);
    p.setPen(Qt::NoPen);
    p.setBrush(on ? Theme::Color::Accent : kCardBorder);
    p.drawRoundedRect(pill, kToggleH/2, kToggleH/2);
    const int ks = kToggleH - 4;
    const int kx = on ? pill.right()-2-ks : pill.left()+2;
    p.setBrush(Qt::white);
    p.drawEllipse(QRect(kx, pill.top()+2, ks, ks));
}

void SettingsPage::paintActionBtn(QPainter &p, const QRect &r,
                                   bool hover, const QString &label,
                                   const QColor &bg, const QColor &hoverBg,
                                   bool lightText) const {
    const QColor activeBg = hover ? hoverBg : bg;
    p.setPen(Qt::NoPen);
    p.setBrush(activeBg);
    p.drawRoundedRect(r, Theme::Space::RadiusS, Theme::Space::RadiusS);
    p.setFont(Fonts::medium(Theme::Font::SizeBody));
    p.setPen(lightText ? QColor(255,255,255) : Theme::Color::TextPrimary);
    p.drawText(r, Qt::AlignCenter, label);
}

void SettingsPage::paintPeersTab(QPainter &p, int tab,
                                  bool selected, bool hovered, const QRect &r) const {
    if (selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(26, 26, 26));
        p.drawRoundedRect(r, kTabH/2, kTabH/2);
        p.setFont(Fonts::medium(Theme::Font::SizeBody));
        p.setPen(Qt::white);
    } else {
        p.setPen(QPen(hovered ? QColor(155,154,151) : kCardBorder, 1));
        p.setBrush(hovered ? kSideHov : Qt::transparent);
        p.drawRoundedRect(r, kTabH/2, kTabH/2);
        p.setFont(Fonts::medium(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextSecondary);
    }
    p.drawText(r, Qt::AlignCenter, QString::fromUtf8(kTabLabels[tab]));
}

// ── Category painters ──────────────────────────────────────────────────────────

void SettingsPage::paintGeneral(QPainter &p) const {
    const int sy   = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;

    // Identity section
    const int idLabelY = kPagePadTop - sy;
    const int idCardTop = idLabelY + kSecLineH + kSectionGap/2;
    paintSectionLabel(p, idLabelY, QStringLiteral("Identity"));
    paintCard(p, idCardTop, 1);
    {
        const QRect row(padX, idCardTop, padW, kRowH);
        paintRowLabel(p, row, QStringLiteral("Display name"),
                      QStringLiteral("Shown to other devices on the network"));
    }

    // Files section
    const int fileLabelY = idCardTop + kRowH + kSectionGap - sy;
    const int fileCardTop = fileLabelY + kSecLineH + kSectionGap/2;
    paintSectionLabel(p, fileLabelY + sy, QStringLiteral("Files")); // label uses idCardTop-relative y — re-do:
    // Actually recompute without double-sy:
    const int fileLabelY2 = idCardTop + kRowH + kSectionGap;
    const int fileCardTop2 = fileLabelY2 + kSecLineH + kSectionGap/2;
    (void)fileLabelY; (void)fileCardTop; // suppress unused
    paintSectionLabel(p, fileLabelY2 - sy, QStringLiteral("Files"));
    paintCard(p, fileCardTop2 - sy, 1);
    {
        const QRect row(padX, fileCardTop2 - sy, padW, kRowH);
        paintRowLabel(p, row, QStringLiteral("Download folder"),
                      QStringLiteral("Where received files are saved"));
        // Browse button (raw painter coords = screen already via -sy applied above)
        const QRect browse(padX + padW - kCardPadH - kBrowseW, row.top() + (kRowH-32)/2, kBrowseW, 32);
        p.setPen(Qt::NoPen);
        p.setBrush(m_browseHover ? Theme::Color::AccentHover : Theme::Color::Accent);
        p.drawRoundedRect(browse, Theme::Space::RadiusS, Theme::Space::RadiusS);
        p.setFont(Fonts::medium(Theme::Font::SizeBody));
        p.setPen(Qt::white);
        p.drawText(browse, Qt::AlignCenter, QStringLiteral("Browse"));
    }
}

void SettingsPage::paintWindow(QPainter &p) const {
    const int sy   = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const int labelY  = kPagePadTop - sy;
    const int cardTop = labelY + kSecLineH + kSectionGap/2;
    paintSectionLabel(p, labelY, QStringLiteral("Close behavior"));
    paintCard(p, cardTop, 2);
    {
        QPainterPath clip;
        clip.addRoundedRect(QRect(padX, cardTop, padW, kRowH*2), kCardRadius, kCardRadius);
        p.save(); p.setClipPath(clip);
        const QRect r0(padX, cardTop, padW, kRowH);
        const QRect r1(padX, cardTop + kRowH, padW, kRowH);
        paintRadioRow(p, r0, m_closeAction == CloseAction::MinimizeToTray,
                      m_minHover, QStringLiteral("Minimize to tray"));
        paintRadioRow(p, r1, m_closeAction != CloseAction::MinimizeToTray,
                      m_quitHover, QStringLiteral("Quit"));
        p.restore();
        paintRowDivider(p, cardTop + kRowH);
    }
}

void SettingsPage::paintStartup(QPainter &p) const {
    const int sy   = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const int labelY  = kPagePadTop - sy;
    const int cardTop = labelY + kSecLineH + kSectionGap/2;
    paintSectionLabel(p, labelY, QStringLiteral("Startup"));
    paintCard(p, cardTop, 1);
    {
        QPainterPath clip;
        clip.addRoundedRect(QRect(padX, cardTop, padW, kRowH), kCardRadius, kCardRadius);
        p.save(); p.setClipPath(clip);
        paintToggleRow(p, QRect(padX, cardTop, padW, kRowH),
                       m_launchAtStartup, m_startHover,
                       QStringLiteral("Launch at startup"),
                       QStringLiteral("Start YukiSend when you log in"));
        p.restore();
    }
}

void SettingsPage::paintMessages(QPainter &p) const {
    const int sy   = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const int labelY  = kPagePadTop - sy;
    const int cardTop = labelY + kSecLineH + kSectionGap/2;
    paintSectionLabel(p, labelY, QStringLiteral("Message history"));
    paintCard(p, cardTop, 2);
    {
        QPainterPath clip;
        clip.addRoundedRect(QRect(padX, cardTop, padW, kRowH*2), kCardRadius, kCardRadius);
        p.save(); p.setClipPath(clip);
        const QRect r0(padX, cardTop, padW, kRowH);
        const QRect r1(padX, cardTop + kRowH, padW, kRowH);
        paintRadioRow(p, r0, m_strategy == StorageStrategy::Persistent,
                      m_persHover, QStringLiteral("Keep history (persist across restarts)"));
        paintRadioRow(p, r1, m_strategy == StorageStrategy::SessionOnly,
                      m_sessHover, QStringLiteral("Delete when peer disconnects"));
        p.restore();
        paintRowDivider(p, cardTop + kRowH);
    }
}

void SettingsPage::paintHistory(QPainter &p) const {
    const int sy   = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const int labelY  = kPagePadTop - sy;
    const int listTop = labelY + kSecLineH + kSectionGap/2;
    paintSectionLabel(p, labelY, QStringLiteral("Chat history"));
    paintCustomCard(p, listTop, kHistH);
    // Buttons
    const bool hasChk = !m_histList->checkedPeers().isEmpty();
    const bool allChk = m_histList->allChecked();
    const int  btnY   = listTop + kHistH + kBtnGap;
    paintActionBtn(p, QRect(padX, btnY, kBtnW, kBtnH), m_histChkAllHover,
                   allChk ? QStringLiteral("Uncheck all") : QStringLiteral("Check all"),
                   kCardBorder, kSideHov, false);
    paintActionBtn(p, QRect(padX + kBtnW + kBtnGap, btnY, kBtnW, kBtnH),
                   m_histDelHover && hasChk, QStringLiteral("Delete"),
                   hasChk ? QColor(195,55,50) : kCardBorder,
                   hasChk ? QColor(170,35,30) : kSideHov,
                   hasChk);
}

void SettingsPage::paintPeers(QPainter &p) const {
    const int sy   = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;

    // Sub-tabs row
    const int tabY = kPagePadTop - sy;
    for (int t = 0; t < kTabCount; ++t) {
        const QRect tr(padX + t*(kTabW + kTabGap), tabY, kTabW, kTabH);
        paintPeersTab(p, t, m_peersTab == t, m_peersTabHover == t, tr);
    }

    // List card
    const int listTop = tabY + kTabH + kBtnGap;
    paintCustomCard(p, listTop, kHistH);

    // Buttons
    const PeerCheckList *cur = (m_peersTab == Saved)     ? m_savedList
                             : (m_peersTab == Favorites) ? m_favList
                             :                              m_blockedList;
    const bool hasChk = !cur->checkedPeers().isEmpty();
    const bool allChk = cur->allChecked();
    const int  btnY   = listTop + kHistH + kBtnGap;

    const QString removeLabel = (m_peersTab == Saved)     ? QStringLiteral("Unsave")
                               : (m_peersTab == Favorites) ? QStringLiteral("Unfavorite")
                               :                              QStringLiteral("Unblock");
    paintActionBtn(p, QRect(padX, btnY, kBtnW, kBtnH), m_peersChkAllHov,
                   allChk ? QStringLiteral("Uncheck all") : QStringLiteral("Check all"),
                   kCardBorder, kSideHov, false);
    paintActionBtn(p, QRect(padX + kBtnW + kBtnGap, btnY, kBtnW, kBtnH),
                   m_peersRemoveHov && hasChk, removeLabel,
                   hasChk ? QColor(195,55,50) : kCardBorder,
                   hasChk ? QColor(170,35,30) : kSideHov,
                   hasChk);
}

void SettingsPage::paintAbout(QPainter &p) const {
    const int sy   = m_scrollY[m_category];
    const int padX = crLeft() + kPagePadH;
    const int padW = width() - crLeft() - kPagePadH * 2;
    const int labelY  = kPagePadTop - sy;
    const int cardTop = labelY + kSecLineH + kSectionGap/2;
    paintSectionLabel(p, labelY, QStringLiteral("About"));
    paintCard(p, cardTop, 1);
    const QRect row(padX, cardTop, padW, kRowH);
    paintRowLabel(p, row, QStringLiteral("Version"));
    p.setFont(Fonts::regular(Theme::Font::SizeBody));
    p.setPen(kDescText);
    p.drawText(QRect(row.left()+kCardPadH+kLabelColW, row.top(),
                     row.width()-kCardPadH*2-kLabelColW, kRowH),
               Qt::AlignVCenter|Qt::AlignLeft, QStringLiteral(APP_VERSION));
}

// ── paintSidebar ───────────────────────────────────────────────────────────────

void SettingsPage::paintSidebar(QPainter &p) const {
    // Background
    p.fillRect(sidebarRect(), kSideBg);
    // Right divider
    p.fillRect(kSideW, 0, 1, height(), kCardBorder);

    for (int c = 0; c < kCatCount; ++c) {
        const QRect r = navItemRect(c);
        const bool sel = (m_category == c);
        const bool hov = (m_navHover == c);

        if (sel) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Color::ItemSelected);
            p.drawRect(r);
            // Left indicator stripe
            p.setBrush(QColor(26, 26, 26, 200));
            p.drawRect(QRect(r.left(), r.top() + 8, 3, r.height() - 16));
        } else if (hov) {
            p.setPen(Qt::NoPen);
            p.setBrush(kSideHov);
            p.drawRect(r);
        }

        p.setFont(Fonts::regular(Theme::Font::SizeCaption));
        p.setPen(sel ? Theme::Color::TextPrimary : Theme::Color::TextSecondary);
        p.drawText(r.adjusted(16, 0, 0, 0), Qt::AlignVCenter|Qt::AlignLeft,
                   QString::fromUtf8(kCatLabels[c]));
    }
}

// ── paintEvent ────────────────────────────────────────────────────────────────

void SettingsPage::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Page background (content area)
    p.fillRect(contentRect(), kPageBg);

    // Sidebar
    paintSidebar(p);

    // Clip content area so nothing bleeds into sidebar
    p.setClipRect(contentRect());

    switch (m_category) {
    case General:  paintGeneral(p);  break;
    case Window:   paintWindow(p);   break;
    case Startup:  paintStartup(p);  break;
    case Messages: paintMessages(p); break;
    case History:  paintHistory(p);  break;
    case Peers:    paintPeers(p);    break;
    case About:    paintAbout(p);    break;
    default: break;
    }
}

// ── mouseMoveEvent ─────────────────────────────────────────────────────────────

void SettingsPage::mouseMoveEvent(QMouseEvent *event) {
    const QPoint pos = event->pos();

    // Sidebar hover
    const int navHov = pos.x() < kSideW
        ? qBound(0, pos.y() / kNavItemH, kCatCount - 1)
        : -1;

    bool bh = false, mh = false, qh = false, sh = false,
         ph = false, seh = false, hcah = false, hdh = false,
         pcah = false, prh = false;
    int  tabHov = -1;

    if (pos.x() >= kSideW) {
        switch (m_category) {
        case General:
            bh  = browseBtnRect().contains(pos);
            break;
        case Window:
            mh  = radioMinRect().contains(pos);
            qh  = radioQuitRect().contains(pos);
            break;
        case Startup:
            sh  = toggleStartRect().contains(pos);
            break;
        case Messages:
            ph  = radioPersRect().contains(pos);
            seh = radioSessRect().contains(pos);
            break;
        case History:
            hcah = histCheckAllRect().contains(pos);
            hdh  = histDeleteRect().contains(pos);
            break;
        case Peers:
            for (int t = 0; t < kTabCount; ++t)
                if (peersTabRect(t).contains(pos)) { tabHov = t; break; }
            pcah = peersCheckAllRect().contains(pos);
            prh  = peersRemoveRect().contains(pos);
            break;
        default: break;
        }
    }

    const bool anyHov = bh||mh||qh||sh||ph||seh||hcah||hdh||pcah||prh||(tabHov>=0)||(navHov>=0);
    setCursor(anyHov ? Qt::PointingHandCursor : Qt::ArrowCursor);

    if (navHov != m_navHover || bh != m_browseHover || mh != m_minHover ||
        qh != m_quitHover || sh != m_startHover || ph != m_persHover ||
        seh != m_sessHover || hcah != m_histChkAllHover || hdh != m_histDelHover ||
        tabHov != m_peersTabHover || pcah != m_peersChkAllHov || prh != m_peersRemoveHov)
    {
        m_navHover = navHov;
        m_browseHover = bh; m_minHover = mh; m_quitHover = qh;
        m_startHover = sh; m_persHover = ph; m_sessHover = seh;
        m_histChkAllHover = hcah; m_histDelHover = hdh;
        m_peersTabHover = tabHov;
        m_peersChkAllHov = pcah; m_peersRemoveHov = prh;
        update();
    }
}

void SettingsPage::leaveEvent(QEvent *) {
    m_navHover = -1;
    m_browseHover = m_minHover = m_quitHover = m_startHover =
    m_persHover = m_sessHover = m_histChkAllHover = m_histDelHover =
    m_peersChkAllHov = m_peersRemoveHov = false;
    m_peersTabHover = -1;
    setCursor(Qt::ArrowCursor);
    update();
}

// ── mousePressEvent ────────────────────────────────────────────────────────────

void SettingsPage::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const QPoint pos = event->pos();

    // Sidebar category switch
    if (pos.x() < kSideW) {
        const int cat = pos.y() / kNavItemH;
        if (cat >= 0 && cat < kCatCount && cat != m_category) {
            m_category = static_cast<Category>(cat);
            // Refresh live data when entering Peers/History
            if (m_category == Peers)   refreshPeers();
            clampScroll();
            layoutChildren();
            update();
        }
        return;
    }

    switch (m_category) {
    case General:
        if (browseBtnRect().contains(pos)) browseForFolder();
        break;
    case Window:
        if (radioMinRect().contains(pos) && m_closeAction != CloseAction::MinimizeToTray) {
            m_closeAction = CloseAction::MinimizeToTray;
            update(); emit closeActionChanged(m_closeAction);
        } else if (radioQuitRect().contains(pos) && m_closeAction != CloseAction::Quit) {
            m_closeAction = CloseAction::Quit;
            update(); emit closeActionChanged(m_closeAction);
        }
        break;
    case Startup:
        if (toggleStartRect().contains(pos)) {
            m_launchAtStartup = !m_launchAtStartup;
            update(); emit launchAtStartupChanged(m_launchAtStartup);
        }
        break;
    case Messages:
        if (radioPersRect().contains(pos) && m_strategy != StorageStrategy::Persistent) {
            m_strategy = StorageStrategy::Persistent;
            update(); emit defaultStorageStrategyChanged(m_strategy);
        } else if (radioSessRect().contains(pos) && m_strategy != StorageStrategy::SessionOnly) {
            m_strategy = StorageStrategy::SessionOnly;
            update(); emit defaultStorageStrategyChanged(m_strategy);
        }
        break;
    case History:
        if (histCheckAllRect().contains(pos)) {
            m_histList->checkAll(!m_histList->allChecked());
            update();
        } else if (histDeleteRect().contains(pos)) {
            const QStringList ids = m_histList->checkedPeers();
            if (!ids.isEmpty()) {
                emit deleteHistoryRequested(ids);
                m_histList->setPeers({});
                update();
            }
        }
        break;
    case Peers: {
        // Tab switch
        for (int t = 0; t < kTabCount; ++t) {
            if (peersTabRect(t).contains(pos) && m_peersTab != t) {
                m_peersTab = static_cast<PeersTab>(t);
                layoutChildren();
                update();
                return;
            }
        }
        // Active list
        PeerCheckList *cur = (m_peersTab == Saved)     ? m_savedList
                           : (m_peersTab == Favorites) ? m_favList
                           :                              m_blockedList;
        if (peersCheckAllRect().contains(pos)) {
            cur->checkAll(!cur->allChecked());
            update();
        } else if (peersRemoveRect().contains(pos)) {
            const QList<Peer> selected = cur->checkedPeers();
            if (selected.isEmpty()) break;
            QStringList ids;
            for (const Peer &p : selected) ids.append(p.id);
            if (m_peersTab == Saved)     { emit unsavePeerRequested(ids);     }
            if (m_peersTab == Favorites) { emit unfavoritePeerRequested(ids); }
            if (m_peersTab == Blocked)   { emit unblockPeerRequested(ids);    }
            refreshPeers();  // reload all three lists
            update();
        }
        break;
    }
    default: break;
    }
}

// ── Browse ─────────────────────────────────────────────────────────────────────

void SettingsPage::browseForFolder() {
    const QString current = m_dirEdit->text();
    const QString chosen  = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose download folder"),
        current.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            : current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (chosen.isEmpty()) return;
    m_dirEdit->setText(chosen);
    emit downloadDirChanged(chosen);
}

#include "SettingsPage.moc"
