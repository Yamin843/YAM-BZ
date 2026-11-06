#pragma once
#include <string>
#include <vector>
#include <memory>

namespace yambz {

/// Recursive parameter value. Mirrors the JSON structure
/// expected by CallBridge.buildFromJsonTree().
struct ParamValue {
    enum Type {
        NULL_V = 0,
        INT, LONG, SHORT, BYTE, CHAR, BOOL, FLOAT, DOUBLE,
        STRING,
        OBJECT,
        ARRAY,
        LIST,
        MAP,
        ENUM,
    };

    Type type = NULL_V;

    // primitives
    long i64 = 0;
    double f64 = 0.0;
    bool b = false;
    std::string str;

    // object
    std::string className;
    std::vector<std::pair<std::string, std::shared_ptr<ParamValue>>> fields;

    // array / list
    std::string elementType;
    std::vector<std::shared_ptr<ParamValue>> elements;

    // map
    std::string keyType, valueType;
    std::vector<std::pair<std::shared_ptr<ParamValue>, std::shared_ptr<ParamValue>>> entries;

    // enum
    std::string enumClass;
    std::string enumValue;

    // ─── factory ───
    static std::shared_ptr<ParamValue> makeNull();
    static std::shared_ptr<ParamValue> forType(const std::string& javaTypeName);

    /// Serialize to JSON accepted by CallBridge
    std::string toJson() const;
};

} // namespace yambz
