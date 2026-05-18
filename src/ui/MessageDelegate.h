#pragma once

#include <QPainter>
#include <QRect>
#include <QHash>
#include <QImage>

#include "model/Message.h"
#include "theme/Theme.h"

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

    int rowHeight(const Message &msg) const;

    // hoverX: widget-relative mouse X, -1 if not hovered.
    // selection: active text selection for this message (ignored for non-text).
    // copyFlash: true for the brief "copied!" highlight after clicking Copy.
    void draw(QPainter *p, const QRect &rect, const Message &msg,
              int hoverX,
              const QString &senderName,
              const QColor &avatarColor,
              TextSelection selection = {},
              bool copyFlash = false);

    // Returns hit code: 1=Accept, 3=AcceptTo, -1=Deny, 2=RequestAgain,
    //   4=ImageOpen, 5=CopyText, 0=miss.
    // senderName must match what was passed to draw() so copy-glyph positions align.
    int hitTestAction(const QRect &rect, const Message &msg,
                      int clickX, int clickY,
                      const QString &senderName) const;

    // Map an absolute widget-x coordinate to a character index in msg.text.
    // contentLeft must be the x pixel where the text string starts (same
    // coordinate space as clickX). Returns a value in [0, msg.text.size()].
    static int charAtX(const Message &msg, int absoluteX, int contentLeft);

private:
    const QImage &thumbnail(const Message &msg);

    QHash<qint64, QImage> m_thumbnails;
};
