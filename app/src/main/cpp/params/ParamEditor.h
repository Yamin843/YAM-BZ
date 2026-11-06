#pragma once
#include "ParamValue.h"
#include <string>
#include <memory>

namespace yambz {

class ParamEditor {
public:
    /// Recursively draw an editor for a ParamValue.
    /// @param label        Widget label (unique per scope)
    /// @param v            Value to edit (in-place)
    /// @param expectedType Java type as string ("int", "java.lang.String", ...)
    /// @param depth        Recursion depth (for limit)
    /// @return true if value changed
    static bool draw(const char* label,
                     std::shared_ptr<ParamValue>& v,
                     const std::string& expectedType,
                     int depth = 0);

    /// Reset a value to default for its type
    static void reset(std::shared_ptr<ParamValue>& v, const std::string& type);

    /// Max recursion
    static constexpr int kMaxDepth = 8;
};

} // namespace yambz
