#pragma once
#include <imgui.h>

namespace yambz {

/**
 * نظام الألوان والأنماط.
 * نمط داكن احترافي مستوحى من أدوات عصرية (VS Code Dark, Discord, JetBrains).
 * ألوان ذهبية دافئة للتمييز، تباين عالٍ، استدارة معتدلة.
 */
class Theme {
public:
    /// النمط الافتراضي (Dark + Gold).
    static void apply();

    /// النمط الفاتح (للنهار).
    static void applyLight();

    /// حجم الخط (px). افتراضي 38.
    static void setFontSize(float px);
    static float fontSize();

    /// إعادة تحميل الخط (بعد تغيير الحجم).
    static void reloadFont();

    // ── ألوان مساعدة للاستخدام داخل الواجهة ──
    static ImU32 gold();         // 0xFFD4A017
    static ImU32 goldDim();      // 0xFFA8801A
    static ImU32 goldBright();   // 0xFFF0BE3C
    static ImU32 textPrimary();
    static ImU32 textDim();
    static ImU32 bgCard();
    static ImU32 bgDeeper();
    static ImU32 border();

    // ── مساعدات رسم ──
    static void drawSectionHeader(const char* title);
    static bool pillButton(const char* label, ImVec2 size = ImVec2(0,0));
    static void separator();

private:
    static float s_fontSize;
};

} // namespace yambz
