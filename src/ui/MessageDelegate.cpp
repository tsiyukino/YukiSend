#include "MessageDelegate.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QImageReader>

#include "theme/Theme.h"
#include "theme/Fonts.h"
#include "PeerItemDelegate.h"
#include "utils/ImageUtils.h"
#include "utils/FileUtils.h"

// ── File-local layout constants ───────────────────────────────────────────────

static constexpr int kAvatarColW    = MessageDelegate::kAvatarSize + 10;
static constexpr int kFileBoxH      = 40;  // fixed height for File messages
static constexpr int kFileBoxW      = 260;
static constexpr int kFolderBoxW    = 300;
static constexpr int kFolderHeaderH = 36;  // name+status row inside folder box
static constexpr int kBoxRadius          =  8;
static constexpr int kBarH               =  4;
static constexpr int kBarRadius          =  2;
static constexpr int kActionFontSz       = 11; // pt
static constexpr int kFolderProgressH    = 20; // progress section height inside box (bar + padding)

// ── Tree layout constants (must be visible to both drawFolderBox and rowHeight)
static constexpr int kTreeRowH    = 20;
static constexpr int kTreeIndentW = 14;
static constexpr int kTreePadH    =  8; // horizontal padding inside box
static constexpr int kTreePadBot  =  6; // padding below last tree row
static constexpr int kTreeFontSz  = 11; // pt

// ── Static helpers ────────────────────────────────────────────────────────────

static QString statusText(const Message &msg) {
    switch (msg.status) {
    case MessageStatus::WaitingAccept:
        return msg.outgoing ? QStringLiteral("Waiting") : QStringLiteral("Incoming");
    case MessageStatus::Transferring: {
        if (msg.fileSize <= 0) return QStringLiteral("0%");
        return QStringLiteral("%1%")
            .arg(int(msg.bytesTransferred * 100 / msg.fileSize));
    }
    case MessageStatus::Done:         return QStringLiteral("✓");
    case MessageStatus::Denied:       return QStringLiteral("Denied");
    case MessageStatus::RequestAgain: return QStringLiteral("Requested");
    }
    return {};
}

static void drawProgressBar(QPainter *p, const QRect &box, const Message &msg) {
    if (msg.status != MessageStatus::Transferring) return;
    const double progress = msg.fileSize > 0
        ? double(msg.bytesTransferred) / double(msg.fileSize) : 0.0;

    const QRect barBg(box.left(), box.bottom() - kBarH, box.width(), kBarH);
    p->setPen(Qt::NoPen);
    p->setBrush(Theme::Color::ProgressBg);
    p->drawRoundedRect(barBg, kBarRadius, kBarRadius);

    const int fillW = int(barBg.width() * progress);
    if (fillW > 0) {
        p->setBrush(Theme::Color::ProgressFg);
        p->drawRoundedRect(QRect(barBg.left(), barBg.top(), fillW, kBarH),
                           kBarRadius, kBarRadius);
    }
}

static void drawFileBox(QPainter *p, int boxLeft, int boxTop,
                        const Message &msg, int hoverX) {
    const QRect box(boxLeft, boxTop, kFileBoxW, kFileBoxH);

    p->setPen(Qt::NoPen);
    p->setBrush(Theme::Color::SearchBg);
    p->drawRoundedRect(box, kBoxRadius, kBoxRadius);

    p->setFont(Fonts::medium(Theme::Font::SizeCaption));
    p->setPen(Theme::Color::TextPrimary);
    const QFontMetrics fm(p->font());
    const int nameAreaW = box.width() - 80 - MessageDelegate::kRowPadH * 2;
    const QString name  = fm.elidedText(msg.fileName, Qt::ElideMiddle, nameAreaW);
    p->drawText(box.left() + MessageDelegate::kRowPadH,
                box.top() + (kFileBoxH + fm.ascent() - fm.descent()) / 2,
                name);

    p->setFont(Fonts::regular(11));
    p->setPen(Theme::Color::TextSecondary);
    const QFontMetrics sfm(p->font());
    const QString stat = statusText(msg);
    p->drawText(box.right() - MessageDelegate::kRowPadH - sfm.horizontalAdvance(stat),
                box.top() + (kFileBoxH + sfm.ascent() - sfm.descent()) / 2,
                stat);

    drawProgressBar(p, box, msg);

    if (!msg.outgoing) {
        const int actionY = box.bottom() + 4;
        p->setFont(Fonts::regular(kActionFontSz));
        const QFontMetrics afm(p->font());
        const bool isHovered = (hoverX >= 0);
        const QColor baseColor(100, 100, 100);
        const QColor hoverColor(40, 40, 40);

        if (msg.status == MessageStatus::WaitingAccept) {
            const QString deny     = QStringLiteral("Deny");
            const QString acceptTo = QStringLiteral("Save To");
            const QString accept   = QStringLiteral("Accept");
            const int kGap  = 10;
            const int denyW = afm.horizontalAdvance(deny);
            const int toW   = afm.horizontalAdvance(acceptTo);
            const int accW  = afm.horizontalAdvance(accept);
            const int denyX = box.right() - denyW;
            const int toX   = denyX - kGap - toW;
            const int accX  = toX   - kGap - accW;
            const int textY = actionY + afm.ascent();
            const int midDT = toX + toW + kGap / 2;
            const int midTA = accX + accW + kGap / 2;

            p->setPen((isHovered && hoverX >= midDT)               ? hoverColor : baseColor);
            p->drawText(denyX, textY, deny);
            p->setPen((isHovered && hoverX >= midTA && hoverX < midDT) ? hoverColor : baseColor);
            p->drawText(toX, textY, acceptTo);
            p->setPen((isHovered && hoverX >= accX - 4 && hoverX < midTA) ? hoverColor : baseColor);
            p->drawText(accX, textY, accept);

        } else if (msg.status == MessageStatus::Done
                || msg.status == MessageStatus::Denied) {
            const QString req  = QStringLiteral("Request Again");
            const int reqX     = box.right() - afm.horizontalAdvance(req);
            const bool overReq = isHovered && (hoverX >= reqX - 4);
            p->setPen(overReq ? hoverColor : baseColor);
            p->drawText(reqX, actionY + afm.ascent(), req);
        }
    }
}

// ── Folder box (header + inline tree) ────────────────────────────────────────

// Height of the tree rows section inside the folder box.
// Returns 0 when there is no tree (receiver hasn't accepted yet).
static int folderTreeSectionH(const FolderTreeState *tree) {
    if (!tree || !tree->ready()) return 0;
    const auto &vis = tree->visible();
    const int rowCount = vis.rows.size() + vis.moreAt.size();
    return qMax(1, rowCount) * kTreeRowH + kTreePadBot;
}

// Total height of the whole folder box (header + tree rows + optional progress section).
static int folderBoxH(const FolderTreeState *tree, bool transferring = false) {
    return kFolderHeaderH + folderTreeSectionH(tree)
         + (transferring ? kFolderProgressH : 0);
}

// Draw one tree row's directory triangle at (triCX, triCY).
static void drawTreeTriangle(QPainter *p, int triCX, int triCY,
                              bool expanded, const QColor &col)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(col);
    QPolygon tri;
    if (expanded)
        tri << QPoint(triCX - 4, triCY - 2) << QPoint(triCX + 4, triCY - 2) << QPoint(triCX, triCY + 3);
    else
        tri << QPoint(triCX - 2, triCY - 4) << QPoint(triCX - 2, triCY + 4) << QPoint(triCX + 3, triCY);
    p->drawPolygon(tri);
    p->restore();
}

// Draw the folder message box (header + tree) starting at (boxLeft, boxTop).
static void drawFolderBox(QPainter *p, int boxLeft, int boxTop,
                           const Message &msg, int hoverX, int hoverY,
                           const FolderTreeState *tree)
{
    const bool transferring = (msg.status == MessageStatus::Transferring);
    const int boxH = folderBoxH(tree, transferring);
    const QRect box(boxLeft, boxTop, kFolderBoxW, boxH);

    // Box background
    p->setPen(Qt::NoPen);
    p->setBrush(Theme::Color::SearchBg);
    p->drawRoundedRect(box, kBoxRadius, kBoxRadius);

    // ── Header row ────────────────────────────────────────────────────────────
    // Divider between header and tree area (only when tree is present)
    if (tree && tree->ready())
        p->fillRect(QRect(boxLeft, boxTop + kFolderHeaderH - 1, kFolderBoxW, 1),
                    Theme::Color::Divider);

    // Folder name
    p->setFont(Fonts::medium(Theme::Font::SizeCaption));
    p->setPen(Theme::Color::TextPrimary);
    const QFontMetrics nfm(p->font());
    const int nameAreaW = kFolderBoxW - 80 - MessageDelegate::kRowPadH * 2;
    const QString name  = nfm.elidedText(msg.fileName, Qt::ElideMiddle, nameAreaW);
    p->drawText(boxLeft + MessageDelegate::kRowPadH,
                boxTop + (kFolderHeaderH + nfm.ascent() - nfm.descent()) / 2, name);

    // Status
    p->setFont(Fonts::regular(11));
    p->setPen(Theme::Color::TextSecondary);
    const QFontMetrics sfm(p->font());
    const QString stat = statusText(msg);
    p->drawText(boxLeft + kFolderBoxW - MessageDelegate::kRowPadH - sfm.horizontalAdvance(stat),
                boxTop + (kFolderHeaderH + sfm.ascent() - sfm.descent()) / 2, stat);

    // ── Tree rows ─────────────────────────────────────────────────────────────
    if (tree && tree->ready()) {
    const auto &vis = tree->visible();
    p->setFont(Fonts::regular(kTreeFontSz));
    const QFontMetrics fm(p->font());
    const int treeTop = boxTop + kFolderHeaderH;

    // Collect "more" positions sorted
    QList<int> morePos = vis.moreAt.keys();
    std::sort(morePos.begin(), morePos.end());

    int row = 0, ri = 0, mi = 0;
    while (ri < vis.rows.size() || mi < morePos.size()) {
        // "… N more" sentinels
        while (mi < morePos.size() && morePos[mi] <= ri) {
            const TreeMore &more = vis.moreAt[morePos[mi]];
            const int rowY  = treeTop + row * kTreeRowH;
            const int textY = rowY + (kTreeRowH + fm.ascent() - fm.descent()) / 2;
            const bool hov  = (hoverY >= rowY && hoverY < rowY + kTreeRowH
                             && hoverX >= boxLeft && hoverX < boxLeft + kFolderBoxW);
            p->setPen(hov ? Theme::Color::Accent : Theme::Color::TextSecondary);
            p->drawText(boxLeft + kTreePadH * 2, textY,
                        QStringLiteral("… %1 more").arg(more.total - more.shown));
            ++mi; ++row;
        }
        if (ri >= vis.rows.size()) break;

        const TreeRow &tr   = vis.rows[ri];
        const int rowY  = treeTop + row * kTreeRowH;
        const int textY = rowY + (kTreeRowH + fm.ascent() - fm.descent()) / 2;
        const int indX  = boxLeft + kTreePadH + tr.depth * kTreeIndentW;
        const bool hov  = (hoverY >= rowY && hoverY < rowY + kTreeRowH
                         && hoverX >= boxLeft && hoverX < boxLeft + kFolderBoxW);
        const QColor rowCol = hov ? Theme::Color::TextPrimary : Theme::Color::TextSecondary;

        if (tr.isDir) {
            drawTreeTriangle(p, indX + 5, rowY + kTreeRowH / 2, tr.expanded, rowCol);
            p->setPen(rowCol);
            p->setFont(Fonts::medium(kTreeFontSz));
            const int nameX    = indX + 14;
            const int nameMaxW = boxLeft + kFolderBoxW - nameX - kTreePadH;
            p->drawText(nameX, textY, fm.elidedText(tr.name, Qt::ElideRight, nameMaxW));
            p->setFont(Fonts::regular(kTreeFontSz));
        } else {
            p->setPen(rowCol);
            const int nameX = indX + 10;
            const QString szLabel = FileUtils::formatSize(tr.size);
            const int szW      = fm.horizontalAdvance(szLabel);
            const int nameMaxW = boxLeft + kFolderBoxW - nameX - szW - kTreePadH * 2;
            p->drawText(indX + 2, textY, QStringLiteral("·"));
            p->drawText(nameX, textY, fm.elidedText(tr.name, Qt::ElideRight, nameMaxW));
            const QColor dimCol(rowCol.red(), rowCol.green(), rowCol.blue(),
                                hov ? 160 : 110);
            p->setPen(dimCol);
            p->drawText(boxLeft + kFolderBoxW - kTreePadH - szW, textY, szLabel);
        }
        ++ri; ++row;
    }
    } // end tree rows block

    // ── Progress bar below tree (Transferring only) ───────────────────────────
    if (transferring) {
        const int barSectionTop = boxTop + kFolderHeaderH + folderTreeSectionH(tree);
        const QRect barRect(boxLeft + kTreePadH,
                            barSectionTop + (kFolderProgressH - kBarH) / 2,
                            kFolderBoxW - kTreePadH * 2, kBarH);
        const double progress = msg.fileSize > 0
            ? double(msg.bytesTransferred) / double(msg.fileSize) : 0.0;
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::Color::ProgressBg);
        p->drawRoundedRect(barRect, kBarRadius, kBarRadius);
        const int fillW = int(barRect.width() * progress);
        if (fillW > 0) {
            p->setBrush(Theme::Color::ProgressFg);
            p->drawRoundedRect(QRect(barRect.left(), barRect.top(), fillW, kBarH),
                               kBarRadius, kBarRadius);
        }
    }

    // ── Accept/Deny action strip (incoming only, below the box) ──────────────
    if (!msg.outgoing) {
        const int actionY = boxTop + boxH + 4;
        p->setFont(Fonts::regular(kActionFontSz));
        const QFontMetrics afm(p->font());
        const bool isHov  = (hoverX >= 0);
        const QColor base(100, 100, 100);
        const QColor hov2(40,  40,  40);

        if (msg.status == MessageStatus::WaitingAccept) {
            const QString deny     = QStringLiteral("Deny");
            const QString acceptTo = QStringLiteral("Save To");
            const QString accept   = QStringLiteral("Accept");
            const int kGap  = 10;
            const int toW   = afm.horizontalAdvance(acceptTo);
            const int accW  = afm.horizontalAdvance(accept);
            const int denyX = boxLeft + kFolderBoxW - afm.horizontalAdvance(deny);
            const int toX   = denyX - kGap - toW;
            const int accX  = toX   - kGap - accW;
            const int textY = actionY + afm.ascent();
            const int midDT = toX  + toW  + kGap / 2;
            const int midTA = accX + accW + kGap / 2;
            p->setPen((isHov && hoverX >= midDT)               ? hov2 : base);
            p->drawText(denyX, textY, deny);
            p->setPen((isHov && hoverX >= midTA && hoverX < midDT) ? hov2 : base);
            p->drawText(toX, textY, acceptTo);
            p->setPen((isHov && hoverX >= accX - 4 && hoverX < midTA) ? hov2 : base);
            p->drawText(accX, textY, accept);
        } else if (msg.status == MessageStatus::Done
                || msg.status == MessageStatus::Denied) {
            const QString req  = QStringLiteral("Request Again");
            const int reqX     = boxLeft + kFolderBoxW - afm.horizontalAdvance(req);
            p->setPen((isHov && hoverX >= reqX - 4) ? hov2 : base);
            p->drawText(reqX, actionY + afm.ascent(), req);
        }
    }
}

static void drawAvatar(QPainter *p, int cx, int cy,
                       const QColor &color, const QString &inits) {
    const int r = MessageDelegate::kAvatarSize;
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawEllipse(cx - r / 2, cy - r / 2, r, r);
    p->setFont(Fonts::semiBold(Theme::Font::SizeCaption));
    p->setPen(Qt::white);
    p->drawText(QRect(cx - r / 2, cy - r / 2, r, r), Qt::AlignCenter, inits);
}

// Draw the image preview area (thumbnail or locked placeholder).
static void drawImagePreview(QPainter *p, int boxLeft, int boxTop,
                             const Message &msg, const QImage &thumb,
                             int hoverX) {
    const int pw = Theme::Space::ImagePreviewMaxW;
    const int ph = Theme::Space::ImagePreviewH;
    const int r  = Theme::Space::ImagePreviewRadius;
    const QRect box(boxLeft, boxTop, pw, ph);

    p->setRenderHint(QPainter::Antialiasing);

    // Clip path used for both the image and the overlay
    QPainterPath clip;
    clip.addRoundedRect(box, r, r);

    if (msg.status == MessageStatus::Done && !thumb.isNull()) {
        // Sharp thumbnail — draw then add a hover overlay.
        p->save();
        p->setClipPath(clip);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawImage(box, thumb);
        if (hoverX >= box.left() && hoverX <= box.right())
            p->fillRect(box, QColor(0, 0, 0, 40));
        p->restore();
    } else if (!thumb.isNull()) {
        // Blurred preview — draw blurred thumbnail, darken it, then overlay
        // a centered circular Accept button.
        p->save();
        p->setClipPath(clip);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawImage(box, thumb);
        // Darken overlay so the button reads clearly
        p->fillRect(box, QColor(0, 0, 0, 80));
        p->restore();

        // Centered download / accept circle button
        static constexpr int kBtnR = 22;
        const QPoint center = box.center();
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 200));
        p->drawEllipse(center, kBtnR, kBtnR);

        // Arrow-down icon drawn manually: stem + chevron
        p->setPen(QPen(QColor(40, 40, 40), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const int cx = center.x();
        const int cy = center.y();
        p->drawLine(cx, cy - 9, cx, cy + 4);           // vertical stem
        p->drawLine(cx - 6, cy - 1, cx,     cy + 6);   // left wing
        p->drawLine(cx + 6, cy - 1, cx,     cy + 6);   // right wing
        p->drawLine(cx - 8, cy + 9, cx + 8, cy + 9);   // base bar

        // Transfer progress bar if receiving
        drawProgressBar(p, box, msg);
    } else {
        // No thumbnail at all (outgoing waiting, or old message with no thumbData)
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::Color::SearchBg);
        p->drawRoundedRect(box, r, r);

        p->setFont(Fonts::regular(Theme::Font::SizeCaption));
        p->setPen(Theme::Color::TextSecondary);
        const QString label = (msg.status == MessageStatus::Transferring)
            ? QStringLiteral("Receiving…")
            : (msg.outgoing ? QStringLiteral("Waiting for accept")
                            : QStringLiteral("Accept to view"));
        p->drawText(box, Qt::AlignCenter, label);

        drawProgressBar(p, box, msg);
    }

    // Accept / Deny strip for incoming image (same as file)
    if (!msg.outgoing) {
        const int actionY = box.bottom() + 4;
        p->setFont(Fonts::regular(kActionFontSz));
        const QFontMetrics afm(p->font());
        const bool isHovered = (hoverX >= 0);
        const QColor baseColor(100, 100, 100);
        const QColor hoverColor(40, 40, 40);

        if (msg.status == MessageStatus::WaitingAccept) {
            const QString deny   = QStringLiteral("Deny");
            const QString accept = QStringLiteral("Accept");
            const int kGap  = 10;
            const int denyW = afm.horizontalAdvance(deny);
            const int accW  = afm.horizontalAdvance(accept);
            const int denyX = box.right() - denyW;
            const int accX  = denyX - kGap - accW;
            const int textY = actionY + afm.ascent();
            const int mid   = accX + accW + kGap / 2;

            p->setPen((isHovered && hoverX >= mid)       ? hoverColor : baseColor);
            p->drawText(denyX, textY, deny);
            p->setPen((isHovered && hoverX < mid && hoverX >= accX - 4) ? hoverColor : baseColor);
            p->drawText(accX, textY, accept);

        } else if (msg.status == MessageStatus::Done
                || msg.status == MessageStatus::Denied) {
            const QString req  = QStringLiteral("Request Again");
            const int reqX     = box.right() - afm.horizontalAdvance(req);
            const bool overReq = isHovered && (hoverX >= reqX - 4);
            p->setPen(overReq ? hoverColor : baseColor);
            p->drawText(reqX, actionY + afm.ascent(), req);
        }
    }
}

// ── MessageDelegateRenderer ───────────────────────────────────────────────────

void MessageDelegateRenderer::invalidate(qint64 messageId) {
    m_thumbCache.invalidate(messageId);
}

void MessageDelegateRenderer::clearCache() {
    m_thumbCache.clearMemory();
}

int MessageDelegateRenderer::rowHeight(const Message &msg,
                                       const FolderTreeState *tree) const {
    switch (msg.type) {
    case MessageType::Folder: {
        const bool xferring = (msg.status == MessageStatus::Transferring);
        const int base = MessageDelegate::kRowPadV + MessageDelegate::kNameH
                       + folderBoxH(tree, xferring) + MessageDelegate::kRowPadV;
        if (msg.outgoing) return base;
        const QFontMetrics afm(Fonts::regular(kActionFontSz));
        return base + 4 + afm.height();
    }
    case MessageType::File: {
        const int base = MessageDelegate::kRowPadV + MessageDelegate::kNameH
                       + kFileBoxH + MessageDelegate::kRowPadV;
        if (msg.outgoing) return base;
        const QFontMetrics afm(Fonts::regular(kActionFontSz));
        return base + 4 + afm.height();
    }
    case MessageType::Image: {
        // Same structure as File for incoming (action strip below preview)
        const int base = MessageDelegate::kRowImage;
        if (msg.outgoing) return base;
        const QFontMetrics afm(Fonts::regular(kActionFontSz));
        return base + 4 + afm.height();
    }
    default:
        return MessageDelegate::kRowText;
    }
}

QImage MessageDelegateRenderer::thumbnail(const Message &msg) {
    QImage cached = m_thumbCache.get(msg.id);
    if (!cached.isNull())
        return cached;

    QImage img;
    if (msg.status == MessageStatus::Done && !msg.filePath.isEmpty()) {
        QImageReader reader(msg.filePath);
        reader.setScaledSize(QSize(Theme::Space::ImagePreviewMaxW * 2,
                                  Theme::Space::ImagePreviewH * 2));
        img = reader.read();
    } else if (!msg.thumbData.isEmpty()) {
        const QImage decoded = ImageUtils::decodeJpeg(msg.thumbData);
        if (!decoded.isNull()) {
            const QImage scaled = decoded.scaled(
                QSize(Theme::Space::ImagePreviewMaxW, Theme::Space::ImagePreviewH),
                Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            img = ImageUtils::boxBlur(scaled, 6);
        }
    }
    if (!img.isNull())
        m_thumbCache.put(msg.id, img);
    return img;
}

void MessageDelegateRenderer::draw(QPainter *p, const QRect &rect,
                                   const Message &msg, int hoverX, int hoverY,
                                   const QString &senderName,
                                   const QColor &avatarColor,
                                   TextSelection selection,
                                   bool copyFlash,
                                   const FolderTreeState *tree)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);

    const bool out = msg.outgoing;

    const int avatarCX = out
        ? rect.right() - MessageDelegate::kRowPadH - MessageDelegate::kAvatarSize / 2
        : rect.left()  + MessageDelegate::kRowPadH + MessageDelegate::kAvatarSize / 2;
    const int avatarCY = rect.top() + MessageDelegate::kRowPadV
                       + MessageDelegate::kAvatarSize / 2;

    drawAvatar(p, avatarCX, avatarCY, avatarColor,
               PeerItemDelegate::initials(senderName));

    // contentLeft/Right define the text/content area — avatar column excluded.
    const int contentLeft  = out
        ? rect.left()  + MessageDelegate::kRowPadH
        : rect.left()  + MessageDelegate::kRowPadH + kAvatarColW;
    const int contentRight = out
        ? rect.right() - MessageDelegate::kRowPadH - kAvatarColW
        : rect.right() - MessageDelegate::kRowPadH;

    // Sender name — aligned to the same left/right edge as the content below it.
    p->setFont(Fonts::semiBold(Theme::Font::SizeCaption));
    p->setPen(avatarColor);
    const QFontMetrics nfm(p->font());
    if (out) {
        const int nameX = contentRight - nfm.horizontalAdvance(senderName);
        p->drawText(nameX, rect.top() + MessageDelegate::kRowPadV + nfm.ascent(),
                    senderName);
    } else {
        p->drawText(contentLeft, rect.top() + MessageDelegate::kRowPadV + nfm.ascent(),
                    senderName);
    }

    // Content sits below name with an explicit gap.
    const int contentTop = rect.top() + MessageDelegate::kRowPadV
                         + MessageDelegate::kNameH + MessageDelegate::kNameGap;

    switch (msg.type) {
    case MessageType::Text: {
        p->setFont(Fonts::regular(Theme::Font::SizeBody));
        const QFontMetrics fm(p->font());
        const int textX = out
            ? contentRight - fm.horizontalAdvance(msg.text)
            : contentLeft;
        const int textY = contentTop + fm.ascent();

        // Selection highlight — drawn before text so text renders on top
        if (!selection.empty() && !msg.text.isEmpty()) {
            const int selFrom = qBound(0, selection.from, msg.text.size());
            const int selTo   = qBound(0, selection.to,   msg.text.size());
            if (selFrom < selTo) {
                const int x0 = textX + fm.horizontalAdvance(msg.text.left(selFrom));
                const int x1 = textX + fm.horizontalAdvance(msg.text.left(selTo));
                p->setPen(Qt::NoPen);
                p->setBrush(Theme::Color::Accent.lighter(170));
                p->drawRect(QRect(x0, contentTop, x1 - x0,
                                  fm.ascent() + fm.descent()));
            }
        }

        p->setPen(Theme::Color::TextPrimary);
        p->drawText(textX, textY, msg.text);

        // Copy glyph in the name row, on the side away from the avatar.
        // Shows ⎘ on hover, swaps to ✓ for 600ms after a successful copy.
        if (hoverX >= 0 || copyFlash) {
            const QString glyph = copyFlash
                ? QStringLiteral("✓")
                : QStringLiteral("⎘");
            p->setFont(Fonts::regular(Theme::Font::SizeCaption));
            const QFontMetrics cfm(p->font());
            const int glyphW = cfm.horizontalAdvance(glyph);
            const int nameBaseline = rect.top() + MessageDelegate::kRowPadV + nfm.ascent();
            int glyphX;
            if (out) {
                const int nameX = contentRight - nfm.horizontalAdvance(senderName);
                glyphX = nameX - glyphW - 6;
            } else {
                glyphX = contentLeft + nfm.horizontalAdvance(senderName) + 6;
            }
            const QColor glyphColor = copyFlash
                ? Theme::Color::Accent
                : QColor(Theme::Color::TextSecondary.red(),
                         Theme::Color::TextSecondary.green(),
                         Theme::Color::TextSecondary.blue(), 140);
            p->setPen(glyphColor);
            p->drawText(glyphX, nameBaseline, glyph);
        }
        break;
    }
    case MessageType::File: {
        const int boxLeft = out ? contentRight - kFileBoxW : contentLeft;
        drawFileBox(p, boxLeft, contentTop, msg, hoverX);
        break;
    }
    case MessageType::Folder: {
        const int boxLeft = out ? contentRight - kFolderBoxW : contentLeft;
        drawFolderBox(p, boxLeft, contentTop, msg, hoverX, hoverY, tree);
        break;
    }
    case MessageType::Image: {
        const int pw      = Theme::Space::ImagePreviewMaxW;
        const int boxLeft = out ? contentRight - pw : contentLeft;
        const QImage thumb = thumbnail(msg);
        drawImagePreview(p, boxLeft, contentTop, msg, thumb, hoverX);
        break;
    }
    }

    // Timestamp — tertiary, 60% opacity per design spec
    p->setFont(Fonts::regular(11));
    {
        QColor tsColor = Theme::Color::TextSecondary;
        tsColor.setAlphaF(0.60);
        p->setPen(tsColor);
    }
    const QString ts = msg.timestamp.toString(QStringLiteral("hh:mm"));
    const QFontMetrics tfm(p->font());
    const int tsX = out
        ? rect.left() + MessageDelegate::kRowPadH
        : rect.right() - MessageDelegate::kRowPadH - tfm.horizontalAdvance(ts);
    p->drawText(tsX, rect.top() + MessageDelegate::kRowPadV + tfm.ascent(), ts);

    p->restore();
}

// static
int MessageDelegateRenderer::charAtX(const Message &msg, int absoluteX, int contentLeft) {
    if (msg.text.isEmpty()) return 0;
    const QFontMetrics fm(Fonts::regular(Theme::Font::SizeBody));
    const int relX = absoluteX - contentLeft;
    if (relX <= 0) return 0;
    // Linear scan: find first index where cumulative advance exceeds relX.
    // For typical chat message lengths this is fast enough; could binary-search
    // if messages ever become very long.
    for (int i = 1; i <= msg.text.size(); ++i) {
        if (fm.horizontalAdvance(msg.text.left(i)) > relX)
            return i - 1;
    }
    return msg.text.size();
}

int MessageDelegateRenderer::hitTestAction(const QRect &rect, const Message &msg,
                                           int clickX, int clickY,
                                           const QString &senderName,
                                           const FolderTreeState *tree,
                                           QString *outTogglePath) const
{
    if (msg.type == MessageType::Text) {
        // Hit-test the copy glyph in the name row, opposite side from avatar.
        const int contentLeft  = msg.outgoing
            ? rect.left()  + MessageDelegate::kRowPadH
            : rect.left()  + MessageDelegate::kRowPadH + kAvatarColW;
        const int contentRight = msg.outgoing
            ? rect.right() - MessageDelegate::kRowPadH - kAvatarColW
            : rect.right() - MessageDelegate::kRowPadH;

        const QFontMetrics nfm(Fonts::semiBold(Theme::Font::SizeCaption));
        const QFontMetrics cfm(Fonts::regular(Theme::Font::SizeCaption));
        // Use the wider of the two possible glyphs for hit zone sizing
        const int glyphW = qMax(cfm.horizontalAdvance(QStringLiteral("⎘")),
                                cfm.horizontalAdvance(QStringLiteral("✓")));

        int glyphX;
        if (msg.outgoing) {
            const int nameX = contentRight - nfm.horizontalAdvance(senderName);
            glyphX = nameX - glyphW - 6;
        } else {
            glyphX = contentLeft + nfm.horizontalAdvance(senderName) + 6;
        }
        const int nameBaseline = rect.top() + MessageDelegate::kRowPadV + nfm.ascent();
        const int btnY0 = nameBaseline - nfm.ascent() - 2;
        const int btnY1 = nameBaseline + nfm.descent() + 2;

        if (clickX >= glyphX - 4 && clickX <= glyphX + glyphW + 4
         && clickY >= btnY0       && clickY <= btnY1)
            return 5;
        return 0;
    }
    const int contentTop  = rect.top()  + MessageDelegate::kRowPadV + MessageDelegate::kNameH;
    const int contentLeft = rect.left() + MessageDelegate::kRowPadH + kAvatarColW;

    // Image: check preview click (Done state) or action strip
    if (msg.type == MessageType::Image) {
        if (msg.outgoing) {
            if (msg.status == MessageStatus::Done) {
                const int contentRight2 = rect.right() - MessageDelegate::kRowPadH - kAvatarColW;
                const QRect box2(contentRight2 - Theme::Space::ImagePreviewMaxW,
                                 contentTop, Theme::Space::ImagePreviewMaxW,
                                 Theme::Space::ImagePreviewH);
                if (box2.contains(clickX, clickY)) return 4;
            }
            return 0;
        }
        if (msg.status != MessageStatus::WaitingAccept
         && msg.status != MessageStatus::Done
         && msg.status != MessageStatus::Denied) return 0;
        const QRect box(contentLeft, contentTop,
                        Theme::Space::ImagePreviewMaxW, Theme::Space::ImagePreviewH);
        if (msg.status == MessageStatus::Done && box.contains(clickX, clickY))
            return 4;

        const int actionY      = box.bottom() + 4;
        const QFontMetrics afm(Fonts::regular(kActionFontSz));
        const int actionBottom = actionY + afm.height();
        if (clickY < actionY || clickY > actionBottom) return 0;

        if (msg.status == MessageStatus::WaitingAccept) {
            const QString deny   = QStringLiteral("Deny");
            const QString accept = QStringLiteral("Accept");
            const int kGap  = 10;
            const int denyX = box.right() - afm.horizontalAdvance(deny);
            const int accX  = denyX - kGap - afm.horizontalAdvance(accept);
            const int mid   = accX + afm.horizontalAdvance(accept) + kGap / 2;
            if (clickX >= mid)       return -1;
            if (clickX >= accX - 4)  return  1;
        } else if (msg.status == MessageStatus::Done
                || msg.status == MessageStatus::Denied) {
            const QString req = QStringLiteral("Request Again");
            const int reqX = box.right() - afm.horizontalAdvance(req);
            if (clickX >= reqX - 4) return 2;
        }
        return 0;
    }

    // ── Folder ────────────────────────────────────────────────────────────────
    if (msg.type == MessageType::Folder) {
        const int boxLeft  = msg.outgoing
            ? (rect.right() - MessageDelegate::kRowPadH - kAvatarColW) - kFolderBoxW
            : contentLeft;
        const int treeTop  = contentTop + kFolderHeaderH;

        // Tree hit test (both outgoing and incoming)
        if (tree && tree->ready()) {
            const auto &vis = tree->visible();
            QList<int> morePos = vis.moreAt.keys();
            std::sort(morePos.begin(), morePos.end());
            int row = 0, ri = 0, mi = 0;
            while (ri < vis.rows.size() || mi < morePos.size()) {
                while (mi < morePos.size() && morePos[mi] <= ri) {
                    const int rowY0 = treeTop + row * kTreeRowH;
                    if (clickY >= rowY0 && clickY < rowY0 + kTreeRowH
                     && clickX >= boxLeft && clickX < boxLeft + kFolderBoxW) {
                        if (outTogglePath)
                            *outTogglePath = vis.moreAt[morePos[mi]].parentPath;
                        return 7;
                    }
                    ++mi; ++row;
                }
                if (ri >= vis.rows.size()) break;
                const int rowY0 = treeTop + row * kTreeRowH;
                if (clickY >= rowY0 && clickY < rowY0 + kTreeRowH
                 && clickX >= boxLeft && clickX < boxLeft + kFolderBoxW) {
                    const TreeRow &tr = vis.rows[ri];
                    if (tr.isDir) {
                        if (outTogglePath) *outTogglePath = tr.relPath;
                        return 6;
                    }
                    return 0;
                }
                ++ri; ++row;
            }
        }

        // Accept/Deny strip (incoming only, below the box)
        if (!msg.outgoing) {
            const bool xferring2 = (msg.status == MessageStatus::Transferring);
            const QRect box(boxLeft, contentTop, kFolderBoxW, folderBoxH(tree, xferring2));
            const int actionY = box.bottom() + 4;
            const QFontMetrics afm(Fonts::regular(kActionFontSz));
            if (clickY >= actionY && clickY <= actionY + afm.height()) {
                if (msg.status == MessageStatus::WaitingAccept) {
                    const QString deny     = QStringLiteral("Deny");
                    const QString acceptTo = QStringLiteral("Save To");
                    const QString accept   = QStringLiteral("Accept");
                    const int kGap  = 10;
                    const int denyX = box.right() - afm.horizontalAdvance(deny);
                    const int toX   = denyX - kGap - afm.horizontalAdvance(acceptTo);
                    const int accX  = toX - kGap - afm.horizontalAdvance(accept);
                    if (clickX >= toX + afm.horizontalAdvance(acceptTo) + kGap / 2) return -1;
                    if (clickX >= accX + afm.horizontalAdvance(accept) + kGap / 2)  return  3;
                    if (clickX >= accX - 4)                                          return  1;
                } else if (msg.status == MessageStatus::Done
                        || msg.status == MessageStatus::Denied) {
                    const QString req = QStringLiteral("Request Again");
                    const int reqX = box.right() - afm.horizontalAdvance(req);
                    if (clickX >= reqX - 4) return 2;
                }
            }
        }
        return 0;
    }

    // ── File ──────────────────────────────────────────────────────────────────
    const int boxLeft = msg.outgoing
        ? (rect.right() - MessageDelegate::kRowPadH - kAvatarColW) - kFileBoxW
        : contentLeft;
    const QRect box(boxLeft, contentTop, kFileBoxW, kFileBoxH);

    if (msg.outgoing) return 0;

    const int actionY      = box.bottom() + 4;
    const QFontMetrics afm(Fonts::regular(kActionFontSz));
    const int actionBottom = actionY + afm.height();
    if (clickY < actionY || clickY > actionBottom) return 0;

    if (msg.status == MessageStatus::WaitingAccept) {
        const QString deny     = QStringLiteral("Deny");
        const QString acceptTo = QStringLiteral("Save To");
        const QString accept   = QStringLiteral("Accept");
        const int kGap  = 10;
        const int denyX = box.right() - afm.horizontalAdvance(deny);
        const int toX   = denyX - kGap - afm.horizontalAdvance(acceptTo);
        const int accX  = toX   - kGap - afm.horizontalAdvance(accept);
        const int midDT = toX + afm.horizontalAdvance(acceptTo) + kGap / 2;
        const int midTA = accX + afm.horizontalAdvance(accept)  + kGap / 2;
        if (clickX >= midDT)    return -1;
        if (clickX >= midTA)    return  3;
        if (clickX >= accX - 4) return  1;
        return 0;
    }
    if (msg.status == MessageStatus::Done || msg.status == MessageStatus::Denied) {
        const QString req = QStringLiteral("Request Again");
        const int reqX = box.right() - afm.horizontalAdvance(req);
        if (clickX >= reqX - 4 && clickX <= box.right() + 4) return 2;
    }
    return 0;
}
