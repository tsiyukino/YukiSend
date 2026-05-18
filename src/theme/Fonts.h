#pragma once

#include <QFont>

// Call once at startup after QApplication is constructed.
// Registers Inter from embedded resources and sets it as the app default.
namespace Fonts {

void load();

// Font factories — use these everywhere instead of constructing QFont directly.
// Sizes are in POINTS (pt), which are DPI-independent.
QFont regular(int pt);
QFont medium(int pt);
QFont semiBold(int pt);
QFont bold(int pt);

} // namespace Fonts
