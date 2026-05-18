#pragma once

#include <QWidget>
#include <QPixmap>

#include "Animator.h"

// Leftmost icon column. Four icons: Send(0), Cooperate(1), Git(2), Settings(3).
// Cooperate/Git/Settings are placeholders — they emit pageChanged but have no view yet.
class NavBar : public QWidget {
    Q_OBJECT
public:
    explicit NavBar(QWidget *parent = nullptr);

    void setCurrentPage(int page);
    int  currentPage() const;

signals:
    void pageChanged(int page);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect iconRect(int index) const;
    void  drawIcon(QPainter &p, const QRect &r, int index,
                   double activeProgress, double hoverProgress) const;

    int  m_currentPage = 0;
    int  m_hoverIndex  = -1;

    // Per-icon animators: [0]=Send, [1]=Cooperate, [2]=Git, [3]=Settings
    Animator m_hoverAnim[4];

    // Cached tinted pixmaps — rebuilt only when tint colour changes.
    // [i][0] = secondary-grey tint, [i][1] = near-black tint (active)
    mutable QPixmap m_iconCache[4][2];
    mutable bool    m_cacheValid = false;

    void buildIconCache() const;
};
