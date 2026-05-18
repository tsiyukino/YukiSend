#pragma once

#include <QImage>
#include <QByteArray>
#include <QSize>

// Pure image-processing utilities. No I/O, no Qt widget dependencies.
namespace ImageUtils {

// Downscale src to fit within maxSize, keeping aspect ratio.
// Returns src unchanged if it already fits.
QImage makeThumbnail(const QImage &src, QSize maxSize);

// Box blur. radius = 1 is a 3×3 kernel; radius = 4 gives a visibly soft result.
// Works on ARGB32 or RGB32. Returns a blurred copy.
QImage boxBlur(const QImage &src, int radius);

// Encode to JPEG. quality 0–100; 40 gives good blur-placeholder compression.
QByteArray encodeJpeg(const QImage &img, int quality = 40);

// Decode from JPEG bytes. Returns a null QImage on failure.
QImage decodeJpeg(const QByteArray &data);

} // namespace ImageUtils
