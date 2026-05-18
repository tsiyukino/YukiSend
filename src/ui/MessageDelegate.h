#pragma once

#include <QPainter>
#include <QRect>
#include <QImage>

#include "model/Message.h"
#include "theme/Theme.h"
#include "FolderTreeState.h"
#include "utils/ThumbCache.h"

// Row layout constants — aliases into Theme::Space for cross-file consistency.
namespace MessageDelegate {

static constexpr int kAvatarSize = Theme::Space::MsgAvatarSize;
static constexpr int kRowPadV    = Theme::Space::MsgRowPadV;
static constexpr int kRowPadH    = Theme::Space::MsgRowPadH;
static constexpr int kNameH      = Theme::Space::MsgNameH;
static constexpr int kNameGap    = Theme::Space::MsgNameGap;
// kRowText: padV + nameH + nameGap + body(13pt ~16px) + padV
static constexpr int kRowText    = kRowPadV + kNameH + kNameGap + 16 + kRowPadV;
// kRowImage: padV + nameH + nameGap + ImagePreviewH + padV
static constexpr int kRowImage   = kRowPadV + kNameH + kNameGap
                                 + Theme::Space::ImagePreviewH + kRowPadV;
// File/folder row heights computed dynamically in rowHeight()

} // namespace MessageDelegate

// Character range within a text message. from is inclusive, to is exclusive.
struct TextSelection {
    int  from = 0;
    int  to   = 0;
    bool empty() const { return from >= to; }
};

// Stateful delegate — owns a thumbnail cache keyed by message id.
// One instance per ChatView; call draw() and hitTestAction() per paint frame.
class MessageDelegateRenderer {
public:
    MessageDelegateRenderer() = default;

    // Invalidate cached thumbnail for a message (call when status changes to Done).
    void invalidate(qint64 messageId);

    // Clear entire cache (call when peer changes).
    void clearCache();

    // tree may be nullptr (no tree / not a folder message).
    int rowHeight(const Message &msg, const FolderTreeState *tree = nullptr) const;

    // hoverX/hoverY: widget-relative mouse pos, (-1,-1) if not hovered.
    // selection: active text selection for this message (ignored for non-text).
    // copyFlash: true for the brief "copied!" highlight after clicking Copy.
    // tree: folder tree state; nullptr if not applicable.
    void draw(QPainter *p, const QRect &rect, const Message &msg,
              int hoverX, int hoverY,
              const QString &senderName,
              const QColor &avatarColor,
              TextSelection selection = {},
              bool copyFlash = false,
              const FolderTreeState *tree = nullptr);

    // Returns hit code: 1=Accept, 3=AcceptTo, -1=Deny, 2=RequestAgain,
    //   4=ImageOpen, 5=CopyText, 6=FolderTreeToggle, 7=FolderTreeLoadMore, 0=miss.
    // When code is 6, outTogglePath receives the relPath of the toggled dir.
    // When code is 7, outTogglePath receives the parentPath for loadMore.
    // senderName must match what was passed to draw() so copy-glyph positions align.
    int hitTestAction(const QRect &rect, const Message &msg,
                      int clickX, int clickY,
                      const QString &senderName,
                      const FolderTreeState *tree = nullptr,
                      QString *outTogglePath = nullptr) const;

    // Map an absolute widget-x coordinate to a character index in msg.text.
    // contentLeft must be the x pixel where the text string starts (same
    // coordinate space as clickX). Returns a value in [0, msg.text.size()].
    static int charAtX(const Message &msg, int absoluteX, int contentLeft);

private:
    QImage thumbnail(const Message &msg);

    ThumbCache m_thumbCache;
};
