#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace yambz {

/**
 * شجرة معامل — تمثّل أي قيمة Java.
 * تُحرَّر بـ std::shared_ptr. عند الاستدعاء تُسلسل إلى JSON.
 */
struct ParamNode {
    enum class Kind : uint8_t {
        Null = 0,
        Int, Long, Short, Byte, Char, Bool, Float, Double,
        String,
        ClassRef,     // str = "java.lang.String"
        Enum,         // classType + str = "RUNNABLE"
        Array,        // classType + elements
        List,         // classType + elements
        Set,          // classType + elements
        Map,          // classType + entries
        Object,       // classType + construction + ctorArgs/fields
    };

    Kind kind = Kind::Null;

    // primitive value
    int64_t  i64 = 0;
    double   f64 = 0.0;
    bool     b   = false;
    std::string str;

    // class info
    std::string classType;

    // Object construction
    enum class How : uint8_t { Unsafe = 0, Constructor = 1, Factory = 2 };
    How how = How::Unsafe;
    std::string factoryName;
    std::vector<std::shared_ptr<ParamNode>> ctorArgs;
    std::vector<std::pair<std::string, std::shared_ptr<ParamNode>>> fields;

    // Array/List/Set
    std::vector<std::shared_ptr<ParamNode>> elements;

    // Map
    std::vector<std::pair<std::shared_ptr<ParamNode>, std::shared_ptr<ParamNode>>> entries;

    // convenience creators
    static std::shared_ptr<ParamNode> makeNull() {
        auto n = std::make_shared<ParamNode>();
        n->kind = Kind::Null;
        return n;
    }
    static std::shared_ptr<ParamNode> makeInt(int v) {
        auto n = std::make_shared<ParamNode>();
        n->kind = Kind::Int; n->i64 = v; return n;
    }
    static std::shared_ptr<ParamNode> makeString(const std::string& s) {
        auto n = std::make_shared<ParamNode>();
        n->kind = Kind::String; n->str = s; return n;
    }
    static std::shared_ptr<ParamNode> makeBool(bool v) {
        auto n = std::make_shared<ParamNode>();
        n->kind = Kind::Bool; n->b = v; return n;
    }
    static std::shared_ptr<ParamNode> makeObjectUnsafe(const std::string& cls) {
        auto n = std::make_shared<ParamNode>();
        n->kind = Kind::Object; n->classType = cls; n->how = How::Unsafe; return n;
    }

    /// Default node for a Java type name (كما يُعرض في UI)
    static std::shared_ptr<ParamNode> forType(const std::string& typeName);
};

/// Serializes to JSON string per ParameterBuilder spec
std::string nodeToJson(const std::shared_ptr<ParamNode>& n);

} // namespace yambz
