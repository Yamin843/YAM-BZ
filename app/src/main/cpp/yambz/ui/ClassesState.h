#pragma once
#include "../core/ClassTypes.h"
#include "../core/SearchEngine.h"
#include <vector>
#include <string>
#include <memory>

namespace yambz {

/// حالة مشتركة بين UI و Background (thread-safe بـ singleton)
struct ClassesState {
    // بحث
    FilterOptions filter;

    // نتائج البحث (مؤخرة من background)
    std::shared_ptr<const std::vector<SearchEngine::Result>> results;

    // مؤشر البحث
    bool searching = false;

    static ClassesState& instance();
};

} // namespace yambz
