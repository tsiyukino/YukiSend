#pragma once

#include <QWidget>
#include <QString>

#include "Animator.h"

// Single-line search input with animated focus border.
// Emits textChanged() as the user types; the caller filters the peer list.
class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr);

    QString text() const;
    void clear();

signals:
    void textChanged(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    void updateText(const QString &text);
    QRect pillRect() const;
    QRect inputRect() const;

    QString   m_text;
    int       m_cursorPos = 0;
    Animator  m_focusAnim;
    bool      m_focused = false;
};
