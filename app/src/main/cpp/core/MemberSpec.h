#pragma once
#include <string>

namespace yambz {
/// Build "className#methodName(argType1,argType2)" from member string
/// member format: "M|mods|name|argType1,argType2|retType" or "F|mods|name|type"
inline std::string buildMemberSpec(const std::string& className, const std::string& memberLine) {
    if (memberLine.empty()) return className;
    char kind = memberLine[0];
    // Skip past two '|' to reach name
    size_t p1 = memberLine.find('|');
    if (p1 == std::string::npos) return className;
    size_t p2 = memberLine.find('|', p1 + 1);
    if (p2 == std::string::npos) return className;
    size_t p3 = memberLine.find('|', p2 + 1);
    std::string name = memberLine.substr(p2 + 1, p3 - p2 - 1);

    if (kind == 'M') {
        size_t p4 = memberLine.find('|', p3 + 1);
        std::string args = (p4 != std::string::npos)
            ? memberLine.substr(p3 + 1, p4 - p3 - 1)
            : memberLine.substr(p3 + 1);
        return className + "#" + name + "(" + args + ")";
    } else {
        // Field — ليس له method spec، لكن نعيد name فقط
        return className + "#" + name;
    }
}
} // namespace yambz
