#pragma once

#include <QWidget>
#include <QImage>

#include "Animator.h"

// Full-screen overlay that displays a single image on top of the main window.
// Owned by ChatView; stacked as a sibling covering the entire parent geometry.
// Dismisses on click, ESC, or external call to dismiss().
class MediaViewer : public QWidget {
    Q_OBJECT
public:
    explicit MediaViewer(QWidget *parent = nullptr);

    void showImage(const QImage &image);
    void dismiss();

signals:
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QImage   m_image;
    Animator m_fadeAnim;

    // Scaled image cached on resize / image change — avoids scaling every frame.
    QImage   m_scaled;
    QRect    m_imageRect;  // position of m_scaled within the widget

    void rebuildScaled();
};
