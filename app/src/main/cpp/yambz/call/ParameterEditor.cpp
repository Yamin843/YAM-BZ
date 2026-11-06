#include "ParameterEditor.h"
#include "ui/Theme.h"
#include "../core/Logger.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include "core/Helpers.h"

namespace yambz {

void ParameterEditor::reset(std::shared_ptr<ParamNode>& node,
                             const std::string& expectedType) {
    node = ParamNode::forType(expectedType);
}

static bool drawPrimitive(const char* label, std::shared_ptr<ParamNode>& node) {
    bool changed = false;
    switch (node->kind) {
        case ParamNode::Kind::Int:
        case ParamNode::Kind::Short:
        case ParamNode::Kind::Byte:
        case ParamNode::Kind::Long: {
            int v = (int) node->i64;
            ImGui::SetNextItemWidth(180);
            if (ImGui::DragInt(label, &v)) { node->i64 = v; changed = true; }
            break;
        }
        case ParamNode::Kind::Char: {
            int v = (int) node->i64;
            ImGui::SetNextItemWidth(180);
            char buf[8];
            if (v > 0 && v < 128) snprintf(buf, sizeof buf, "%c", (char)v);
            else buf[0] = '\0';
            if (ImGui::InputText(label, buf, sizeof buf, ImGuiInputTextFlags_CharsNoBlank)) {
                node->i64 = buf[0]; changed = true;
            }
            break;
        }
        case ParamNode::Kind::Bool: {
            bool v = node->b;
            if (ImGui::Checkbox(label, &v)) { node->b = v; changed = true; }
            break;
        }
        case ParamNode::Kind::Float:
        case ParamNode::Kind::Double: {
            float v = (float) node->f64;
            ImGui::SetNextItemWidth(180);
            if (ImGui::DragFloat(label, &v)) { node->f64 = v; changed = true; }
            break;
        }
        case ParamNode::Kind::Null: {
            ImGui::TextDisabled("%s = null", label);
            if (ImGui::Button(("set " + std::string(label)).c_str())) {
                // caller يقرر — لن نستخدم هذا
            }
            break;
        }
        default: break;
    }
    return changed;
}

bool ParameterEditor::draw(const char* label, std::shared_ptr<ParamNode>& node,
                           const std::string& expectedType, int depth) {
    if (depth > 32) { ImGui::TextDisabled("(max depth)"); return false; }
    if (!node) node = ParamNode::forType(expectedType);

    bool changed = false;
    ImGui::PushID((int)(intptr_t) node.get());

    // ─── header: label + type + null toggle ───
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", expectedType.c_str());
    ImGui::SameLine();
    bool isNull = node->kind == ParamNode::Kind::Null;
    if (ImGui::Checkbox("null", &isNull)) {
        if (isNull) node = ParamNode::makeNull();
        else node = ParamNode::forType(expectedType);
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("set to null (default)");

    if (node->kind == ParamNode::Kind::Null) {
        ImGui::PopID();
        return changed;
    }

    ImGui::Indent(20);

    // ─── kind-specific ───
    switch (node->kind) {
        case ParamNode::Kind::Int:
        case ParamNode::Kind::Long:
        case ParamNode::Kind::Short:
        case ParamNode::Kind::Byte:
        case ParamNode::Kind::Char:
        case ParamNode::Kind::Bool:
        case ParamNode::Kind::Float:
        case ParamNode::Kind::Double: {
            char il[128]; snprintf(il, sizeof il, "##p_%s", label);
            changed |= drawPrimitive(il, node);
            break;
        }
        case ParamNode::Kind::String: {
            char buf[2048];
            strncpy(buf, node->str.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf)-1] = '\0';
            char il[128]; snprintf(il, sizeof il, "##s_%s", label);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText(il, buf, sizeof buf)) {
                node->str = buf;
                changed = true;
            }
            break;
        }
        case ParamNode::Kind::Object: {
            // construction choice
            const char* hows[] = {"unsafe", "constructor", "factory"};
            int cur = (int) node->how;
            ImGui::SetNextItemWidth(180);
            if (ImGui::Combo("##how", &cur, hows, 3)) {
                node->how = (ParamNode::How) cur;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("construction");
            ImGui::SameLine();
            if (ImGui::SmallButton("Find on heap")) {

                yambz_openHeapPicker(node->classType.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Proxy") && node->classType.find("interface") != std::string::npos) {
                // سيُعالج عبر "unsafe" للinterfaces (ParameterBuilder يدعم ذلك)
            }

            // fields (recursive)
            if (!node->fields.empty()) {
                ImGui::Text("Fields:");
                for (auto& [fname, fnode] : node->fields) {
                    ImGui::PushID(fname.c_str());
                    changed |= draw(fname.c_str(), fnode, fnode->classType, depth + 1);
                    ImGui::PopID();
                }
            } else {
                ImGui::TextDisabled("(no fields set — unsafe allocates empty)");
            }
            break;
        }
        case ParamNode::Kind::Array:
        case ParamNode::Kind::List:
        case ParamNode::Kind::Set: {
            ImGui::Text("Elements (%zu):", node->elements.size());
            for (size_t i = 0; i < node->elements.size(); i++) {
                ImGui::PushID((int)i);
                char lbl[32]; snprintf(lbl, sizeof lbl, "[%zu]", i);
                changed |= draw(lbl, node->elements[i], expectedType, depth + 1);
                ImGui::SameLine();
                if (ImGui::SmallButton("×")) {
                    node->elements.erase(node->elements.begin() + i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::Button("+ add element")) {
                node->elements.push_back(ParamNode::makeNull());
                changed = true;
            }
            ImGui::SameLine();
            static int s_bulkCount = 10;
            ImGui::SetNextItemWidth(80);
            ImGui::DragInt("##bulk", &s_bulkCount, 1, 1, 10000, "n=%d");
            ImGui::SameLine();
            if (ImGui::Button("Add n")) {
                for (int k = 0; k < s_bulkCount; k++)
                    node->elements.push_back(ParamNode::makeNull());
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear all")) {
                node->elements.clear();
                changed = true;
            }
            // quick templates
            if (node->kind == ParamNode::Kind::List) {
                ImGui::SameLine();
                if (ImGui::Button("String[]")) {
                    for (int k = 0; k < 3; k++) {
                        auto s = std::make_shared<ParamNode>();
                        s->kind = ParamNode::Kind::String;
                        s->str = "item" + std::to_string(k);
                        node->elements.push_back(s);
                    }
                    changed = true;
                }
            }
            break;
        }
        case ParamNode::Kind::Map: {
            ImGui::Text("Entries (%zu):", node->entries.size());
            for (size_t i = 0; i < node->entries.size(); i++) {
                ImGui::PushID((int)i);
                ImGui::Text("[%zu] key:", i);
                changed |= draw("key", node->entries[i].first, "", depth + 1);
                ImGui::Text("    value:");
                changed |= draw("value", node->entries[i].second, "", depth + 1);
                if (ImGui::SmallButton("remove")) {
                    node->entries.erase(node->entries.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::Button("+ add entry")) {
                node->entries.emplace_back(ParamNode::makeNull(), ParamNode::makeNull());
                changed = true;
            }
            ImGui::SameLine();
            static int s_mapBulk = 10;
            ImGui::SetNextItemWidth(80);
            ImGui::DragInt("##bulk", &s_mapBulk, 1, 1, 10000, "n=%d");
            ImGui::SameLine();
            if (ImGui::Button("Add n")) {
                for (int k = 0; k < s_mapBulk; k++)
                    node->entries.emplace_back(ParamNode::makeNull(), ParamNode::makeNull());
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear all")) {
                node->entries.clear();
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
