#pragma once

#include "imgui.h"

namespace CH570App {

class UIStyle {
public:
    static void ApplyModernLightTheme(float dpiScale = 1.0f);
    static void LoadFonts(float baseFontSize = 14.5f, float dpiScale = 1.0f);

    // WinUI 3 / Fluent 2 Light Palette (Authentic Windows 11 Native Theme)
    static ImVec4 ColorAccent()       { return ImVec4(0.000f, 0.404f, 0.753f, 1.000f); } // #0067C0 WinUI 3 Primary Accent
    static ImVec4 ColorAccentHover()  { return ImVec4(0.094f, 0.514f, 0.843f, 1.000f); } // #1883D7 Accent Hover
    static ImVec4 ColorAccentActive() { return ImVec4(0.000f, 0.353f, 0.620f, 1.000f); } // #005A9E Accent Pressed

    static ImVec4 ColorCurrent()      { return ImVec4(0.059f, 0.482f, 0.059f, 1.000f); } // #0F7B0F Fluent 2 Emerald Green (Current)
    static ImVec4 ColorPower()        { return ImVec4(0.698f, 0.357f, 0.000f, 1.000f); } // #B25B00 Fluent 2 Terracotta Amber (Power)
    static ImVec4 ColorVoltage()      { return ImVec4(0.000f, 0.404f, 0.753f, 1.000f); } // #0067C0 Fluent 2 System Blue (Voltage)
    static ImVec4 ColorEnergy()       { return ImVec4(0.388f, 0.400f, 0.945f, 1.000f); } // #6366F1 Fluent 2 Indigo (Capacity & Energy)
    static ImVec4 ColorDanger()       { return ImVec4(0.769f, 0.169f, 0.110f, 1.000f); } // #C42B1C Fluent 2 Danger Red

    static ImVec4 ColorTextMain()     { return ImVec4(0.122f, 0.122f, 0.122f, 1.000f); } // #1F1F1F Fluent 2 TextPrimary (Ultra Sharp)
    static ImVec4 ColorTextMuted()    { return ImVec4(0.365f, 0.365f, 0.365f, 1.000f); } // #5D5D5D Fluent 2 TextSecondary
    static ImVec4 ColorTextDisabled() { return ImVec4(0.553f, 0.553f, 0.553f, 1.000f); } // #8D8D8D Fluent 2 TextDisabled

    static ImVec4 ColorBgCanvas()     { return ImVec4(0.953f, 0.953f, 0.953f, 1.000f); } // #F3F3F3 Fluent 2 Mica / Neutral Canvas
    static ImVec4 ColorBgCard()       { return ImVec4(1.000f, 1.000f, 1.000f, 1.000f); } // #FFFFFF Pure White Card Surface
    static ImVec4 ColorBorder()       { return ImVec4(0.898f, 0.898f, 0.898f, 1.000f); } // #E5E5E5 Fluent 2 Card Border (1px)
    static ImVec4 ColorBorderSubtle() { return ImVec4(0.941f, 0.941f, 0.941f, 1.000f); } // #F0F0F0 Subtle Inner Border

    // WinUI 3 Standard Buttons (Subtle off-white with crisp 1px border)
    static ImVec4 ColorBtnNormal()    { return ImVec4(0.984f, 0.988f, 0.996f, 1.000f); } // #FBFCFE
    static ImVec4 ColorBtnHover()     { return ImVec4(0.961f, 0.961f, 0.961f, 1.000f); } // #F5F5F5
    static ImVec4 ColorBtnActive()    { return ImVec4(0.922f, 0.922f, 0.922f, 1.000f); } // #EBEBEB

    // Draw realistic WinUI 3 elevation shadow beneath cards
    static void DrawCardShadow(const ImVec2& p_min, const ImVec2& p_max, float rounding = 8.0f);
    // Draw subtle button bottom border/shadow for physical elevation
    static void DrawButtonShadow(const ImVec2& p_min, const ImVec2& p_max, float rounding = 4.0f);

    // Draw 1/3-width centered horizontal divider line for clean visual partitioning
    static void DrawCenteredDivider(float verticalPadding = 8.0f);

    // WinUI 3 Native Tactile Button with physical press & native bottom edge
    static bool TactileButton(
        const char* label,
        const ImVec2& size = ImVec2(0, 0),
        ImU32 bgCol = IM_COL32(251, 252, 254, 255),
        ImU32 textCol = IM_COL32(31, 31, 31, 255),
        ImU32 borderCol = IM_COL32(224, 224, 224, 255),
        float rounding = 4.0f
    );

    // WinUI 3 Accent Button (Solid Accent Blue)
    static bool AccentButton(
        const char* label,
        const ImVec2& size = ImVec2(0, 0),
        float rounding = 4.0f
    );

    // WinUI 3 SelectorBar / Segmented button for Time Window (30s / 1m / 2m / 5m / 10m / 30m / 1h)
    static bool SegmentButton(const char* label, bool isSelected, float width = 46.0f, float height = 24.0f);

    // WinUI 3 Filter Chip / ToggleButton badge for channels (Current, Power, Voltage)
    static bool PillToggle(const char* label, bool* value, ImU32 activeColor, float height = 24.0f, float width = 0.0f);

    // Modern Radio Toggle with circular hollow/filled indicator (no pill border, gray when unchecked)
    static bool RadioToggle(const char* label, bool* value, ImU32 activeColor);
};

extern ImFont* g_FontDefault;
extern ImFont* g_FontLarge;
extern ImFont* g_FontBold;
extern ImFont* g_FontTitleBold;
extern ImFont* g_FontOswaldLarge;
extern ImFont* g_FontOswaldMedium;
extern ImFont* g_FontDseg;
extern ImFont* g_FontDsegMedium;

} // namespace CH570App
