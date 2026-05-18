#pragma once

#include <QWidget>
#include <QString>
#include <QImage>

#include "Animator.h"

// Bottom input bar: text field on the left, File and Folder buttons on the right.
// All painting custom via QPainter — no QLineEdit, no QPushButton.
class ChatInputBar : public QWidget {
    Q_OBJECT
public:
    explicit ChatInputBar(QWidget *parent = nullptr);

    QString text() const;
    void    clear();

signals:
    void messageSent(const QString &text);
    void fileSendRequested();
    void folderSendRequested();
    void imageSendRequested(const QImage &image);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void updateText(const QString &t);
    // Move cursor; if resetSel, collapse selection to cursor position.
    void moveCursor(int pos, bool resetSel = true);
    // Map pixel x in the input area to a character index.
    int  charAtX(int x) const;
    bool hasSelection() const { return m_selStart != m_cursorPos; }
    int  selFrom() const { return qMin(m_selStart, m_cursorPos); }
    int  selTo()   const { return qMax(m_selStart, m_cursorPos); }
    QString selectedText() const { return m_text.mid(selFrom(), selTo() - selFrom()); }
    void deleteSelection();
    bool m_dragging = false;
    QRect fileButtonRect()   const;
    QRect folderButtonRect() const;
    QRect inputRect()        const;

    QString  m_text;
    QString  m_preedit;          // IME composition string (not yet committed)
    int      m_cursorPos   = 0;
    int      m_selStart    = 0;  // selection anchor; selection = [min, max) of selStart/cursorPos
    bool     m_focused     = false;
    bool     m_fileHover   = false;
    bool     m_folderHover = false;

    // Image compose mode — non-null when user pasted an image from clipboard.
    QImage   m_pendingImage;
    bool     m_cancelHover = false;

    Animator m_focusAnim;
    Animator m_fileAnim;
    Animator m_folderAnim;

    void enterImageCompose(const QImage &image);
    void cancelImageCompose();
    QRect cancelButtonRect() const;
    QRect imagePreviewRect() const;
};
