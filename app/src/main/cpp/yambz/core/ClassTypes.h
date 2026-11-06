#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace yambz {

/// نوع عضو داخل صنف
enum class MemberKind : uint8_t {
    Constructor = 0,
    Method      = 1,
    Field       = 2,
};

/// بساطة التمثيل — نتجنب std::vector<std::string> لتوفير الذاكرة
struct ClassInfo {
    std::string fullName;       // com.example.Foo$Bar
    std::string simpleName;     // Bar
    std::string package;        // com.example
    uint32_t    memberCount;    // -1 = غير معروف بعد
    int32_t     classLoaderIdx; // -1 = boot
    uint8_t     flags;          // interface|abstract|enum|final
};

struct MemberInfo {
    MemberKind  kind;
    uint32_t    modifiers;
    std::string name;
    std::string signature;      // (Ljava/lang/String;I)V أو L...;
    std::string returnType;     // فقط للدوال
};

struct FilterOptions {
    // بحث
    std::string query;          // نص البحث (lowercase)
    bool caseSensitive = false;
    bool useRegex = false;

    // تصفية
    std::string packagePrefix;  // "" = كل الحزم
    uint8_t kindMask = 0x07;    // bitmask للـ MemberKind

    // ترتيب
    enum class SortBy : uint8_t { Name = 0, Package = 1, Members = 2, FullName = 3 };
    SortBy sortBy = SortBy::Name;
    bool   sortDescending = false;
};

} // namespace yambz
