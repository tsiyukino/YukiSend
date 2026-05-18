#include "FingerprintDialog.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEventLoop>
#include <QEvent>
#include <QResizeEvent>

#include "PeerItemDelegate.h"
#include "theme/Theme.h"
#include "theme/Fonts.h"

// ── Layout constants (4-px grid) ─────────────────────────────────────────────

static constexpr int kCardW        = 400;
static constexpr int kPadH         =  24;  // horizontal padding inside card
static constexpr int kPadV         =  28;  // vertical padding top/bottom
static constexpr int kAvatarSize   =  40;
static constexpr int kAvatarGap    =  12;
static constexpr int kSectionGap   =  16;
static constexpr int kNameAddrGap  =   4;  // gap between name and IP address
static constexpr int kLabelGap     =   6;
static constexpr int kBtnH         =  36;
static constexpr int kBtnRadius    =   4;
static constexpr int kBtnGap       =   8;
static constexpr int kFpBoxPadH    =  12;
static constexpr int kFpBoxPadV    =   8;
static constexpr int kFpBoxR       =   6;
static constexpr int kOverlayAlpha = 140;

// Formats a UUID fingerprint as colon-separated byte pairs (first 16 bytes shown).
// Example: "a1:b2:c3:d4:e5:f6:07:08:09:0a:0b:0c:0d:0e:0f:10"
static QString formatFp(const QString &fp) {
    const QString clean = QString(fp).remove(QLatin1Char('-'));
    QString out;
    for (int i = 0; i < clean.size() && i < 32; i += 2) {
        if (!out.isEmpty()) out += QLatin1Char(':');
        out += clean.mid(i, 2).toLower();
    }
    return out;
}

// ── Construction ──────────────────────────────────────────────────────────────

FingerprintDialog::FingerprintDialog(Variant variant,
                                     const Peer &peer,
                                     const QString &knownFp,
                                     const QString &seenFp,
                                     QWidget *parent)
    : QWidget(parent, Qt::Widget)
    , m_variant(variant)
    , m_peer(peer)
    , m_knownFp(knownFp)
    , m_seenFp(seenFp)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Fill the parent as a child widget, or use a fixed size if shown standalone.
    if (parent) {
        parent->installEventFilter(this);
        setGeometry(0, 0, parent->width(), parent->height());
    } else {
        resize(800, 600);
    }

    recomputeCard();
}

void FingerprintDialog::recomputeCard() {
    const int contentW = kCardW - kPadH * 2;

    QFontMetrics bodyFm(Fonts::regular(Theme::Font::SizeBody));
    QFontMetrics captionFm(Fonts::regular(Theme::Font::SizeCaption));

    QString bodyText;
    if (m_variant == Variant::Unknown) {
        bodyText = QStringLiteral("Do you want to trust this device?");
    } else {
        bodyText = QStringLiteral(
            "This device's fingerprint changed. It may be a reinstall, "
            "or a different device using the same name.");
    }

    const QRect bodyBound = bodyFm.boundingRect(
        QRect(0, 0, contentW, 1000), Qt::TextWordWrap, bodyText);

    // Each fp section = label line + gap + box
    const int fpBoxH     = kFpBoxPadV * 2 + captionFm.height();
    const int fpSectionH = captionFm.height() + kLabelGap + fpBoxH;
    const int fpCount    = (m_variant == Variant::Mismatch) ? 2 : 1;
    const int fpTotalH   = fpCount * fpSectionH + (fpCount - 1) * kSectionGap;

    m_cardH = kPadV
            + kAvatarSize
            + kSectionGap
            + bodyBound.height()
            + kSectionGap
            + fpTotalH
            + kSectionGap
            + kBtnH
            + kPadV;

    m_cardW = kCardW;
    m_cardX = (width()  - m_cardW) / 2;
    m_cardY = (height() - m_cardH) / 2;
}

// ── Factories ─────────────────────────────────────────────────────────────────

FingerprintDialog *FingerprintDialog::forUnknownPeer(const Peer &peer, QWidget *parent) {
    return new FingerprintDialog(Variant::Unknown, peer, {}, {}, parent);
}

FingerprintDialog *FingerprintDialog::forMismatch(const Peer &peer,
                                                   const QString &knownFp,
                                                   const QString &seenFp,
                                                   QWidget *parent) {
    return new FingerprintDialog(Variant::Mismatch, peer, knownFp, seenFp, parent);
}

// ── exec ──────────────────────────────────────────────────────────────────────

void FingerprintDialog::exec() {
    show();
    raise();
    activateWindow();
    QEventLoop loop;
    connect(this, &QWidget::destroyed, &loop, &QEventLoop::quit);
    // Also quit when m_done is set (buttons call close()).
    // Handled via close() → destroyed signal above.
    loop.exec();
}

// ── Painting ──────────────────────────────────────────────────────────────────

void FingerprintDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Dim overlay
    p.fillRect(rect(), QColor(0, 0, 0, kOverlayAlpha));

    // Card background
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::Color::WindowBg);
    p.drawRoundedRect(m_cardX, m_cardY, m_cardW, m_cardH,
                      Theme::Space::RadiusL, Theme::Space::RadiusL);

    const int cx   = m_cardX + kPadH;
    const int cw   = m_cardW - kPadH * 2;
    int       curY = m_cardY + kPadV;

    // ── Avatar + name row ─────────────────────────────────────────────────────
    {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(PeerItemDelegate::avatarColor(m_peer.id));
        p.drawEllipse(cx, curY, kAvatarSize, kAvatarSize);
        p.setFont(Fonts::semiBold(Theme::Font::SizeCaption));
        p.setPen(Qt::white);
        p.drawText(QRect(cx, curY, kAvatarSize, kAvatarSize),
                   Qt::AlignCenter, PeerItemDelegate::initials(m_peer.displayName));
        p.restore();

        const int nameX = cx + kAvatarSize + kAvatarGap;
        const int nameW = cw - kAvatarSize - kAvatarGap;

        const QFontMetrics nameFm(Fonts::semiBold(Theme::Font::SizeBody));
        const QFontMetrics addrFm(Fonts::regular(Theme::Font::SizeCaption));
        // Two-line block height: name line + gap + addr line
        const int blockH = nameFm.height() + kNameAddrGap + addrFm.height();
        const int blockTop = curY + (kAvatarSize - blockH) / 2;

        p.setFont(Fonts::semiBold(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextPrimary);
        p.drawText(nameX, blockTop + nameFm.ascent(),
                   nameFm.elidedText(m_peer.displayName, Qt::ElideRight, nameW));

        p.setFont(Fonts::regular(Theme::Font::SizeCaption));
        p.setPen(Theme::Color::TextSecondary);
        p.drawText(nameX, blockTop + nameFm.height() + kNameAddrGap + addrFm.ascent(),
                   addrFm.elidedText(m_peer.address, Qt::ElideRight, nameW));
    }
    curY += kAvatarSize + kSectionGap;

    // ── Body text ─────────────────────────────────────────────────────────────
    {
        QString body;
        if (m_variant == Variant::Unknown) {
            body = QStringLiteral("Do you want to trust this device?");
        } else {
            body = QStringLiteral(
                "This device’s fingerprint changed. It may be a reinstall, "
                "or a different device using the same name.");
        }

        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextPrimary);
        const QRect bodyRect = p.boundingRect(
            QRect(cx, curY, cw, 200), Qt::TextWordWrap, body);
        p.drawText(bodyRect, Qt::TextWordWrap, body);
        curY += bodyRect.height() + kSectionGap;
    }

    // ── Fingerprint box(es) ───────────────────────────────────────────────────
    auto paintFpBox = [&](const QString &label, const QString &fp) {
        p.setFont(Fonts::regular(Theme::Font::SizeCaption));
        p.setPen(Theme::Color::TextSecondary);
        const QFontMetrics captionFm(p.font());
        p.drawText(cx, curY + captionFm.ascent(), label);
        curY += captionFm.height() + kLabelGap;

        // Box
        const int boxH = kFpBoxPadV * 2 + captionFm.height();
        const QColor boxBg(Theme::Color::SearchBg);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(boxBg);
        p.drawRoundedRect(cx, curY, cw, boxH, kFpBoxR, kFpBoxR);

        // Monospaced fingerprint text (use regular — Inter is already clean at small sizes)
        p.setFont(Fonts::regular(Theme::Font::SizeCaption));
        p.setPen(Theme::Color::TextPrimary);
        const QString formatted = formatFp(fp);
        const QFontMetrics fpFm(p.font());
        const int maxTextW = cw - kFpBoxPadH * 2;
        const QString elided = fpFm.elidedText(formatted, Qt::ElideRight, maxTextW);
        p.drawText(cx + kFpBoxPadH, curY + kFpBoxPadV + fpFm.ascent(), elided);
        p.restore();

        curY += boxH + kSectionGap;
    };

    if (m_variant == Variant::Unknown) {
        paintFpBox(QStringLiteral("Fingerprint"), m_peer.fingerprint);
    } else {
        paintFpBox(QStringLiteral("Previous fingerprint"), m_knownFp);
        paintFpBox(QStringLiteral("New fingerprint"), m_seenFp);
    }

    // ── Buttons ───────────────────────────────────────────────────────────────
    // Layout right-to-left: [Later?] … [Block] [Primary]
    // Primary button is always rightmost and uses accent color.

    const int btnY = m_cardY + m_cardH - kPadV - kBtnH;

    auto measureBtn = [&](const QString &label) -> int {
        const QFontMetrics fm(Fonts::semiBold(Theme::Font::SizeBody));
        return fm.horizontalAdvance(label) + 32;  // 16px H-pad each side
    };

    const QString primaryLabel = (m_variant == Variant::Unknown)
        ? QStringLiteral("Trust")
        : QStringLiteral("Trust New");
    const QString secondaryLabel = QStringLiteral("Block");
    const QString tertiaryLabel  = QStringLiteral("Later");

    const int primaryW   = qMax(80, measureBtn(primaryLabel));
    const int secondaryW = qMax(80, measureBtn(secondaryLabel));
    const int tertiaryW  = qMax(80, measureBtn(tertiaryLabel));

    int rightEdge = m_cardX + m_cardW - kPadH;

    // Primary (accent filled)
    m_primaryRect = QRect(rightEdge - primaryW, btnY, primaryW, kBtnH);
    rightEdge    -= primaryW + kBtnGap;

    // Secondary (ghost / outlined)
    m_secondaryRect = QRect(rightEdge - secondaryW, btnY, secondaryW, kBtnH);
    rightEdge      -= secondaryW + kBtnGap;

    // Tertiary (text-only, Unknown variant only)
    if (m_variant == Variant::Unknown) {
        m_tertiaryRect = QRect(rightEdge - tertiaryW, btnY, tertiaryW, kBtnH);
    } else {
        m_tertiaryRect = QRect();
    }

    // Draw primary button
    {
        const QColor bg = m_primaryHover ? Theme::Color::AccentHover : Theme::Color::Accent;
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(m_primaryRect, kBtnRadius, kBtnRadius);
        p.setFont(Fonts::semiBold(Theme::Font::SizeBody));
        p.setPen(Qt::white);
        p.drawText(m_primaryRect, Qt::AlignCenter, primaryLabel);
        p.restore();
    }

    // Draw secondary button (outlined, explicit white fill to prevent
    // transparent-background bleed on Windows with WA_TranslucentBackground)
    {
        const QColor borderColor = m_secondaryHover
            ? Theme::Color::TextPrimary
            : Theme::Color::Divider;
        const QColor fillColor = m_secondaryHover
            ? Theme::Color::ItemHover
            : Theme::Color::WindowBg;
        p.save();
        p.setPen(QPen(borderColor, 1));
        p.setBrush(fillColor);
        p.drawRoundedRect(m_secondaryRect.adjusted(0, 0, -1, -1), kBtnRadius, kBtnRadius);
        p.setFont(Fonts::semiBold(Theme::Font::SizeBody));
        p.setPen(Theme::Color::TextPrimary);
        p.drawText(m_secondaryRect, Qt::AlignCenter, secondaryLabel);
        p.restore();
    }

    // Draw tertiary button (text-only)
    if (!m_tertiaryRect.isEmpty()) {
        p.setFont(Fonts::regular(Theme::Font::SizeBody));
        p.setPen(m_tertiaryHover ? Theme::Color::TextPrimary : Theme::Color::TextSecondary);
        p.drawText(m_tertiaryRect, Qt::AlignCenter, tertiaryLabel);
    }
}

// ── Input ─────────────────────────────────────────────────────────────────────

void FingerprintDialog::mouseMoveEvent(QMouseEvent *event) {
    const QPoint pos = event->pos();
    const bool ph = m_primaryRect.contains(pos);
    const bool sh = m_secondaryRect.contains(pos);
    const bool th = m_tertiaryRect.contains(pos);
    if (ph != m_primaryHover || sh != m_secondaryHover || th != m_tertiaryHover) {
        m_primaryHover   = ph;
        m_secondaryHover = sh;
        m_tertiaryHover  = th;
        setCursor((ph || sh || th) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void FingerprintDialog::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const QPoint pos = event->pos();

    if (m_primaryRect.contains(pos)) {
        m_result = UserResult::Trust;
        close();
    } else if (m_secondaryRect.contains(pos)) {
        m_result = UserResult::Block;
        close();
    } else if (!m_tertiaryRect.isEmpty() && m_tertiaryRect.contains(pos)) {
        m_result = UserResult::Later;
        close();
    }
    // Clicks outside the card are ignored (modal).
}

void FingerprintDialog::leaveEvent(QEvent *) {
    m_primaryHover   = false;
    m_secondaryHover = false;
    m_tertiaryHover  = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void FingerprintDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        m_result = UserResult::Later;
        close();
    }
}

bool FingerprintDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parent() && event->type() == QEvent::Resize) {
        auto *re = static_cast<QResizeEvent *>(event);
        resize(re->size());
        recomputeCard();
        update();
    }
    return false;
}
