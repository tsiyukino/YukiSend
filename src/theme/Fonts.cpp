#include "Fonts.h"

#include <QFontDatabase>
#include <QApplication>

namespace Fonts {

static QString sFamily = QStringLiteral("Inter");

void load() {
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-Medium.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-SemiBold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-Bold.ttf"));

    // Set Inter as the application-wide default font (10pt ≈ 13px at 96dpi).
    QFont appFont(sFamily);
    appFont.setPointSize(10);
    QApplication::setFont(appFont);
}

QFont regular(int pt) {
    QFont f(sFamily);
    f.setPointSize(pt);
    f.setWeight(QFont::Normal);
    return f;
}

QFont medium(int pt) {
    QFont f(sFamily);
    f.setPointSize(pt);
    f.setWeight(QFont::Medium);
    return f;
}

QFont semiBold(int pt) {
    QFont f(sFamily);
    f.setPointSize(pt);
    f.setWeight(QFont::DemiBold);
    return f;
}

QFont bold(int pt) {
    QFont f(sFamily);
    f.setPointSize(pt);
    f.setWeight(QFont::Bold);
    return f;
}

} // namespace Fonts
