#include "dashboard.hpp"
#include "style.hpp"
#include "imgui.h"
#include "implot.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace CH570App {

static std::string FormatMaxDigits(double val, int maxDigits) {
    if (std::isnan(val) || std::isinf(val)) return "0";
    double absVal = std::abs(val);
    if (absVal == 0.0) {
        if (maxDigits == 4) return "0.0";
        if (maxDigits == 5) return "0.00";
        return "0.0";
    }

    int intDigits = (absVal < 1.0) ? 1 : static_cast<int>(std::floor(std::log10(absVal))) + 1;
    int decimals = maxDigits - intDigits;
    if (decimals < 0) decimals = 0;

    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, val);
    return std::string(buf);
}

static std::string Format5SlotDseg(double val) {
    if (std::isnan(val) || std::isinf(val)) return "    0";
    bool neg = (val < -0.0001);
    double absVal = std::abs(val);

    char numBuf[32];
    if (absVal >= 10000.0) {
        snprintf(numBuf, sizeof(numBuf), "%.0f", absVal);
    } else if (absVal >= 1000.0) {
        snprintf(numBuf, sizeof(numBuf), "%.1f", absVal);
    } else if (absVal >= 100.0) {
        snprintf(numBuf, sizeof(numBuf), "%.2f", absVal);
    } else {
        snprintf(numBuf, sizeof(numBuf), "%.2f", absVal);
    }

    int digitCount = 0;
    for (int i = 0; numBuf[i] != '\0'; ++i) {
        if (numBuf[i] != '.') digitCount++;
    }
    if (neg) digitCount++;

    int padCount = 5 - digitCount;
    if (padCount < 0) padCount = 0;

    std::string res;
    // When DSEG7 is loaded, '!' is the blank segment cell (advance 816, 0 contours).
    // If fallback font (Oswald/Default) is ever active, use ' ' (space) to prevent literal '!' from displaying.
    char padChar = (g_FontDseg && g_FontDseg != g_FontDefault && g_FontDseg != g_FontLarge && g_FontDseg != g_FontOswaldLarge) ? '!' : ' ';
    res.append(padCount, padChar);
    if (neg) res.push_back('-');
    res.append(numBuf);
    return res;
}

Dashboard::Dashboard(BleScanner& bleScanner)
    : m_bleScanner(bleScanner) {
    memset(m_com1SendBuffer, 0, sizeof(m_com1SendBuffer));
    memset(m_customCmdBuffer, 0, sizeof(m_customCmdBuffer));

    // Default time window to 30s as requested by user
    AppState::Instance().timeWindowSeconds = 30.0f;
    m_relTime.reserve(15000);
}

void Dashboard::Render() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::Begin("MainCanvas", nullptr, windowFlags);
    ImGui::PopStyleVar();

    RenderStatusBar();
    UIStyle::DrawCenteredDivider(4.0f);
    RenderKpiCards();
    UIStyle::DrawCenteredDivider(5.0f);
    RenderWaveform();
    UIStyle::DrawCenteredDivider(5.0f);
    RenderBottomTabs();

    ImGui::End();
}

void Dashboard::RenderStatusBar() {
    auto& state = AppState::Instance();

    ImVec2 p_min = ImGui::GetCursorScreenPos();
    float barWidth = ImGui::GetContentRegionAvail().x;
    float barHeight = 32.0f;
    ImVec2 p_max(p_min.x + barWidth, p_min.y + barHeight);
    float centerY = p_min.y + barHeight * 0.5f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    UIStyle::DrawCardShadow(p_min, p_max, 6.0f);
    drawList->AddRectFilled(p_min, p_max, ImGui::GetColorU32(UIStyle::ColorBgCard()), 6.0f);
    drawList->AddRect(p_min, p_max, ImGui::GetColorU32(UIStyle::ColorBorder()), 6.0f, 0, 1.0f);

    float fontH = ImGui::GetTextLineHeight();
    float textY = centerY - fontH * 0.5f - 1.0f;
    float btnH = 22.0f;
    float btnY = centerY - btnH * 0.5f;

    // 1. BLE Scanner Status & Toggle (Organic breathing LED dot)
    float curX = p_min.x + 12.0f;
    if (state.bleScanning.load()) {
        float t = (float)ImGui::GetTime();
        float breath = 0.5f + 0.5f * sinf(t * 2.4f);
        float alpha = 0.30f + 0.70f * breath;

        float r = 4.5f;
        ImVec2 center(curX + r, centerY);
        drawList->AddCircleFilled(center, r + 3.0f, IM_COL32(16, 185, 129, (int)(65 * alpha)), 16);
        drawList->AddCircleFilled(center, r, IM_COL32(5, 150, 105, (int)(255 * alpha)), 16);
        curX += r * 2.0f + 7.0f;

        ImGui::SetCursorScreenPos(ImVec2(curX, textY));
        ImGui::TextColored(UIStyle::ColorCurrent(), "BLE 监听中");
        curX += ImGui::CalcTextSize("BLE 监听中").x + 8.0f;

        ImGui::SetCursorScreenPos(ImVec2(curX, btnY));
        if (UIStyle::TactileButton("停止 BLE", ImVec2(68, btnH), IM_COL32(255, 240, 240, 255), IM_COL32(196, 43, 28, 255), IM_COL32(248, 215, 218, 255), 4.0f)) {
            m_bleScanner.Stop();
        }
        curX += 68.0f + 8.0f;
    } else {
        float r = 4.5f;
        ImVec2 center(curX + r, centerY);
        drawList->AddCircle(center, r, IM_COL32(160, 160, 160, 255), 16, 1.5f);
        curX += r * 2.0f + 7.0f;

        ImGui::SetCursorScreenPos(ImVec2(curX, textY));
        ImGui::TextColored(UIStyle::ColorTextMuted(), "BLE 停止");
        curX += ImGui::CalcTextSize("BLE 停止").x + 8.0f;

        ImGui::SetCursorScreenPos(ImVec2(curX, btnY));
        if (UIStyle::TactileButton("启动 BLE", ImVec2(68, btnH), IM_COL32(239, 249, 240, 255), IM_COL32(15, 123, 15, 255), IM_COL32(196, 232, 197, 255), 4.0f)) {
            m_bleScanner.Start();
        }
        curX += 68.0f + 8.0f;
    }

    char bleStatsBuf[64];
    snprintf(bleStatsBuf, sizeof(bleStatsBuf), "| 信号: %d dBm | 广播包: %llu",
             (int)state.bleLastRssi.load(),
             (unsigned long long)state.blePacketCount.load());
    ImGui::SetCursorScreenPos(ImVec2(curX, textY));
    ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", bleStatsBuf);
    curX += ImGui::CalcTextSize(bleStatsBuf).x + 16.0f;

    // 2. Dongle Status (Aligned to ~36% of bar width)
    float dongleStartX = p_min.x + barWidth * 0.36f;
    if (dongleStartX < curX) dongleStartX = curX;
    curX = dongleStartX;

    ImGui::SetCursorScreenPos(ImVec2(curX, textY));
    if (state.dongleConnected.load()) {
        char dongleBuf[64];
        snprintf(dongleBuf, sizeof(dongleBuf), "● Dongle: %s", state.donglePortName.c_str());
        ImGui::TextColored(UIStyle::ColorVoltage(), "%s", dongleBuf);
        curX += ImGui::CalcTextSize(dongleBuf).x + 8.0f;
    } else {
        const char* dongleText = "○ Dongle 未连接";
        ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", dongleText);
        curX += ImGui::CalcTextSize(dongleText).x + 8.0f;
    }

    ImGui::SetCursorScreenPos(ImVec2(curX, btnY));
    if (UIStyle::TactileButton("自动探测", ImVec2(68, btnH), IM_COL32(255, 255, 255, 255), IM_COL32(0, 103, 192, 255), IM_COL32(224, 224, 224, 255), 4.0f)) {
        SerialManager::Instance().AutoDetectDongle();
    }
    curX += 68.0f + 8.0f;

    char dongleStatsBuf[64];
    if (state.dongleRssiValid.load()) {
        snprintf(dongleStatsBuf, sizeof(dongleStatsBuf), "| 2.4G 信号: %d dBm | 遥测: %llu 帧",
                 state.dongleRssi.load(),
                 (unsigned long long)state.donglePacketCount.load());
    } else {
        snprintf(dongleStatsBuf, sizeof(dongleStatsBuf), "| 2.4G 信号: -- dBm | 遥测: %llu 帧",
                 (unsigned long long)state.donglePacketCount.load());
    }
    ImGui::SetCursorScreenPos(ImVec2(curX, textY));
    ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", dongleStatsBuf);
    curX += ImGui::CalcTextSize(dongleStatsBuf).x + 16.0f;

    // 2b. Firmware version (auto-queried via CMD:VER? after COM2 connects)
    {
        std::string dongleVer, probeVer;
        state.GetFwVersions(dongleVer, probeVer);
        bool probeOk = !probeVer.empty() && probeVer.find("not connected") == std::string::npos;
        char fwBuf[128];
        fwBuf[0] = '\0';
        if (probeOk && !dongleVer.empty()) {
            if (dongleVer == probeVer)
                snprintf(fwBuf, sizeof(fwBuf), "| 固件 %s", dongleVer.c_str());
            else
                snprintf(fwBuf, sizeof(fwBuf), "| 固件 D:%s P:%s", dongleVer.c_str(), probeVer.c_str());
        } else if (!dongleVer.empty()) {
            snprintf(fwBuf, sizeof(fwBuf), "| 固件 %s", dongleVer.c_str());
        }
        if (fwBuf[0] != '\0') {
            ImGui::SetCursorScreenPos(ImVec2(curX, textY));
            ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", fwBuf);
            curX += ImGui::CalcTextSize(fwBuf).x + 16.0f;
        }
    }

    // 3. COM1 Passthrough Status (Aligned to ~76% of bar width)
    float com1StartX = p_min.x + barWidth * 0.76f;
    if (com1StartX < curX) com1StartX = curX;
    curX = com1StartX;

    ImGui::SetCursorScreenPos(ImVec2(curX, textY));
    if (state.com1Open.load()) {
        auto now = std::chrono::steady_clock::now();
        uint64_t nowMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        bool isFlashing = (nowMs - state.lastCom1ActivityMs.load() < 160);

        char com1Buf[64];
        snprintf(com1Buf, sizeof(com1Buf), "● 透传串口: %s", state.com1PortName.c_str());
        if (isFlashing) {
            ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), "%s", com1Buf);
        } else {
            ImGui::TextColored(UIStyle::ColorCurrent(), "%s", com1Buf);
        }
    } else {
        ImGui::TextColored(UIStyle::ColorTextMuted(), "○ 透传串口关闭");
    }

    ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_max.y + 4.0f));
}

void Dashboard::RenderKpiCards() {
    auto& state = AppState::Instance();

    MetricStats vStats, iStats, pStats;
    state.history.GetWindowStats(state.timeWindowSeconds, vStats, iStats, pStats);
    if (vStats.count == 0) {
        vStats = state.history.GetVStats();
        iStats = state.history.GetIStats();
        pStats = state.history.GetPStats();
    }

    float availWidth = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    // 4 cards layout: 1. Current, 2. Power, 3. Voltage (mV), 4. Capacity & Energy (Ah / Wh)
    float cardWidth = (availWidth - 3.0f * spacing) / 4.0f;
    float cardHeight = 108.0f;

    auto renderCardHeader = [](ImDrawList* drawList, const ImVec2& p_min, const ImVec2& p_max,
                               const char* zhTitle, const char* enTitle, ImVec4 textCol, ImU32 lineCol) {
        ImGui::SetCursorScreenPos(ImVec2(p_min.x + 16.0f, p_min.y + 8.0f));
        ImGui::PushFont(g_FontTitleBold);
        ImGui::TextColored(textCol, "%s", zhTitle);
        ImGui::PopFont();
        if (enTitle && enTitle[0] != '\0') {
            ImGui::SameLine(0, 6.0f);
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(textCol, "%s", enTitle);
            ImGui::PopFont();
        }

        // 8px clean clearance between title text and line
        float lineY = p_min.y + 36.0f;
        drawList->AddLine(ImVec2(p_min.x + 14.0f, lineY), ImVec2(p_max.x - 14.0f, lineY), lineCol, 1.2f);
    };

    float slot5W = g_FontDseg->CalcTextSizeA(g_FontDseg->LegacySize, FLT_MAX, 0.0f, "88888").x;
    float unitOffsetLarge = slot5W + 8.0f;
    float baselineOffset = 9.0f; // Oswald Medium baseline offset against DSEG Large

    // --- Card 1: Shunt Current (Priority 1) ---
    {
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + cardWidth, p_min.y + cardHeight);
        UIStyle::DrawCardShadow(p_min, p_max, 8.0f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, UIStyle::ColorBgCard());
        ImGui::PushStyleColor(ImGuiCol_Border, UIStyle::ColorBorder());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::BeginChild("CardCurrent", ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            renderCardHeader(drawList, p_min, p_max, "采样电流", "SHUNT CURRENT", UIStyle::ColorCurrent(), IM_COL32(22, 163, 74, 180));

            float contentX = p_min.x + 14.0f;
            float contentY = p_min.y + 46.0f;

            // Faint 7-segment unlit LCD background
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::PushFont(g_FontDseg);
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.07f), "88888");

            // Active 5-slot 7-segment numerical value
            std::string dsegStr = Format5SlotDseg(iStats.current);
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", dsegStr.c_str());
            ImGui::PopFont();

            // 100% Fixed Unit Position (never jumps)
            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetLarge, contentY + baselineOffset));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorCurrent(), "mA");
            ImGui::PopFont();

            // Right-aligned gray rolling min/max stats with guaranteed right margin & bottom clearance
            std::string minStr = "min: " + FormatMaxDigits(iStats.min_val, 4);
            std::string maxStr = "max: " + FormatMaxDigits(iStats.max_val, 4);

            ImGui::PushFont(g_FontOswaldMedium);
            float wMin = ImGui::CalcTextSize(minStr.c_str()).x;
            float wMax = ImGui::CalcTextSize(maxStr.c_str()).x;
            float statW = (wMin > wMax ? wMin : wMax);
            float statX = p_max.x - 18.0f - statW;

            ImGui::SetCursorScreenPos(ImVec2(statX, contentY + 1.0f));
            ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", minStr.c_str());
            ImGui::SetCursorScreenPos(ImVec2(statX, contentY + 21.0f));
            ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", maxStr.c_str());
            ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();

    // --- Card 2: Instant Power (Priority 2) ---
    {
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + cardWidth, p_min.y + cardHeight);
        UIStyle::DrawCardShadow(p_min, p_max, 8.0f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, UIStyle::ColorBgCard());
        ImGui::PushStyleColor(ImGuiCol_Border, UIStyle::ColorBorder());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::BeginChild("CardPower", ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            renderCardHeader(drawList, p_min, p_max, "实时功率", "INSTANT POWER", UIStyle::ColorPower(), IM_COL32(217, 119, 6, 180));

            float contentX = p_min.x + 14.0f;
            float contentY = p_min.y + 46.0f;

            // Faint 7-segment unlit LCD background
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::PushFont(g_FontDseg);
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.07f), "88888");

            // Active 5-slot 7-segment numerical value
            std::string dsegStr = Format5SlotDseg(pStats.current);
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", dsegStr.c_str());
            ImGui::PopFont();

            // 100% Fixed Unit Position (never jumps)
            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetLarge, contentY + baselineOffset));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorPower(), "mW");
            ImGui::PopFont();

            // Right-aligned gray rolling min/max stats with guaranteed right margin & bottom clearance
            std::string minStr = "min: " + FormatMaxDigits(pStats.min_val, 4);
            std::string maxStr = "max: " + FormatMaxDigits(pStats.max_val, 4);

            ImGui::PushFont(g_FontOswaldMedium);
            float wMin = ImGui::CalcTextSize(minStr.c_str()).x;
            float wMax = ImGui::CalcTextSize(maxStr.c_str()).x;
            float statW = (wMin > wMax ? wMin : wMax);
            float statX = p_max.x - 18.0f - statW;

            ImGui::SetCursorScreenPos(ImVec2(statX, contentY + 1.0f));
            ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", minStr.c_str());
            ImGui::SetCursorScreenPos(ImVec2(statX, contentY + 21.0f));
            ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", maxStr.c_str());
            ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();

    // --- Card 3: Bus Voltage (Priority 3, in mV) ---
    {
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + cardWidth, p_min.y + cardHeight);
        UIStyle::DrawCardShadow(p_min, p_max, 8.0f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, UIStyle::ColorBgCard());
        ImGui::PushStyleColor(ImGuiCol_Border, UIStyle::ColorBorder());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::BeginChild("CardVoltage", ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            renderCardHeader(drawList, p_min, p_max, "母线电压", "BUS VOLTAGE", UIStyle::ColorVoltage(), IM_COL32(2, 132, 199, 180));

            float contentX = p_min.x + 14.0f;
            float contentY = p_min.y + 46.0f;

            double mv_cur = vStats.current * 1000.0;
            double mv_min = vStats.min_val * 1000.0;
            double mv_max = vStats.max_val * 1000.0;

            // Faint 7-segment unlit LCD background
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::PushFont(g_FontDseg);
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.07f), "88888");

            // Active 5-slot 7-segment numerical value
            std::string dsegStr = Format5SlotDseg(mv_cur);
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", dsegStr.c_str());
            ImGui::PopFont();

            // 100% Fixed Unit Position (never jumps)
            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetLarge, contentY + baselineOffset));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorVoltage(), "mV");
            ImGui::PopFont();

            // Right-aligned gray rolling min/max stats with guaranteed right margin & bottom clearance
            std::string minStr = "min: " + FormatMaxDigits(mv_min, 4);
            std::string maxStr = "max: " + FormatMaxDigits(mv_max, 4);

            ImGui::PushFont(g_FontOswaldMedium);
            float wMin = ImGui::CalcTextSize(minStr.c_str()).x;
            float wMax = ImGui::CalcTextSize(maxStr.c_str()).x;
            float statW = (wMin > wMax ? wMin : wMax);
            float statX = p_max.x - 18.0f - statW;

            ImGui::SetCursorScreenPos(ImVec2(statX, contentY + 1.0f));
            ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", minStr.c_str());
            ImGui::SetCursorScreenPos(ImVec2(statX, contentY + 21.0f));
            ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", maxStr.c_str());
            ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();

    // --- Card 4: Capacity & Energy Statistics (电量累计, Ah / Wh) ---
    {
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + cardWidth, p_min.y + cardHeight);
        UIStyle::DrawCardShadow(p_min, p_max, 8.0f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, UIStyle::ColorBgCard());
        ImGui::PushStyleColor(ImGuiCol_Border, UIStyle::ColorBorder());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::BeginChild("CardEnergy", ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            renderCardHeader(drawList, p_min, p_max, "电量累计", "CAPACITY & ENERGY", UIStyle::ColorEnergy(), IM_COL32(79, 70, 229, 180));

            double ah = state.history.GetAccumulatedAh();
            double wh = state.history.GetAccumulatedWh();

            float contentX = p_min.x + 16.0f;
            float slot5W_med = g_FontDsegMedium->CalcTextSizeA(g_FontDsegMedium->LegacySize, FLT_MAX, 0.0f, "88888").x;
            float unitOffsetMed = slot5W_med + 8.0f;

            // Upper Line: Ah (DSEG Medium 5-slot + Fixed Unit)
            ImGui::SetCursorScreenPos(ImVec2(contentX, p_min.y + 45.0f));
            ImGui::PushFont(g_FontDsegMedium);
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.07f), "88888");
            ImGui::SetCursorScreenPos(ImVec2(contentX, p_min.y + 45.0f));
            std::string ahStr = Format5SlotDseg(ah);
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", ahStr.c_str());
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetMed, p_min.y + 46.0f));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorEnergy(), "Ah");
            ImGui::PopFont();

            // Lower Line: Wh (DSEG Medium 5-slot + Fixed Unit)
            ImGui::SetCursorScreenPos(ImVec2(contentX, p_min.y + 73.0f));
            ImGui::PushFont(g_FontDsegMedium);
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.07f), "88888");
            ImGui::SetCursorScreenPos(ImVec2(contentX, p_min.y + 73.0f));
            std::string whStr = Format5SlotDseg(wh);
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", whStr.c_str());
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetMed, p_min.y + 74.0f));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorEnergy(), "Wh");
            ImGui::PopFont();
        }

        // Double click anywhere on Card 4 to silently reset
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            state.history.ResetEnergy();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }
}

void Dashboard::RenderWaveform() {
    auto& state = AppState::Instance();
    float spacing = ImGui::GetStyle().ItemSpacing.x;

    // --- Waveform Toolbar ---
    {
        const char* pauseLabel = state.isPaused ? "继续采集" : "暂停采集";
        ImU32 pauseBg = state.isPaused ? IM_COL32(239, 249, 240, 255) : IM_COL32(255, 255, 255, 255);
        ImU32 pauseText = state.isPaused ? IM_COL32(15, 123, 15, 255) : IM_COL32(31, 31, 31, 255);
        ImU32 pauseBorder = state.isPaused ? IM_COL32(148, 211, 150, 255) : IM_COL32(224, 224, 224, 255);
        if (UIStyle::TactileButton(pauseLabel, ImVec2(80, 24), pauseBg, pauseText, pauseBorder, 4.0f)) {
            state.isPaused = !state.isPaused;
        }

        ImGui::SameLine();
        if (UIStyle::TactileButton("清空波形", ImVec2(80, 24), IM_COL32(255, 255, 255, 255), IM_COL32(31, 31, 31, 255), IM_COL32(224, 224, 224, 255), 4.0f)) {
            state.history.Clear();
        }

        ImGui::SameLine(0, 10.0f);
        ImVec2 twPos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(twPos.x, twPos.y + (24.0f - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextColored(UIStyle::ColorTextMuted(), "时间窗口:");
        ImGui::SameLine(0, 8.0f);
        // Modern Segmented Control: [ 30s | 1min | 2min | 5min | 10min | 30min | 1h ]
        struct TimeOpt { const char* label; float sec; float width; };
        static const TimeOpt s_timeOpts[] = {
            { "30s",   30.0f,   46.0f },
            { "1min",  60.0f,   50.0f },
            { "2min",  120.0f,  50.0f },
            { "5min",  300.0f,  50.0f },
            { "10min", 600.0f,  56.0f },
            { "30min", 1800.0f, 56.0f },
            { "1h",    3600.0f, 46.0f }
        };
        for (size_t k = 0; k < IM_ARRAYSIZE(s_timeOpts); ++k) {
            if (k > 0) ImGui::SameLine(0, 6.0f);
            if (UIStyle::SegmentButton(s_timeOpts[k].label, state.timeWindowSeconds == s_timeOpts[k].sec, s_timeOpts[k].width, 24.0f)) {
                state.timeWindowSeconds = s_timeOpts[k].sec;
            }
        }

        // Channel Radio Toggles: 电流, 功率, 电压 (No pill box, No units, Radio style)
        float totalRightW = 200.0f;
        float rightPos = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - totalRightW;
        if (rightPos > ImGui::GetCursorPosX()) {
            ImGui::SameLine(rightPos);
        }

        UIStyle::RadioToggle("电流", &state.showCurrent, ImGui::GetColorU32(UIStyle::ColorCurrent()));
        ImGui::SameLine(0, 16.0f);
        UIStyle::RadioToggle("功率", &state.showPower, ImGui::GetColorU32(UIStyle::ColorPower()));
        ImGui::SameLine(0, 16.0f);
        UIStyle::RadioToggle("电压", &state.showVoltage, ImGui::GetColorU32(UIStyle::ColorVoltage()));
    }

    // --- ImPlot Real-Time Waveform with Symmetrical Margins ---
    float plotHeight = 185.0f;
    ImPlotStyle& pstyle = ImPlot::GetStyle();
    pstyle.PlotPadding = ImVec2(10.0f, 10.0f);

    if (ImPlot::BeginPlot("##WaveformPlot", ImVec2(-1, plotHeight), ImPlotFlags_NoMouseText)) {
        // X-Axis: Remove bottom text, display clean relative seconds from -window to 0s
        double window = state.timeWindowSeconds;
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_X1, -window, 0.0, ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.0fs");

        ImPlotCond condY1 = state.resetY1 ? ImPlotCond_Always : ImPlotCond_Once;
        ImPlotCond condY2 = state.resetY2 ? ImPlotCond_Always : ImPlotCond_Once;
        ImPlotCond condY3 = state.resetY3 ? ImPlotCond_Always : ImPlotCond_Once;

        bool allThree = state.showCurrent && state.showPower && state.showVoltage;

        if (allThree) {
            // Y1 (Left): Current, Y2 (Right 1): Power, Y3 (Right 2): Voltage in mV (No English)
            ImPlot::SetupAxis(ImAxis_Y1, "电流 (mA)", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y2, "功率 (mW)", ImPlotAxisFlags_AuxDefault);
            ImPlot::SetupAxis(ImAxis_Y3, "电压 (mV)", ImPlotAxisFlags_AuxDefault);

            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 1000.0, condY1);
            ImPlot::SetupAxisLimits(ImAxis_Y2, -50.0, 3500.0, condY2);
            ImPlot::SetupAxisLimits(ImAxis_Y3, 0.0, 6000.0, condY3);
        } else if (state.showCurrent && state.showPower) {
            ImPlot::SetupAxis(ImAxis_Y1, "电流 (mA)", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y2, "功率 (mW)", ImPlotAxisFlags_AuxDefault);

            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 1000.0, condY1);
            ImPlot::SetupAxisLimits(ImAxis_Y2, -50.0, 3500.0, condY2);
        } else if (state.showCurrent && state.showVoltage) {
            ImPlot::SetupAxis(ImAxis_Y1, "电流 (mA)", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y2, "电压 (mV)", ImPlotAxisFlags_AuxDefault);

            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 1000.0, condY1);
            ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, 6000.0, condY2);
        } else if (state.showPower && state.showVoltage) {
            ImPlot::SetupAxis(ImAxis_Y1, "功率 (mW)", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y2, "电压 (mV)", ImPlotAxisFlags_AuxDefault);

            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 3500.0, condY1);
            ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, 6000.0, condY2);
        } else if (state.showCurrent) {
            ImPlot::SetupAxis(ImAxis_Y1, "电流 (mA)", ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 1000.0, condY1);
        } else if (state.showPower) {
            ImPlot::SetupAxis(ImAxis_Y1, "功率 (mW)", ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 3500.0, condY1);
        } else if (state.showVoltage) {
            ImPlot::SetupAxis(ImAxis_Y1, "电压 (mV)", ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 6000.0, condY1);
        }

        state.resetY1 = false;
        state.resetY2 = false;
        state.resetY3 = false;

        std::lock_guard<std::mutex> lock(state.history.GetMutex());
        const auto& t = state.history.GetTime();
        const auto& v = state.history.GetVoltage();
        const auto& i = state.history.GetCurrent();
        const auto& p = state.history.GetPower();

        if (!t.empty()) {
            int count = static_cast<int>(t.size());
            double curTime = state.GetElapsedSeconds();

            // Map absolute timestamps to [-window, 0.0] so newest is at 0.0s (right)
            m_relTime.resize(count);
            for (int k = 0; k < count; ++k) {
                m_relTime[k] = t[k] - curTime;
            }

            // 1. Current (Rich Emerald #059669)
            if (state.showCurrent) {
                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
                ImPlotSpec spec;
                spec.LineColor = UIStyle::ColorCurrent();
                spec.LineWeight = 2.0f;
                ImPlot::PlotLine("电流 (mA)", m_relTime.data(), i.data(), count, spec);
            }

            // 2. Power (Warm Amber #D97706)
            if (state.showPower) {
                ImAxis pAxis = allThree ? ImAxis_Y2 : (state.showCurrent ? ImAxis_Y2 : ImAxis_Y1);
                ImPlot::SetAxes(ImAxis_X1, pAxis);
                ImPlotSpec spec;
                spec.LineColor = UIStyle::ColorPower();
                spec.LineWeight = 1.8f;
                ImPlot::PlotLine("功率 (mW)", m_relTime.data(), p.data(), count, spec);
            }

            // 3. Voltage in mV (Azure Sky #0284C7)
            if (state.showVoltage) {
                ImAxis vAxis = allThree ? ImAxis_Y3 : ((state.showCurrent || state.showPower) ? ImAxis_Y2 : ImAxis_Y1);
                ImPlot::SetAxes(ImAxis_X1, vAxis);
                ImPlotSpec spec;
                spec.LineColor = UIStyle::ColorVoltage();
                spec.LineWeight = 1.8f;

                m_voltage_mv.resize(count);
                for (int k = 0; k < count; ++k) {
                    m_voltage_mv[k] = v[k] * 1000.0;
                }
                ImPlot::PlotLine("电压 (mV)", m_relTime.data(), m_voltage_mv.data(), count, spec);
            }
        }

        // Double-click Y axes or plot area to reset scale (safely queried inside BeginPlot/EndPlot)
        if (ImPlot::IsAxisHovered(ImAxis_Y1) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) state.resetY1 = true;
        if (ImPlot::IsAxisHovered(ImAxis_Y2) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) state.resetY2 = true;
        if (ImPlot::IsAxisHovered(ImAxis_Y3) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) state.resetY3 = true;
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            state.resetY1 = true; state.resetY2 = true; state.resetY3 = true;
        }

        ImPlot::EndPlot();
    }
}

void Dashboard::RenderBottomTabs() {
    static int s_initialTab = -1;
    if (s_initialTab == -1) {
        const char* envTab = getenv("CH570_SELECT_TAB");
        s_initialTab = envTab ? atoi(envTab) : 0;
    }

    if (ImGui::BeginTabBar("BottomTabBar", ImGuiTabBarFlags_None)) {
        ImGuiTabItemFlags f0 = (s_initialTab == 0) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        ImGuiTabItemFlags f1 = (s_initialTab == 1) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        ImGuiTabItemFlags f2 = (s_initialTab == 2) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        s_initialTab = -2; // only apply once on startup

        if (ImGui::BeginTabItem("  硬件遥控与交互 (Dongle & Probe Control)  ", nullptr, f0)) {
            RenderControlAndLogTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  数据串口穿透 (COM1 Target MCU)  ", nullptr, f1)) {
            RenderCom1TerminalTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  BLE 广播与协议诊断 (Beacon Diagnostics)  ", nullptr, f2)) {
            RenderBleDiagnosticsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void Dashboard::RenderControlAndLogTab() {
    auto& serial = SerialManager::Instance();
    auto& state = AppState::Instance();

    float totalW = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float leftColW = (totalW - spacing) * 0.47f;
    if (leftColW < 420.0f) leftColW = 420.0f;
    float rightColW = totalW - leftColW - spacing;

    // ==========================================
    // LEFT COLUMN: 上部命令下发 + 下部采样参数配置
    // ==========================================
    ImGui::BeginGroup();
    {
        // --- 1. 上部：Probe 硬件快捷控制命令 ---
        ImGui::TextColored(UIStyle::ColorTextMain(), "Probe 硬件快捷控制命令");

        float btnW = (leftColW - spacing) * 0.5f;
        float btnH = 28.0f;

        // Row 1: RST | BOOT
        if (UIStyle::TactileButton("复位MCU (CMD:RST)", ImVec2(btnW, btnH),
            IM_COL32(255, 240, 240, 255), IM_COL32(196, 43, 28, 255), IM_COL32(248, 215, 218, 255), 4.0f)) {
            serial.SendCommand("CMD:RST");
        }
        ImGui::SameLine();
        if (UIStyle::TactileButton("进入Boot (CMD:BOOT)", ImVec2(btnW, btnH),
            IM_COL32(255, 248, 236, 255), IM_COL32(178, 91, 0, 255), IM_COL32(253, 227, 181, 255), 4.0f)) {
            serial.SendCommand("CMD:BOOT");
        }

        // Row 2: SWAP | CFG?
        if (UIStyle::TactileButton("调转线序 (CMD:SWAP)", ImVec2(btnW, btnH),
            IM_COL32(235, 243, 252, 255), IM_COL32(0, 103, 192, 255), IM_COL32(158, 200, 240, 255), 4.0f)) {
            serial.SendCommand("CMD:SWAP");
        }
        ImGui::SameLine();
        if (UIStyle::TactileButton("查询配置 (CMD:CFG?)", ImVec2(btnW, btnH),
            IM_COL32(239, 249, 240, 255), IM_COL32(15, 123, 15, 255), IM_COL32(148, 211, 150, 255), 4.0f)) {
            serial.SendCommand("CMD:CFG?");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- 2. 下部：遥测与采样参数配置 (2x2 Grid) ---
        ImGui::TextColored(UIStyle::ColorTextMain(), "遥测与采样参数配置");

        static const char* const rateLabels[] = { "10 ms (100 Hz)", "20 ms (50 Hz)", "50 ms (20 Hz)", "100 ms (10 Hz)", "200 ms (5 Hz)", "500 ms (2 Hz)", "1000 ms (1 Hz)" };
        static const uint32_t rateValues[] = { 10, 20, 50, 100, 200, 500, 1000 };
        static int selectedRate = 2; // Default 50ms

        static const char* const fscLabels[] = { "100 mA", "500 mA", "1000 mA (默认)", "2000 mA", "3200 mA (最大)" };
        static const uint32_t fscValues[] = { 100, 500, 1000, 2000, 3200 };
        static int selectedFsc = 2; // Default 1000mA

        static const char* const avgLabels[] = { "1 次 (极速)", "4 次", "16 次 (默认推荐)", "64 次", "128 次", "256 次", "512 次", "1024 次 (极平滑)" };
        static const uint32_t avgValues[] = { 1, 4, 16, 64, 128, 256, 512, 1024 };
        static int selectedAvg = 2; // Default 16

        static const char* const shuntLabels[] = { "2 mΩ", "5 mΩ", "10 mΩ", "20 mΩ (默认标配)", "50 mΩ", "100 mΩ" };
        static const uint32_t shuntValues[] = { 2, 5, 10, 20, 50, 100 };
        static int selectedShunt = 3; // Default 20mR (matching hardware BOM R104 20mR 1% 3W)

        float comboItemW = 175.0f;
        float col2X = 290.0f;

        // Line 1: Rate | FSC (LED满幅)
        ImGui::AlignTextToFramePadding();
        ImGui::Text("采样周期:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##RateCombo", &selectedRate, rateLabels, IM_ARRAYSIZE(rateLabels))) {
            serial.SendCommand("CMD:RATE=" + std::to_string(rateValues[selectedRate]));
        }

        ImGui::SameLine(col2X);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("LED满幅:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##FscCombo", &selectedFsc, fscLabels, IM_ARRAYSIZE(fscLabels))) {
            serial.SendCommand("CMD:FSC=" + std::to_string(fscValues[selectedFsc]));
        }

        // Line 2: AVG (采样平滑) | Shunt (检流电阻)
        ImGui::AlignTextToFramePadding();
        ImGui::Text("采样平滑:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##AvgCombo", &selectedAvg, avgLabels, IM_ARRAYSIZE(avgLabels))) {
            serial.SendCommand("CMD:AVG=" + std::to_string(avgValues[selectedAvg]));
        }

        ImGui::SameLine(col2X);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("检流电阻:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##ShuntCombo", &selectedShunt, shuntLabels, IM_ARRAYSIZE(shuntLabels))) {
            serial.SendCommand("CMD:SHUNT=" + std::to_string(shuntValues[selectedShunt]));
        }

        ImGui::Spacing();

        // Line 3: Save button (Full width WinUI 3 Primary Accent Button)
        if (UIStyle::AccentButton("保存配置至 Flash (CMD:SAVE)", ImVec2(leftColW, 28), 4.0f)) {
            serial.SendCommand("CMD:SAVE");
        }
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    // ==========================================
    // RIGHT COLUMN: 交互日志与控制台终端
    // ==========================================
    ImGui::BeginGroup();
    {
        // Header line: Title + Right-aligned Clear Button
        ImGui::TextColored(UIStyle::ColorTextMain(), "Dongle 响应与控制交互日志");

        float clearBtnW = 65.0f;
        float clearBtnPos = ImGui::GetCursorPosX() + rightColW - clearBtnW;
        if (clearBtnPos > ImGui::GetCursorPosX()) {
            ImGui::SameLine(clearBtnPos);
        }
        if (UIStyle::TactileButton("清空日志", ImVec2(clearBtnW, 20),
            IM_COL32(255, 255, 255, 255), IM_COL32(93, 93, 93, 255), IM_COL32(224, 224, 224, 255), 4.0f)) {
            state.ClearControlLog();
        }

        // Log scrolling box (Pure white elevated container)
        float logBoxH = 142.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, UIStyle::ColorBgCard());
        ImGui::PushStyleColor(ImGuiCol_Border, UIStyle::ColorBorder());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        ImGui::BeginChild("ControlLogChild", ImVec2(0.0f, logBoxH), true);
        {
            std::lock_guard<std::mutex> lock(state.controlLogMutex);
            if (state.controlLog.empty()) {
                ImGui::TextColored(UIStyle::ColorTextMuted(), "暂无交互日志。发送控制指令或接收到遥测ACK后在此显示。");
            } else {
                for (const auto& line : state.controlLog) {
                    if (line.find("[TX ->") != std::string::npos) {
                        ImGui::TextColored(UIStyle::ColorPower(), "%s", line.c_str());
                    } else if (line.find("ACK") != std::string::npos || line.find("[CFG]") != std::string::npos) {
                        ImGui::TextColored(UIStyle::ColorCurrent(), "%s", line.c_str());
                    } else if (line.find("failed") != std::string::npos || line.find("Failed") != std::string::npos) {
                        ImGui::TextColored(UIStyle::ColorDanger(), "%s", line.c_str());
                    } else {
                        ImGui::TextColored(UIStyle::ColorTextMain(), "%s", line.c_str());
                    }
                }
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        // Custom command send input bar
        float sendBtnW = 65.0f;
        float availInputW = ImGui::GetContentRegionAvail().x - sendBtnW - spacing;
        if (availInputW < 100.0f) availInputW = 100.0f;
        ImGui::SetNextItemWidth(availInputW);
        bool enterPressed = ImGui::InputTextWithHint("##CustomCmd", "输入指令例如 CMD:RATE=100", m_customCmdBuffer, sizeof(m_customCmdBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((UIStyle::AccentButton("发送", ImVec2(sendBtnW, 24), 4.0f) || enterPressed)
            && m_customCmdBuffer[0] != '\0') {
            serial.SendCommand(m_customCmdBuffer);
            m_customCmdBuffer[0] = '\0';
        }
    }
    ImGui::EndGroup();
}

void Dashboard::RenderCom1TerminalTab() {
    auto& serial = SerialManager::Instance();
    auto& state = AppState::Instance();

    float totalW = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float leftCmdW = 168.0f;
    float rightTermW = totalW - leftCmdW - spacing;

    // ==========================================
    // LEFT COLUMN: 硬件快捷控制命令
    // ==========================================
    ImGui::BeginGroup();
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UIStyle::ColorTextMain(), "硬件快捷控制");
        ImGui::Spacing();

        float btnW = leftCmdW;
        float btnH = 27.0f;

        if (UIStyle::TactileButton("复位MCU (CMD:RST)", ImVec2(btnW, btnH),
            IM_COL32(255, 240, 240, 255), IM_COL32(196, 43, 28, 255), IM_COL32(248, 215, 218, 255), 4.0f)) {
            serial.SendCommand("CMD:RST");
        }
        ImGui::Spacing();
        if (UIStyle::TactileButton("进入Boot (CMD:BOOT)", ImVec2(btnW, btnH),
            IM_COL32(255, 248, 236, 255), IM_COL32(178, 91, 0, 255), IM_COL32(253, 227, 181, 255), 4.0f)) {
            serial.SendCommand("CMD:BOOT");
        }
        ImGui::Spacing();
        if (UIStyle::TactileButton("调转线序 (CMD:SWAP)", ImVec2(btnW, btnH),
            IM_COL32(235, 243, 252, 255), IM_COL32(0, 103, 192, 255), IM_COL32(158, 200, 240, 255), 4.0f)) {
            serial.SendCommand("CMD:SWAP");
        }
        ImGui::Spacing();
        if (UIStyle::TactileButton("查询配置 (CMD:CFG?)", ImVec2(btnW, btnH),
            IM_COL32(239, 249, 240, 255), IM_COL32(15, 123, 15, 255), IM_COL32(148, 211, 150, 255), 4.0f)) {
            serial.SendCommand("CMD:CFG?");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(UIStyle::ColorTextMuted(), "指令经由COM2无线");
        ImGui::TextColored(UIStyle::ColorTextMuted(), "遥控下发至探头。");
    }
    ImGui::EndGroup();

    // Vertical Divider Line between Hardware Control and Serial Console
    ImGui::SameLine(0, 12.0f);
    ImVec2 vPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(vPos.x + 4.0f, vPos.y + 4.0f), ImVec2(vPos.x + 4.0f, vPos.y + 195.0f), IM_COL32(220, 220, 220, 255), 1.0f);
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine(0, 6.0f);

    // ==========================================
    // RIGHT COLUMN: 目标板串口透传与终端
    // ==========================================
    ImGui::BeginGroup();
    {
        // Top Port Selection Toolbar
        static std::vector<SerialPortInfo> portList;
        static int selectedPortIdx = 0;
        static bool portListInitialized = false;

        float toolbarH = ImGui::GetFrameHeight();
        ImGui::AlignTextToFramePadding();

        if (!portListInitialized || UIStyle::TactileButton("刷新串口列表", ImVec2(96, toolbarH),
            IM_COL32(255, 255, 255, 255), IM_COL32(31, 31, 31, 255), IM_COL32(224, 224, 224, 255), 4.0f)) {
            portList = serial.EnumeratePorts();
            portListInitialized = true;
            for (size_t i = 0; i < portList.size(); ++i) {
                if (portList[i].isDongleCOM1 || portList[i].portName == state.com1PortName) {
                    selectedPortIdx = static_cast<int>(i);
                    break;
                }
            }
        }

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("串口:");
        ImGui::SameLine();
        ImGui::PushItemWidth(160.0f);
        std::string preview = (!portList.empty() && selectedPortIdx < portList.size())
            ? (portList[selectedPortIdx].portName + " (" + portList[selectedPortIdx].friendlyName + ")")
            : "未找到串口";
        if (ImGui::BeginCombo("##ComPortCombo", preview.c_str())) {
            for (int i = 0; i < (int)portList.size(); ++i) {
                bool isSelected = (selectedPortIdx == i);
                std::string label = portList[i].portName + " - " + portList[i].friendlyName;
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedPortIdx = i;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("波特率:");
        ImGui::SameLine();
        static const uint32_t bauds[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 2000000 };
        static const char* const baudStrings[] = { "9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "1000000", "2000000" };
        static int selectedBaudIdx = 4; // 115200

        ImGui::PushItemWidth(108.0f);
        ImGui::Combo("##BaudCombo", &selectedBaudIdx, baudStrings, IM_ARRAYSIZE(baudStrings));
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (state.com1Open.load()) {
            if (UIStyle::TactileButton("关闭串口", ImVec2(78, toolbarH),
                IM_COL32(255, 240, 240, 255), IM_COL32(196, 43, 28, 255), IM_COL32(248, 215, 218, 255), 4.0f)) {
                serial.CloseCOM1();
            }
        } else {
            if (UIStyle::AccentButton("打开串口", ImVec2(78, toolbarH), 4.0f)) {
                if (!portList.empty() && selectedPortIdx < portList.size()) {
                    serial.OpenCOM1(portList[selectedPortIdx].portName, bauds[selectedBaudIdx]);
                }
            }
        }

        ImGui::SameLine();
        ImGui::Checkbox("HEX", &state.com1HexMode);
        ImGui::SameLine();
        ImGui::Checkbox("自动滚屏", &state.com1AutoScroll);
        ImGui::SameLine();
        if (UIStyle::TactileButton("清空", ImVec2(48, toolbarH),
            IM_COL32(255, 255, 255, 255), IM_COL32(93, 93, 93, 255), IM_COL32(224, 224, 224, 255), 4.0f)) {
            state.ClearCom1Rx();
        }

        char rxTxText[64];
        snprintf(rxTxText, sizeof(rxTxText), "RX:%lluB | TX:%lluB",
                 (unsigned long long)state.com1RxBytes.load(),
                 (unsigned long long)state.com1TxBytes.load());
        float textW = ImGui::CalcTextSize(rxTxText).x;
        float rxX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textW;
        if (rxX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(rxX);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UIStyle::ColorTextMuted(), "%s", rxTxText);

        ImGui::Spacing();

        // RX Output Terminal Area (Pure white card background)
        ImGui::PushStyleColor(ImGuiCol_ChildBg, UIStyle::ColorBgCard());
        ImGui::PushStyleColor(ImGuiCol_Border, UIStyle::ColorBorder());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        ImGui::BeginChild("Com1TerminalOutput", ImVec2(0.0f, 116.0f), true);
        {
            std::lock_guard<std::mutex> lock(state.com1RxMutex);
            if (state.com1RxBuffer.empty()) {
                ImGui::TextColored(UIStyle::ColorTextMuted(), "暂无接收数据。打开串口后即可与目标MCU双向高速透传。");
            } else {
                if (state.com1HexMode) {
                    char hexBuf[4];
                    std::string line = "";
                    for (size_t i = 0; i < state.com1RxBuffer.size(); ++i) {
                        snprintf(hexBuf, sizeof(hexBuf), "%02X ", state.com1RxBuffer[i]);
                        line += hexBuf;
                        if ((i + 1) % 16 == 0 || i + 1 == state.com1RxBuffer.size()) {
                            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", line.c_str());
                            line.clear();
                        }
                    }
                } else {
                    std::string text(state.com1RxBuffer.begin(), state.com1RxBuffer.end());
                    ImGui::TextColored(UIStyle::ColorTextMain(), "%s", text.c_str());
                }
            }

            if (state.com1AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        // TX Input Bar
        float sendBtnW = 76.0f;
        float eolComboW = 72.0f;
        float availInputW = ImGui::GetContentRegionAvail().x - sendBtnW - eolComboW - spacing * 2.0f;
        if (availInputW < 100.0f) availInputW = 100.0f;

        ImGui::PushItemWidth(availInputW);
        bool enterSend = ImGui::InputTextWithHint("##TxDataInput", "输入待发送数据", m_com1SendBuffer, sizeof(m_com1SendBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        const char* const eolLabels[] = { "无换行", "\\r\\n", "\\n" };
        ImGui::PushItemWidth(eolComboW);
        ImGui::Combo("##EolCombo", &m_com1NewlineIndex, eolLabels, IM_ARRAYSIZE(eolLabels));
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if ((UIStyle::AccentButton("发送数据", ImVec2(sendBtnW, toolbarH), 4.0f) || enterSend)
            && m_com1SendBuffer[0] != '\0') {
            std::string toSend = m_com1SendBuffer;
            if (m_com1NewlineIndex == 1) toSend += "\r\n";
            else if (m_com1NewlineIndex == 2) toSend += "\n";
            serial.SendCOM1String(toSend);
            m_com1SendBuffer[0] = '\0';
        }
    }
    ImGui::EndGroup();
}

void Dashboard::RenderBleDiagnosticsTab() {
    auto& state = AppState::Instance();

    ImGui::TextColored(UIStyle::ColorVoltage(), "Probe 蓝牙未配对广播机制 (ADV_NONCONN_IND)");
    ImGui::BulletText("目标 MAC 地址: %s", state.bleDeviceMac.c_str());
    ImGui::BulletText("服务 UUID: 0xFCD2 (16-bit Service Data 广播格式)");
    ImGui::BulletText("广播包总负载: 11 字节 (Flags 3B + Service Data 8B)");
    ImGui::BulletText("实时信号强度 (RSSI): %d dBm", (int)state.bleLastRssi.load());
    ImGui::BulletText("累计捕获广播包数: %llu", (unsigned long long)state.blePacketCount.load());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(UIStyle::ColorCurrent(), "INA226 满分辨率高精度遥测设计指标");
    ImGui::BulletText("分流电压分辨率: 1 LSB = 2.5 uV (搭配 20 mΩ 采样电阻即 1 LSB = 0.125 mA = 125 uA)");
    ImGui::BulletText("母线电压分辨率: 1 LSB = 1.25 mV");
    ImGui::BulletText("功率由上位机高精度浮点计算: P = V_bus * I_shunt (保留 2 位小数精确至 0.01 mW)");
    ImGui::BulletText("极低射频开销与功耗: 报文仅传输 4 字节原始 ADC 码值 (shunt_raw 2B + bus_raw 2B)");
}

} // namespace CH570App
