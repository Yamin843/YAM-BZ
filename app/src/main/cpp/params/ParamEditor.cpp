#include "ParamEditor.h"
#include "ui/Theme.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "bridge/ClassBridgeQuery.h"

#include <imgui.h>
#include <cstring>
#include <cstdio>

namespace yambz {

void ParamEditor::reset(std::shared_ptr<ParamValue>& v, const std::string& type) {
    v = ParamValue::forType(type);
}

// ═══════════════════════════════════════════════════════════
//  Primitive editors
// ═══════════════════════════════════════════════════════════
static bool drawPrimitive(ParamValue& v) {
    bool changed = false;
    switch (v.type) {
        case ParamValue::INT:
        case ParamValue::LONG:
        case ParamValue::SHORT:
        case ParamValue::BYTE: {
            int tmp = (int) v.i64;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##prim", &tmp)) {
                v.i64 = tmp;
                changed = true;
            }
            break;
        }
        case ParamValue::CHAR: {
            int tmp = (int) v.i64;
            char buf[8];
            if (tmp > 0 && tmp < 128) snprintf(buf, sizeof buf, "%c", (char)tmp);
            else buf[0] = 0;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##char", buf, sizeof buf, ImGuiInputTextFlags_CharsNoBlank)) {
                v.i64 = buf[0];
                changed = true;
            }
            break;
        }
        case ParamValue::BOOL: {
            bool bv = v.b;
            if (ImGui::Checkbox("##bool", &bv)) {
                v.b = bv;
                changed = true;
            }
            break;
        }
        case ParamValue::FLOAT:
        case ParamValue::DOUBLE: {
            float fv = (float) v.f64;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##f", &fv)) {
                v.f64 = fv;
                changed = true;
            }
            break;
        }
        default: break;
    }
    return changed;
}

// ═══════════════════════════════════════════════════════════
//  Main draw
// ═══════════════════════════════════════════════════════════
bool ParamEditor::draw(const char* label,
                       std::shared_ptr<ParamValue>& v,
                       const std::string& expectedType,
                       int depth) {
    if (!v) v = ParamValue::forType(expectedType);
    if (depth > kMaxDepth) {
        ImGui::TextDisabled("(max depth)");
        return false;
    }

    bool changed = false;
    ImGui::PushID((intptr_t) v.get());

    // Header: label + type + null toggle
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", expectedType.c_str());
    ImGui::SameLine();
    bool isNull = (v->type == ParamValue::NULL_V);
    if (ImGui::Checkbox("null", &isNull)) {
        if (isNull) v = ParamValue::makeNull();
        else v = ParamValue::forType(expectedType);
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle null");

    if (v->type == ParamValue::NULL_V) {
        ImGui::PopID();
        return changed;
    }

    ImGui::Indent(20);

    // Body
    switch (v->type) {
        case ParamValue::INT:
        case ParamValue::LONG:
        case ParamValue::SHORT:
        case ParamValue::BYTE:
        case ParamValue::CHAR:
        case ParamValue::BOOL:
        case ParamValue::FLOAT:
        case ParamValue::DOUBLE: {
            changed |= drawPrimitive(*v);
            break;
        }

        case ParamValue::STRING: {
            char buf[2048];
            strncpy(buf, v->str.c_str(), sizeof buf - 1);
            buf[sizeof buf - 1] = 0;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextMultiline("##str", buf, sizeof buf, ImVec2(-1, 60))) {
                v->str = buf;
                changed = true;
            }
            break;
        }

        case ParamValue::OBJECT: {
            // Ensure fields populated from schema
            if (v->fields.empty() && !v->className.empty()) {
                auto schema = ClassBridgeQuery::getFieldSchema(v->className);
                for (auto& [name, ftype] : schema) {
                    v->fields.emplace_back(name, ParamValue::forType(ftype));
                }
            }

            if (v->fields.empty()) {
                ImGui::TextDisabled("(no fields — will use Unsafe allocation)");
            } else {
                for (auto& [fname, fval] : v->fields) {
                    ImGui::PushID(fname.c_str());
                    char sublabel[128];
                    snprintf(sublabel, sizeof sublabel, "%s", fname.c_str());
                    // Get type from schema (store per-field? or re-fetch)
                    std::string ftype = "java.lang.Object";
                    auto schema = ClassBridgeQuery::getFieldSchema(v->className);
                    for (auto& [n, t] : schema) {
                        if (n == fname) { ftype = t; break; }
                    }
                    changed |= ParamEditor::draw(sublabel, fval, ftype, depth + 1);
                    ImGui::PopID();
                }
            }
            break;
        }

        case ParamValue::ARRAY:
        case ParamValue::LIST: {
            ImGui::Text("Element type: %s",
                v->elementType.empty() ? "?" : v->elementType.c_str());

            for (size_t i = 0; i < v->elements.size(); i++) {
                ImGui::PushID((int)i);
                char lbl[32];
                snprintf(lbl, sizeof lbl, "[%zu]", i);
                changed |= ParamEditor::draw(lbl, v->elements[i], v->elementType, depth + 1);
                ImGui::SameLine();
                if (ImGui::SmallButton("×")) {
                    v->elements.erase(v->elements.begin() + i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            if (ImGui::Button("+ Add element")) {
                v->elements.push_back(ParamValue::forType(v->elementType));
                changed = true;
            }
            break;
        }

        case ParamValue::MAP: {
            ImGui::Text("Key: %s | Value: %s", v->keyType.c_str(), v->valueType.c_str());
            for (size_t i = 0; i < v->entries.size(); i++) {
                ImGui::PushID((int)i);
                ImGui::Text("[%zu] key:", i);
                changed |= ParamEditor::draw("key", v->entries[i].first, v->keyType, depth + 1);
                ImGui::Text("    value:");
                changed |= ParamEditor::draw("val", v->entries[i].second, v->valueType, depth + 1);
                if (ImGui::SmallButton("remove")) {
                    v->entries.erase(v->entries.begin() + i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::Button("+ Add entry")) {
                v->entries.emplace_back(
                    ParamValue::forType(v->keyType),
                    ParamValue::forType(v->valueType));
                changed = true;
            }
            break;
        }

        case ParamValue::ENUM: {
            char buf[256];
            strncpy(buf, v->enumValue.c_str(), sizeof buf - 1);
            buf[sizeof buf - 1] = 0;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("Value", buf, sizeof buf)) {
                v->enumValue = buf;
                changed = true;
            }
            break;
        }

        default: break;
    }

    ImGui::Unindent(20);
    ImGui::PopID();
    return changed;
}

} // namespace yambz
