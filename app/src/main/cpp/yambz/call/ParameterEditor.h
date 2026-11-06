#pragma once
#include "ParamNode.h"
#include <string>

namespace yambz {

/**
 * مُحرّر recursive لشجرة ParamNode.
 *
 * يعرض حسب النوع:
 *   - primitive → DragInt/Float + Reset
 *   - String → InputText
 *   - Object → Combo(how) + nested fields tree
 *   - Array/List/Map → list editor with add/remove
 *   - Null toggle في كل node
 *
 * يُرجع true إذا تغيّر شيء.
 */
class ParameterEditor {
public:
    /// label يُطبع قبل المحرر. node قابل للتعديل in-place.
    static bool draw(const char* label, std::shared_ptr<ParamNode>& node,
                     const std::string& expectedType, int depth = 0);

    /// إعادة تعيين node إلى default وفق expectedType
    static void reset(std::shared_ptr<ParamNode>& node,
                      const std::string& expectedType);
};

} // namespace yambz
