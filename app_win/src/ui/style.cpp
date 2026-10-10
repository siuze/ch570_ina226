#include "style.hpp"
#include "g_iosevkaregular_font_data.hpp"
#include "g_iosevkabold_font_data.hpp"
#include "dseg7_font_data.hpp"
#include "implot.h"
#include "imgui_internal.h"
#include <windows.h>
#include <io.h>
#include <string>
#include <cstdlib>

namespace CH570App {

ImFont* g_FontDefault = nullptr;
ImFont* g_FontLarge = nullptr;
ImFont* g_FontBold = nullptr;
ImFont* g_FontTitleBold = nullptr;
ImFont* g_FontOswaldLarge = nullptr;
ImFont* g_FontOswaldMedium = nullptr;
ImFont* g_FontDseg = nullptr;
ImFont* g_FontDsegMedium = nullptr;

void UIStyle::ApplyModernLightTheme(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Fluent 2 Geometry: Cards 8px, Controls 4px
    style.WindowRounding    = 8.0f * dpiScale;
    style.ChildRounding     = 8.0f * dpiScale;
    style.FrameRounding     = 4.0f * dpiScale;
    style.PopupRounding     = 8.0f * dpiScale;
    style.ScrollbarRounding = 4.0f * dpiScale;
    style.GrabRounding      = 4.0f * dpiScale;
    style.TabRounding       = 6.0f * dpiScale;

    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 1.0f;

    style.WindowPadding     = ImVec2(12.0f * dpiScale, 8.0f * dpiScale);
    style.FramePadding      = ImVec2(8.0f * dpiScale, 3.5f * dpiScale);
    style.ItemSpacing       = ImVec2(8.0f * dpiScale, 6.0f * dpiScale);
    style.ItemInnerSpacing  = ImVec2(6.0f * dpiScale, 4.0f * dpiScale);
    style.ScrollbarSize     = 8.0f * dpiScale;
    style.GrabMinSize       = 8.0f * dpiScale;
    style.ButtonTextAlign   = ImVec2(0.5f, 0.5f);

    // Fluent 2 / WinUI 3 Light Palette
    colors[ImGuiCol_Text]                  = ColorTextMain();      // #1F1F1F
    colors[ImGuiCol_TextDisabled]          = ColorTextDisabled();  // #8D8D8D
    colors[ImGuiCol_WindowBg]              = ColorBgCanvas();      // #F3F3F3 Mica / Neutral
    colors[ImGuiCol_ChildBg]               = ColorBgCard();        // #FFFFFF Pure White
    colors[ImGuiCol_PopupBg]               = ImVec4(1.000f, 1.000f, 1.000f, 0.98f);
    colors[ImGuiCol_Border]                = ColorBorder();        // #E5E5E5
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    // Inputs, Combos, Sliders (WinUI 3 TextBoxes & Dropdowns)
    colors[ImGuiCol_FrameBg]               = ImVec4(1.000f, 1.000f, 1.000f, 1.00f); // #FFFFFF
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.965f, 0.965f, 0.965f, 1.00f); // #F6F6F6
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.930f, 0.930f, 0.930f, 1.00f); // #EDEDED

    // Title Bar / Menus
    colors[ImGuiCol_TitleBg]               = ColorBgCanvas();
    colors[ImGuiCol_TitleBgActive]         = ColorBgCanvas();
    colors[ImGuiCol_TitleBgCollapsed]      = ColorBgCanvas();
    colors[ImGuiCol_MenuBarBg]             = ColorBgCanvas();

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.95f, 0.95f, 0.95f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Checkbox & Radio (WinUI 3 Accent Blue Check)
    colors[ImGuiCol_CheckMark]             = ColorAccent(); // #0067C0

    // Sliders & Grabs
    colors[ImGuiCol_SliderGrab]            = ColorAccent();
    colors[ImGuiCol_SliderGrabActive]      = ColorAccentActive();

    // Buttons
    colors[ImGuiCol_Button]                = ColorBtnNormal();
    colors[ImGuiCol_ButtonHovered]         = ColorBtnHover();
    colors[ImGuiCol_ButtonActive]          = ColorBtnActive();

    // Headers & List Selectables
    colors[ImGuiCol_Header]                = ImVec4(0.930f, 0.955f, 0.985f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.880f, 0.925f, 0.975f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.800f, 0.880f, 0.950f, 1.00f);

    // TabView (WinUI 3 Native Tabs)
    colors[ImGuiCol_Tab]                   = ImVec4(0.930f, 0.930f, 0.930f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.970f, 0.970f, 0.970f, 1.00f);
    colors[ImGuiCol_TabSelected]           = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
    colors[ImGuiCol_TabDimmed]             = ImVec4(0.930f, 0.930f, 0.930f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]     = ImVec4(0.980f, 0.980f, 0.980f, 1.00f);

    // Tables
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.960f, 0.960f, 0.960f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ColorBorder();
    colors[ImGuiCol_TableBorderLight]      = ColorBorderSubtle();
    colors[ImGuiCol_TableRowBg]            = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);

    // ImPlot WinUI 3 Theme (Clean White Canvas with Crisp Delicate Grid)
    ImPlotStyle& pstyle = ImPlot::GetStyle();
    pstyle.Colors[ImPlotCol_FrameBg]       = ColorBgCard(); // Pure white
    pstyle.Colors[ImPlotCol_PlotBg]        = ColorBgCard();
    pstyle.Colors[ImPlotCol_PlotBorder]    = ColorBorder(); // #E5E5E5
    pstyle.Colors[ImPlotCol_LegendBg]      = ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
    pstyle.Colors[ImPlotCol_LegendBorder]  = ColorBorder();
    pstyle.Colors[ImPlotCol_LegendText]    = ColorTextMain();
    pstyle.Colors[ImPlotCol_TitleText]     = ColorTextMain();
    pstyle.Colors[ImPlotCol_InlayText]     = ColorTextMain();
    pstyle.Colors[ImPlotCol_AxisText]      = ColorTextMuted(); // #5D5D5D
    pstyle.Colors[ImPlotCol_AxisGrid]      = ImVec4(0.91f, 0.91f, 0.91f, 0.85f); // Subtle, clean grid line
    pstyle.Colors[ImPlotCol_AxisTick]      = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    pstyle.Colors[ImPlotCol_Crosshairs]    = ImVec4(0.40f, 0.40f, 0.40f, 0.70f);
    pstyle.PlotBorderSize                  = 1.0f;
    pstyle.PlotPadding                     = ImVec2(10.0f * dpiScale, 10.0f * dpiScale);
}

void UIStyle::LoadFonts(float baseFontSize, float dpiScale) {
    ImGuiIO& io = ImGui::GetIO();
    float scaledSize = baseFontSize * dpiScale;
    if (scaledSize < 13.0f) scaledSize = 13.0f;

    // Resolve Iosevka font paths
    std::string iosevkaRegPath;
    std::string iosevkaBoldPath;
    const char* localAppData = getenv("LOCALAPPDATA");
    if (localAppData) {
        std::string pReg = std::string(localAppData) + "\\Microsoft\\Windows\\Fonts\\Iosevka-Regular.ttc";
        std::string pBold = std::string(localAppData) + "\\Microsoft\\Windows\\Fonts\\Iosevka-Bold.ttc";
        if (_access(pReg.c_str(), 0) == 0) iosevkaRegPath = pReg;
        if (_access(pBold.c_str(), 0) == 0) iosevkaBoldPath = pBold;
    }
    if (iosevkaRegPath.empty()) {
        const char* pRegWin = "C:\\Windows\\Fonts\\Iosevka-Regular.ttc";
        const char* pBoldWin = "C:\\Windows\\Fonts\\Iosevka-Bold.ttc";
        if (_access(pRegWin, 0) == 0) iosevkaRegPath = pRegWin;
        if (_access(pBoldWin, 0) == 0) iosevkaBoldPath = pBoldWin;
    }

    bool hasIosevka = !iosevkaRegPath.empty();
    if (iosevkaBoldPath.empty() && hasIosevka) iosevkaBoldPath = iosevkaRegPath;

    const char* yaheiPath = "C:\\Windows\\Fonts\\msyh.ttc";
    const char* yaheiBoldPath = "C:\\Windows\\Fonts\\msyhbd.ttc";
    bool hasYahei = (_access(yaheiPath, 0) == 0);
    bool hasYaheiBold = (_access(yaheiBoldPath, 0) == 0);
    const char* boldYaheiPath = hasYaheiBold ? yaheiBoldPath : yaheiPath;

    static const ImWchar iosevkaRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x0370, 0x03FF, // Greek (Ω, µ)
        0x2000, 0x206F, // General Punctuation
        0x2190, 0x21FF, // Arrows
        0x25A0, 0x25FF, // Geometric Shapes (●, ○, ■, □)
        0
    };

    if (hasIosevka) {
        // --- 1. Default UI Font: Iosevka Regular (Latin/Numbers/Symbols) + YaHei Fallback (Chinese) ---
        ImFontConfig iosevkaCfg;
        iosevkaCfg.FontNo = 0;
        iosevkaCfg.OversampleH = 1;
        iosevkaCfg.OversampleV = 1;
        iosevkaCfg.PixelSnapH = true;
        iosevkaCfg.RasterizerMultiply = 1.15f;
        g_FontDefault = io.Fonts->AddFontFromFileTTF(
            iosevkaRegPath.c_str(), scaledSize * 1.05f, &iosevkaCfg, iosevkaRanges
        );

        if (hasYahei) {
            ImFontConfig yaheiCfg;
            yaheiCfg.MergeMode = true;
            yaheiCfg.OversampleH = 1;
            yaheiCfg.OversampleV = 1;
            yaheiCfg.PixelSnapH = true;
            yaheiCfg.RasterizerMultiply = 1.15f;
            yaheiCfg.GlyphOffset.y = -1.5f * dpiScale;
            io.Fonts->AddFontFromFileTTF(
                yaheiPath, scaledSize, &yaheiCfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()
            );
        }

        // --- 2. Large KPI Numeric Font: Pure Iosevka Bold ---
        ImFontConfig largeCfg;
        largeCfg.FontNo = 0;
        largeCfg.OversampleH = 1;
        largeCfg.OversampleV = 1;
        largeCfg.PixelSnapH = true;
        largeCfg.RasterizerMultiply = 1.15f;
        g_FontLarge = io.Fonts->AddFontFromFileTTF(
            iosevkaBoldPath.c_str(), scaledSize * 1.65f, &largeCfg, iosevkaRanges
        );

        // --- 3. Bold Header Font: Iosevka Bold + YaHei Bold Fallback ---
        ImFontConfig boldCfg;
        boldCfg.FontNo = 0;
        boldCfg.OversampleH = 1;
        boldCfg.OversampleV = 1;
        boldCfg.PixelSnapH = true;
        boldCfg.RasterizerMultiply = 1.15f;
        g_FontBold = io.Fonts->AddFontFromFileTTF(
            iosevkaBoldPath.c_str(), scaledSize * 1.08f, &boldCfg, iosevkaRanges
        );

        if (hasYahei) {
            ImFontConfig yaheiBoldCfg;
            yaheiBoldCfg.MergeMode = true;
            yaheiBoldCfg.OversampleH = 1;
            yaheiBoldCfg.OversampleV = 1;
            yaheiBoldCfg.PixelSnapH = true;
            yaheiBoldCfg.RasterizerMultiply = 1.15f;
            yaheiBoldCfg.GlyphOffset.y = -1.5f * dpiScale;
            io.Fonts->AddFontFromFileTTF(
                boldYaheiPath, scaledSize * 1.05f, &yaheiBoldCfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()
            );
        }
    } else {
        // Embedded Iosevka keeps the UI typography deterministic on machines
        // where the user has not installed the optional system font.
        ImFontConfig config;
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        config.RasterizerMultiply = 1.15f;
        config.GlyphOffset.y = -1.5f * dpiScale;

        ImFontConfig embeddedRegular = config;
        embeddedRegular.GlyphOffset.y = 0.0f;
        g_FontDefault = io.Fonts->AddFontFromMemoryTTF(
            (void*)g_IosevkaRegularData, (int)g_IosevkaRegularSize,
            scaledSize * 1.05f, &embeddedRegular, iosevkaRanges
        );
        g_FontLarge = io.Fonts->AddFontFromMemoryTTF(
            (void*)g_IosevkaBoldData, (int)g_IosevkaBoldSize,
            scaledSize * 1.65f, &embeddedRegular, iosevkaRanges
        );
        g_FontBold = io.Fonts->AddFontFromMemoryTTF(
            (void*)g_IosevkaBoldData, (int)g_IosevkaBoldSize,
            scaledSize * 1.08f, &embeddedRegular, iosevkaRanges
        );

        // Merge Chinese glyphs into each embedded Latin font when available.
        if (hasYahei) {
            ImFontConfig yaheiCfg = config;
            yaheiCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(yaheiPath, scaledSize, &yaheiCfg,
                                          io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            ImFontConfig yaheiBoldCfg = config;
            yaheiBoldCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(boldYaheiPath, scaledSize * 1.05f, &yaheiBoldCfg,
                                          io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        }
    }

    // --- Load Oswald Font for KPI Card Values and Units ---
    std::string oswaldPath;
    if (localAppData) {
        std::string pOswald = std::string(localAppData) + "\\Microsoft\\Windows\\Fonts\\Oswald-Medium.otf";
        if (_access(pOswald.c_str(), 0) == 0) oswaldPath = pOswald;
    }
    if (oswaldPath.empty()) {
        const char* pWinOswald = "C:\\Windows\\Fonts\\Oswald-Medium.otf";
        if (_access(pWinOswald, 0) == 0) oswaldPath = pWinOswald;
    }

    if (!oswaldPath.empty()) {
        ImFontConfig oswaldLargeCfg;
        oswaldLargeCfg.OversampleH = 1;
        oswaldLargeCfg.OversampleV = 1;
        oswaldLargeCfg.PixelSnapH = true;
        oswaldLargeCfg.RasterizerMultiply = 1.15f;
        g_FontOswaldLarge = io.Fonts->AddFontFromFileTTF(
            oswaldPath.c_str(), scaledSize * 2.30f, &oswaldLargeCfg, io.Fonts->GetGlyphRangesDefault()
        );

        ImFontConfig oswaldMedCfg;
        oswaldMedCfg.OversampleH = 1;
        oswaldMedCfg.OversampleV = 1;
        oswaldMedCfg.PixelSnapH = true;
        oswaldMedCfg.RasterizerMultiply = 1.15f;
        g_FontOswaldMedium = io.Fonts->AddFontFromFileTTF(
            oswaldPath.c_str(), scaledSize * 1.35f, &oswaldMedCfg, io.Fonts->GetGlyphRangesDefault()
        );
    } else {
        g_FontOswaldLarge = g_FontLarge;
        g_FontOswaldMedium = g_FontBold;
    }

    // --- Load Title Bold Chinese Font (Heavy Black Gothic) ---
    // NotoSansSC-VF.ttf on Windows is a Variable Font which stb_truetype opens as hairline thin.
    // msyhbd.ttc (Microsoft YaHei Bold) or simhei.ttf (SimHei) provides true Black/Heavy bold weight.
    const char* simheiPath = "C:\\Windows\\Fonts\\simhei.ttf";
    const char* titleChineseFont = (_access(simheiPath, 0) == 0) ? simheiPath :
                                   ((_access(yaheiBoldPath, 0) == 0) ? yaheiBoldPath : boldYaheiPath);
    ImFontConfig titleCfg;
    titleCfg.FontNo = 0;
    titleCfg.OversampleH = 1;
    titleCfg.OversampleV = 1;
    titleCfg.PixelSnapH = true;
    titleCfg.RasterizerMultiply = 1.35f; // Extra bold stroke density
    titleCfg.GlyphOffset.y = -1.0f * dpiScale;
    g_FontTitleBold = io.Fonts->AddFontFromFileTTF(
        titleChineseFont, scaledSize * 1.25f, &titleCfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()
    );
    if (!g_FontTitleBold) g_FontTitleBold = g_FontBold;

    // --- Load 7-Segment Digital Tube Font (DSEG7 Classic) from Embedded Binary Memory ---
    ImFontConfig dsegLargeCfg;
    dsegLargeCfg.FontDataOwnedByAtlas = false;
    dsegLargeCfg.OversampleH = 1;
    dsegLargeCfg.OversampleV = 1;
    dsegLargeCfg.PixelSnapH = true;
    dsegLargeCfg.RasterizerMultiply = 1.15f;
    g_FontDseg = io.Fonts->AddFontFromMemoryTTF(
        (void*)g_Dseg7ClassicBoldData, (int)g_Dseg7ClassicBoldSize,
        scaledSize * 1.80f, &dsegLargeCfg, io.Fonts->GetGlyphRangesDefault()
    );

    ImFontConfig dsegMedCfg;
    dsegMedCfg.FontDataOwnedByAtlas = false;
    dsegMedCfg.OversampleH = 1;
    dsegMedCfg.OversampleV = 1;
    dsegMedCfg.PixelSnapH = true;
    dsegMedCfg.RasterizerMultiply = 1.15f;
    g_FontDsegMedium = io.Fonts->AddFontFromMemoryTTF(
        (void*)g_Dseg7ClassicBoldData, (int)g_Dseg7ClassicBoldSize,
        scaledSize * 1.05f, &dsegMedCfg, io.Fonts->GetGlyphRangesDefault()
    );

    if (!g_FontDseg) g_FontDseg = g_FontOswaldLarge;
    if (!g_FontDsegMedium) g_FontDsegMedium = g_FontOswaldMedium;

    if (!g_FontDefault) {
        g_FontDefault = io.Fonts->AddFontDefault();
        g_FontLarge = g_FontDefault;
        g_FontBold = g_FontDefault;
        g_FontTitleBold = g_FontDefault;
        g_FontOswaldLarge = g_FontDefault;
        g_FontOswaldMedium = g_FontDefault;
        g_FontDseg = g_FontDefault;
        g_FontDsegMedium = g_FontDefault;
    }
}

void UIStyle::DrawCenteredDivider(float verticalPadding) {
    ImGui::Dummy(ImVec2(0.0f, verticalPadding));
    float availW = ImGui::GetContentRegionAvail().x;
    float dividerW = availW / 3.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.x += (availW - dividerW) * 0.5f;
    ImVec2 p_end(p.x + dividerW, p.y + 1.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(p, p_end, IM_COL32(220, 220, 220, 255));
    ImGui::Dummy(ImVec2(0.0f, verticalPadding));
}

void UIStyle::DrawCardShadow(const ImVec2& p_min, const ImVec2& p_max, float rounding) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    // WinUI 3 Elevation 2 Shadow (Soft, ambient, professional)
    drawList->AddRectFilled(
        ImVec2(p_min.x - 2.0f, p_min.y - 1.0f),
        ImVec2(p_max.x + 2.0f, p_max.y + 4.0f),
        IM_COL32(0, 0, 0, 6), rounding + 2.0f
    );
    drawList->AddRectFilled(
        ImVec2(p_min.x - 1.0f, p_min.y),
        ImVec2(p_max.x + 1.0f, p_max.y + 2.0f),
        IM_COL32(0, 0, 0, 10), rounding + 1.0f
    );
    drawList->AddRectFilled(
        ImVec2(p_min.x, p_min.y + 0.5f),
        ImVec2(p_max.x, p_max.y + 1.0f),
        IM_COL32(0, 0, 0, 14), rounding
    );
}

void UIStyle::DrawButtonShadow(const ImVec2& p_min, const ImVec2& p_max, float rounding) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    // Subtle WinUI 3 button bottom stroke
    drawList->AddLine(
        ImVec2(p_min.x + rounding, p_max.y),
        ImVec2(p_max.x - rounding, p_max.y),
        IM_COL32(0, 0, 0, 24), 1.0f
    );
}

bool UIStyle::TactileButton(const char* label, const ImVec2& size_arg, ImU32 bgCol, ImU32 textCol, ImU32 borderCol, float rounding) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

    ImVec2 pos = window->DC.CursorPos;
    if (style.FramePadding.y < window->DC.CurrLineTextBaseOffset)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
    ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImDrawList* drawList = window->DrawList;

    // WinUI 3 Micro press response (1.0px shift)
    float offsetY = held ? 1.0f : 0.0f;
    ImVec2 b_min(bb.Min.x, bb.Min.y + offsetY);
    ImVec2 b_max(bb.Max.x, bb.Max.y + offsetY);

    // 1. Button Background
    ImU32 currentBg = bgCol;
    if (held) {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(bgCol);
        c.x = ImMax(0.0f, c.x * 0.92f);
        c.y = ImMax(0.0f, c.y * 0.92f);
        c.z = ImMax(0.0f, c.z * 0.92f);
        currentBg = ImGui::ColorConvertFloat4ToU32(c);
    } else if (hovered) {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(bgCol);
        c.x = ImMin(1.0f, c.x * 1.04f);
        c.y = ImMin(1.0f, c.y * 1.04f);
        c.z = ImMin(1.0f, c.z * 1.04f);
        currentBg = ImGui::ColorConvertFloat4ToU32(c);
    }
    drawList->AddRectFilled(b_min, b_max, currentBg, rounding);

    // 2. 1px Delicate Border
    drawList->AddRect(b_min, b_max, borderCol, rounding, 0, 1.0f);

    // 3. WinUI 3 Bottom edge stroke (provides subtle physical elevation when not pressed)
    if (!held) {
        drawList->AddLine(
            ImVec2(b_min.x + rounding, b_max.y - 0.5f),
            ImVec2(b_max.x - rounding, b_max.y - 0.5f),
            IM_COL32(0, 0, 0, 26), 1.0f
        );
    }

    // 4. Centered Text
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
    ImGui::RenderTextClipped(
        b_min, b_max,
        label, nullptr, &label_size, ImVec2(0.5f, 0.5f)
    );
    ImGui::PopStyleColor();

    return pressed;
}

bool UIStyle::AccentButton(const char* label, const ImVec2& size, float rounding) {
    ImU32 bgCol = IM_COL32(0, 103, 192, 255);      // #0067C0
    ImU32 textCol = IM_COL32(255, 255, 255, 255);  // #FFFFFF
    ImU32 borderCol = IM_COL32(0, 90, 158, 255);   // #005A9E
    return TactileButton(label, size, bgCol, textCol, borderCol, rounding);
}

bool UIStyle::SegmentButton(const char* label, bool isSelected, float width, float height) {
    if (isSelected) {
        // Active: Solid Windows 11 Accent Blue
        ImU32 bgCol = IM_COL32(0, 103, 192, 255);      // #0067C0
        ImU32 textCol = IM_COL32(255, 255, 255, 255);  // #FFFFFF
        ImU32 borderCol = IM_COL32(0, 90, 158, 255);
        return TactileButton(label, ImVec2(width, height), bgCol, textCol, borderCol, 4.0f);
    } else {
        // Inactive: Crisp off-white with delicate border
        ImU32 bgCol = IM_COL32(255, 255, 255, 255);    // #FFFFFF
        ImU32 textCol = IM_COL32(93, 93, 93, 255);     // #5D5D5D
        ImU32 borderCol = IM_COL32(224, 224, 224, 255);// #E0E0E0
        return TactileButton(label, ImVec2(width, height), bgCol, textCol, borderCol, 4.0f);
    }
}

bool UIStyle::PillToggle(const char* label, bool* value, ImU32 activeColor, float height, float width) {
    bool currentVal = *value;
    char fullLabel[64];
    snprintf(fullLabel, sizeof(fullLabel), "%s %s", currentVal ? "●" : "○", label);

    ImVec2 label_size = ImGui::CalcTextSize(fullLabel, nullptr, true);
    float btnWidth = (width > 0.0f) ? width : (label_size.x + 18.0f);

    ImU32 bgCol;
    ImU32 textCol;
    ImU32 borderCol;

    if (currentVal) {
        ImVec4 ac = ImGui::ColorConvertU32ToFloat4(activeColor);
        // Soft pastel tint background of activeColor
        bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
            ac.x * 0.10f + 0.90f * 1.0f,
            ac.y * 0.10f + 0.90f * 1.0f,
            ac.z * 0.10f + 0.90f * 1.0f,
            1.0f
        ));
        textCol = activeColor;
        borderCol = activeColor;
    } else {
        bgCol = IM_COL32(255, 255, 255, 255);     // #FFFFFF
        textCol = IM_COL32(93, 93, 93, 255);      // #5D5D5D
        borderCol = IM_COL32(224, 224, 224, 255); // #E0E0E0
    }

    if (TactileButton(fullLabel, ImVec2(btnWidth, height), bgCol, textCol, borderCol, 4.0f)) {
        *value = !currentVal;
        return true;
    }
    return false;
}

bool UIStyle::RadioToggle(const char* label, bool* value, ImU32 activeColor) {
    if (!value) return false;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    float radius = 7.0f;
    float circleGap = 6.0f;
    float height = 24.0f; // Match TactileButton and SegmentButton height for perfect horizontal alignment
    float width = radius * 2.0f + circleGap + labelSize.x;

    ImVec2 pos = window->DC.CursorPos;
    ImRect totalBb(pos, ImVec2(pos.x + width, pos.y + height));
    ImGui::ItemSize(totalBb, 0.0f);
    if (!ImGui::ItemAdd(totalBb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(totalBb, id, &hovered, &held);
    if (pressed) {
        *value = !(*value);
        ImGui::MarkItemEdited(id);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 center(pos.x + radius, pos.y + height * 0.5f);

    if (*value) {
        // Active/Checked: Circle filled with activeColor, with inner white dot
        drawList->AddCircleFilled(center, radius, activeColor, 16);
        drawList->AddCircleFilled(center, 2.2f, IM_COL32(255, 255, 255, 255), 12);

        ImVec2 textPos(pos.x + radius * 2.0f + circleGap, pos.y + (height - labelSize.y) * 0.5f);
        drawList->AddText(textPos, IM_COL32(31, 31, 31, 255), label);
    } else {
        // Inactive/Unchecked: Hollow circle with gray outline, text in gray
        ImU32 grayCol = hovered ? IM_COL32(110, 110, 110, 255) : IM_COL32(160, 160, 160, 255);
        drawList->AddCircle(center, radius, grayCol, 16, 1.5f);

        ImVec2 textPos(pos.x + radius * 2.0f + circleGap, pos.y + (height - labelSize.y) * 0.5f);
        drawList->AddText(textPos, grayCol, label);
    }

    return pressed;
}

} // namespace CH570App
