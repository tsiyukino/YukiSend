#include "MediaViewer.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>

static constexpr int    kFadeDuration = 180;
static constexpr double kBackdropAlpha = 0.82;

MediaViewer::MediaViewer(QWidget *parent)
    : QWidget(parent)
    , m_fadeAnim(this)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);
    hide();
}

void MediaViewer::showImage(const QImage &image) {
    m_image = image;
    rebuildScaled();
    show();
    raise();
    setFocus();
    m_fadeAnim.start([this] { update(); }, 0.0, 1.0, kFadeDuration);
}

void MediaViewer::dismiss() {
    m_fadeAnim.start([this] {
        update();
        if (m_fadeAnim.value(0.0) <= 0.001)
            hide();
    }, 1.0, 0.0, kFadeDuration);
    emit dismissed();
}

void MediaViewer::rebuildScaled() {
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        m_scaled    = QImage();
        m_imageRect = QRect();
        return;
    }

    // Fit image within the viewer with 48px margin on each side.
    const int margin  = 48;
    const int maxW    = width()  - margin * 2;
    const int maxH    = height() - margin * 2;
    const QSize fitted = m_image.size().scaled(maxW, maxH, Qt::KeepAspectRatio);
    m_scaled = m_image.scaled(fitted, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const int x = (width()  - fitted.width())  / 2;
    const int y = (height() - fitted.height()) / 2;
    m_imageRect = QRect(x, y, fitted.width(), fitted.height());
}

void MediaViewer::paintEvent(QPaintEvent *) {
    const double alpha = m_fadeAnim.value(isVisible() ? 1.0 : 0.0);

    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Backdrop
    QColor backdrop(0, 0, 0, int(255 * kBackdropAlpha * alpha));
    p.fillRect(rect(), backdrop);

    if (m_image.isNull() || m_imageRect.isEmpty()) return;

    // Image with fade-in opacity
    p.setOpacity(alpha);
    p.drawImage(m_imageRect, m_scaled);
    p.setOpacity(1.0);

    // Subtle rounded border around image
    p.setPen(QPen(QColor(255, 255, 255, int(40 * alpha)), 1));
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawRoundedRect(m_imageRect, 4, 4);
}

void MediaViewer::mousePressEvent(QMouseEvent *) {
    dismiss();
}

void MediaViewer::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape)
        dismiss();
}

void MediaViewer::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    rebuildScaled();
}
