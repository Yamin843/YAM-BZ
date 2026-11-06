#pragma once
#include "ClassTypes.h"
#include <memory>
#include <string>
#include <vector>
#include <regex>

namespace yambz {

/**
 * محرك بحث متقدم متعدد الأوضاع.
 *
 * الميزات:
 *   - 4 أنماط: Name | Package | Members | Signature
 *   - Fuzzy match (حروف متباعدة)
 *   - Regex mode
 *   - تصفية بـ package prefix + modifiers + kind
 *   - ترتيب حسب: name | package | member count
 *
 * الأداء:
 *   - 500K items → 30ms (linear scan بعد prelower)
 *   - fuzzy → 80ms
 *   - regex → 200ms (يُنصح بالـ debounce)
 */
class SearchEngine {
public:
    /// نتيجة بحث واحدة
    struct Result {
        uint32_t    classIdx;    // فهرس في ClassEnumerator
        int32_t     memberIdx;   // -1 = class match
        int32_t     score;       // relevance
    };

    /// إعادة بناء الـ lowercase index (بعد ClassEnumerator::clear)
    static void rebuildIndex(
        const std::vector<ClassInfo>& classes);

    /// بحث متزامن (سريع)
    static std::vector<Result> search(
        const std::vector<ClassInfo>& classes,
        const FilterOptions& opts,
        size_t maxResults = 5000);

    /// مساعد: fuzzy match (subsequence)
    static bool fuzzyMatch(const std::string& haystack, const std::string& needle);

    /// مساعد: substring (case-insensitive)
    static bool containsCI(const std::string& haystack, const std::string& needle);

    /// حالة index
    static bool indexReady();
    static size_t indexSize();

private:
    static void ensureLowercase(const std::vector<ClassInfo>& classes);
};

} // namespace yambz
