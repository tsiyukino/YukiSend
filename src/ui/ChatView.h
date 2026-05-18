#pragma once

#include <QWidget>
#include <QList>
#include <QTimer>
#include <QUrl>
#include <optional>

#include "model/Message.h"
#include "network/Discovery.h"
#include "MessageDelegate.h"  // also brings in TextSelection
#include "Animator.h"

// Scrollable chat message list for a single peer conversation.
// Owns a MessageDelegateRenderer (thumbnail cache) and a MediaViewer overlay.
class MediaViewer;

class ChatView : public QWidget {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);

    void setPeer(const Peer &peer);
    void setMessages(const QList<Message> &messages);
    void appendMessage(const Message &msg);
    void updateMessage(const Message &msg);

signals:
    void acceptRequested(qint64 messageId);
    void acceptToRequested(qint64 messageId);
    void denyRequested(qint64 messageId);
    void requestAgainRequested(qint64 messageId);
    void peerPanelRequested();
    void filesDropped(const QList<QUrl> &urls);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void rebuildLayout();
    int  messageAt(int y) const;
    int  contentHeight() const;
    int  listTop() const;
    void clampScroll();
    void scrollToBottom();
    void paintHeader(QPainter &p) const;
    void paintScrollButton(QPainter &p) const;
    void updateScrollButtonState();
    QRect scrollButtonRect() const;
    QRect hamburgerBtnRect() const;

    std::optional<Peer>       m_peer;
    QList<Message>            m_messages;
    QList<int>                m_rowOffsets;
    int                       m_scrollOffset = 0;
    int                       m_hoverIndex   = -1;
    int                       m_hoverX       = -1;

    MessageDelegateRenderer   m_delegate;
    MediaViewer              *m_viewer = nullptr; // child widget, lazy-created

    // Text selection state
    int            m_selMsgIdx  = -1;  // index into m_messages, -1 = none
    int            m_selAnchor  =  0;  // char index where drag started
    int            m_selFrom    =  0;  // min(anchor, current) — inclusive
    int            m_selTo      =  0;  // max(anchor, current) — exclusive
    bool           m_selecting  = false;

    bool           m_hamburgerHovered = false;

    // Copy flash feedback — briefly shows the glyph in accent color
    int            m_flashMsgIdx = -1;
    QTimer        *m_flashTimer  = nullptr;

    // Scroll-to-bottom button (fades in/out via Animator; 0.0=hidden, 1.0=visible)
    Animator       m_btnAnim;
    bool           m_btnHovered   = false;

    // Returns the pixel x where msg.text starts, for the message at msgIdx.
    int textContentLeft(int msgIdx) const;

    bool m_dropActive = false;
};
