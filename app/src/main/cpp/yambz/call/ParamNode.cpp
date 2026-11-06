#include "ParamNode.h"
#include <sstream>
#include <cstdio>
#include <string>
#include <memory>

namespace yambz {

static void jsonEscape(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

static std::string jsonStr(const std::string& s) {
    std::string o; jsonEscape(o, s); return o;
}

std::shared_ptr<ParamNode> ParamNode::forType(const std::string& typeName) {
    auto n = std::make_shared<ParamNode>();
    // primitives
    if (typeName == "int")     { n->kind = Kind::Int; }
    else if (typeName == "long")    { n->kind = Kind::Long; }
    else if (typeName == "short")   { n->kind = Kind::Short; }
    else if (typeName == "byte")    { n->kind = Kind::Byte; }
    else if (typeName == "char")    { n->kind = Kind::Char; }
    else if (typeName == "boolean") { n->kind = Kind::Bool; }
    else if (typeName == "float")   { n->kind = Kind::Float; }
    else if (typeName == "double")  { n->kind = Kind::Double; }
    else if (typeName == "java.lang.String" || typeName == "String") {
        n->kind = Kind::String;
    }
    else {
        // assume object / interface
        n->kind = Kind::Object;
        n->classType = typeName;
        n->how = How::Unsafe;
    }
    return n;
}

static void serialize(std::string& out, const std::shared_ptr<ParamNode>& n) {
    if (!n) { out += "{\"k\":\"null\"}"; return; }
    out += '{';
    switch (n->kind) {
        case ParamNode::Kind::Null:
            out += "\"k\":\"null\"";
            break;
        case ParamNode::Kind::Int:
            out += "\"k\":\"int\",\"v\":"; out += std::to_string(n->i64);
            break;
        case ParamNode::Kind::Long:
            out += "\"k\":\"long\",\"v\":"; out += std::to_string(n->i64);
            break;
        case ParamNode::Kind::Short:
            out += "\"k\":\"short\",\"v\":"; out += std::to_string(n->i64);
            break;
        case ParamNode::Kind::Byte:
            out += "\"k\":\"byte\",\"v\":"; out += std::to_string(n->i64);
            break;
        case ParamNode::Kind::Char:
            out += "\"k\":\"char\",\"v\":"; out += std::to_string((int)n->i64);
            break;
        case ParamNode::Kind::Bool:
            out += "\"k\":\"bool\",\"v\":"; out += (n->b ? "true" : "false");
            break;
        case ParamNode::Kind::Float:
            out += "\"k\":\"float\",\"v\":"; out += std::to_string(n->f64);
            break;
        case ParamNode::Kind::Double:
            out += "\"k\":\"double\",\"v\":"; out += std::to_string(n->f64);
            break;
        case ParamNode::Kind::String:
            out += "\"k\":\"str\",\"v\":";
            out += jsonStr(n->str);
            break;
        case ParamNode::Kind::ClassRef:
            out += "\"k\":\"cls\",\"v\":";
            out += jsonStr(n->str);
            break;
        case ParamNode::Kind::Enum:
            out += "\"k\":\"enum\",\"cls\":";
            out += jsonStr(n->classType);
            out += ",\"v\":";
            out += jsonStr(n->str);
            break;
        case ParamNode::Kind::Array:
        case ParamNode::Kind::List:
        case ParamNode::Kind::Set: {
            const char* k = n->kind == ParamNode::Kind::Array ? "array"
                          : n->kind == ParamNode::Kind::List  ? "list"
                                                              : "set";
            out += "\"k\":\""; out += k; out += "\",";
            out += "\"cls\":"; out += jsonStr(n->classType);
            out += ",\"items\":[";
            for (size_t i = 0; i < n->elements.size(); i++) {
                if (i) out += ',';
                serialize(out, n->elements[i]);
            }
            out += ']';
            break;
        }
        case ParamNode::Kind::Map: {
            out += "\"k\":\"map\",";
            out += "\"cls\":"; out += jsonStr(n->classType);
            out += ",\"entries\":[";
            for (size_t i = 0; i < n->entries.size(); i++) {
                if (i) out += ',';
                out += '[';
                serialize(out, n->entries[i].first);
                out += ',';
                serialize(out, n->entries[i].second);
                out += ']';
            }
            out += ']';
            break;
        }
        case ParamNode::Kind::Object: {
            out += "\"k\":\"obj\",";
            out += "\"cls\":"; out += jsonStr(n->classType);
            out += ",\"how\":\"";
            switch (n->how) {
                case ParamNode::How::Unsafe:      out += "unsafe"; break;
                case ParamNode::How::Constructor: out += "ctor";   break;
                case ParamNode::How::Factory:     out += "factory";break;
            }
            out += '"';
            if (n->how == ParamNode::How::Constructor) {
                out += ",\"ctor\":[";
                for (size_t i = 0; i < n->ctorArgs.size(); i++) {
                    if (i) out += ',';
                    serialize(out, n->ctorArgs[i]);
                }
                out += ']';
            } else if (n->how == ParamNode::How::Factory) {
                out += ",\"factory\":";
                out += jsonStr(n->factoryName);
            }
            if (!n->fields.empty()) {
                out += ",\"fields\":{";
                bool first = true;
                for (auto& [fname, fnode] : n->fields) {
                    if (!first) out += ',';
                    first = false;
                    out += jsonStr(fname);
                    out += ':';
                    serialize(out, fnode);
                }
                out += '}';
            }
            break;
        }
    }
    out += '}';
}

std::string nodeToJson(const std::shared_ptr<ParamNode>& n) {
    std::string out;
    out.reserve(256);
    serialize(out, n);
    return out;
}

} // namespace yambz
