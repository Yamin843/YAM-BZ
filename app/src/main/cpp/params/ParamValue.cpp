#include "ParamValue.h"
#include <sstream>
#include <cstdio>

namespace yambz {

static void jsonEscape(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", (unsigned)c);
                    out += buf;
                } else out += c;
        }
    }
    out += '"';
}

std::shared_ptr<ParamValue> ParamValue::makeNull() {
    auto n = std::make_shared<ParamValue>();
    n->type = NULL_V;
    return n;
}

std::shared_ptr<ParamValue> ParamValue::forType(const std::string& t) {
    auto n = std::make_shared<ParamValue>();
    if (t == "int")       { n->type = INT; }
    else if (t == "long") { n->type = LONG; }
    else if (t == "short"){ n->type = SHORT; }
    else if (t == "byte") { n->type = BYTE; }
    else if (t == "char") { n->type = CHAR; }
    else if (t == "boolean" || t == "bool") { n->type = BOOL; }
    else if (t == "float"){ n->type = FLOAT; }
    else if (t == "double") { n->type = DOUBLE; }
    else if (t == "java.lang.String" || t == "String") { n->type = STRING; }
    else {
        // assume object
        n->type = OBJECT;
        n->className = t;
    }
    return n;
}

static void serialize(std::string& out, const std::shared_ptr<ParamValue>& v) {
    if (!v) { out += "{\"k\":\"null\"}"; return; }
    out += '{';
    switch (v->type) {
        case ParamValue::NULL_V: out += "\"k\":\"null\""; break;
        case ParamValue::INT:    out += "\"k\":\"int\",\"v\":" + std::to_string(v->i64); break;
        case ParamValue::LONG:   out += "\"k\":\"long\",\"v\":" + std::to_string(v->i64); break;
        case ParamValue::SHORT:  out += "\"k\":\"short\",\"v\":" + std::to_string(v->i64); break;
        case ParamValue::BYTE:   out += "\"k\":\"byte\",\"v\":" + std::to_string(v->i64); break;
        case ParamValue::CHAR:   out += "\"k\":\"char\",\"v\":" + std::to_string((int)v->i64); break;
        case ParamValue::BOOL:   out += "\"k\":\"bool\",\"v\":";
                                 out += (v->b ? "true" : "false"); break;
        case ParamValue::FLOAT:  out += "\"k\":\"float\",\"v\":";
                                 { char buf[32]; snprintf(buf, sizeof buf, "%.9g", (float)v->f64); out += buf; }
                                 break;
        case ParamValue::DOUBLE: out += "\"k\":\"double\",\"v\":";
                                 { char buf[32]; snprintf(buf, sizeof buf, "%.17g", v->f64); out += buf; }
                                 break;
        case ParamValue::STRING: out += "\"k\":\"str\",\"v\":"; jsonEscape(out, v->str); break;
        case ParamValue::ENUM:
            out += "\"k\":\"enum\",\"cls\":";
            jsonEscape(out, v->enumClass);
            out += ",\"v\":";
            jsonEscape(out, v->enumValue);
            break;
        case ParamValue::OBJECT: {
            out += "\"k\":\"obj\",\"cls\":";
            jsonEscape(out, v->className);
            out += ",\"fields\":{";
            bool first = true;
            for (auto& [name, fv] : v->fields) {
                if (!first) out += ',';
                first = false;
                jsonEscape(out, name);
                out += ':';
                serialize(out, fv);
            }
            out += '}';
            break;
        }
        case ParamValue::ARRAY:
        case ParamValue::LIST: {
            out += v->type == ParamValue::ARRAY
                ? "\"k\":\"array\"" : "\"k\":\"list\"";
            out += ",\"elemType\":";
            jsonEscape(out, v->elementType);
            out += ",\"items\":[";
            for (size_t i = 0; i < v->elements.size(); i++) {
                if (i) out += ',';
                serialize(out, v->elements[i]);
            }
            out += ']';
            break;
        }
        case ParamValue::MAP: {
            out += "\"k\":\"map\",\"keyType\":";
            jsonEscape(out, v->keyType);
            out += ",\"valueType\":";
            jsonEscape(out, v->valueType);
            out += ",\"entries\":[";
            for (size_t i = 0; i < v->entries.size(); i++) {
                if (i) out += ',';
                out += '[';
                serialize(out, v->entries[i].first);
                out += ',';
                serialize(out, v->entries[i].second);
                out += ']';
            }
            out += ']';
            break;
        }
    }
    out += '}';
}

std::string ParamValue::toJson() const {
    std::string out;
    out.reserve(256);
    auto self = std::make_shared<ParamValue>(*this);
    serialize(out, self);
    return out;
}

} // namespace yambz
