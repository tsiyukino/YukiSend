#include "ImageUtils.h"

#include <QImageReader>
#include <QImageWriter>
#include <QBuffer>

namespace ImageUtils {

QImage makeThumbnail(const QImage &src, QSize maxSize) {
    if (src.isNull()) return {};
    if (src.width() <= maxSize.width() && src.height() <= maxSize.height())
        return src;
    return src.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage boxBlur(const QImage &src, int radius) {
    if (src.isNull() || radius <= 0) return src;

    const QImage input = src.convertToFormat(QImage::Format_ARGB32);
    const int w = input.width();
    const int h = input.height();
    const int r = qMin(radius, qMin(w, h) / 2);
    const int span = 2 * r + 1;

    // Horizontal pass
    QImage horiz(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb *src_row = reinterpret_cast<const QRgb *>(input.constScanLine(y));
        QRgb       *dst_row = reinterpret_cast<QRgb *>(horiz.scanLine(y));

        // Seed the running sums from the leftmost span
        int ra = 0, rr = 0, rg = 0, rb = 0;
        for (int x = -r; x <= r; ++x) {
            const QRgb px = src_row[qBound(0, x, w - 1)];
            ra += qAlpha(px); rr += qRed(px);
            rg += qGreen(px); rb += qBlue(px);
        }
        dst_row[0] = qRgba(rr / span, rg / span, rb / span, ra / span);

        for (int x = 1; x < w; ++x) {
            const QRgb add = src_row[qBound(0, x + r,     w - 1)];
            const QRgb sub = src_row[qBound(0, x - r - 1, w - 1)];
            ra += qAlpha(add) - qAlpha(sub);
            rr += qRed(add)   - qRed(sub);
            rg += qGreen(add) - qGreen(sub);
            rb += qBlue(add)  - qBlue(sub);
            dst_row[x] = qRgba(rr / span, rg / span, rb / span, ra / span);
        }
    }

    // Vertical pass
    QImage result(w, h, QImage::Format_ARGB32);
    for (int x = 0; x < w; ++x) {
        int ra = 0, rr = 0, rg = 0, rb = 0;
        for (int y = -r; y <= r; ++y) {
            const QRgb px = reinterpret_cast<const QRgb *>(
                horiz.constScanLine(qBound(0, y, h - 1)))[x];
            ra += qAlpha(px); rr += qRed(px);
            rg += qGreen(px); rb += qBlue(px);
        }
        reinterpret_cast<QRgb *>(result.scanLine(0))[x] =
            qRgba(rr / span, rg / span, rb / span, ra / span);

        for (int y = 1; y < h; ++y) {
            const QRgb add = reinterpret_cast<const QRgb *>(
                horiz.constScanLine(qBound(0, y + r,     h - 1)))[x];
            const QRgb sub = reinterpret_cast<const QRgb *>(
                horiz.constScanLine(qBound(0, y - r - 1, h - 1)))[x];
            ra += qAlpha(add) - qAlpha(sub);
            rr += qRed(add)   - qRed(sub);
            rg += qGreen(add) - qGreen(sub);
            rb += qBlue(add)  - qBlue(sub);
            reinterpret_cast<QRgb *>(result.scanLine(y))[x] =
                qRgba(rr / span, rg / span, rb / span, ra / span);
        }
    }

    return result;
}

QByteArray encodeJpeg(const QImage &img, int quality) {
    if (img.isNull()) return {};
    QByteArray buf;
    QBuffer    dev(&buf);
    dev.open(QIODevice::WriteOnly);
    img.save(&dev, "JPEG", quality);
    return buf;
}

QImage decodeJpeg(const QByteArray &data) {
    if (data.isEmpty()) return {};
    QImage img;
    img.loadFromData(data, "JPEG");
    return img;
}

} // namespace ImageUtils
