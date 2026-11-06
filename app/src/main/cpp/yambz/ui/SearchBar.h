#pragma once
#include "../core/ClassTypes.h"
#include <string>

namespace yambz {

/**
 * شريط بحث بأسلوب حديث:
 *   [🔍 بحث...                          ] [C] [M] [F] [⚙]
 *
 * الميزات:
 *   - Text input مع clear button
 *   - Toggle buttons: Classes | Methods | Fields
 *   - Filter popup (package prefix, case, regex, sort)
 *   - Debounce 120ms
 */
class SearchBar {
public:
    /// رسم + إرجاع true إذا تغيّر الفلتر
    static bool draw(FilterOptions& opts);

    /// مسح البحث
    static void clear();

private:
    static char s_queryBuf[512];
    static bool s_initialized;
    static bool s_showFilters;
    static char s_pkgBuf[256];
    static double s_lastTypedAt;
};

} // namespace yambz
