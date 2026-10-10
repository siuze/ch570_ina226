#include "dashboard.hpp"
#include "style.hpp"
#include "imgui.h"
#include "implot.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>

#pragma comment(lib, "comdlg32.lib")

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

static std::string Format5SlotDseg(double val, int decimalsUnder100 = 2, int slotCount = 5) {
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
    } else if (absVal < 100.0 && decimalsUnder100 >= 3) {
        snprintf(numBuf, sizeof(numBuf), "%.3f", absVal);
    } else {
        snprintf(numBuf, sizeof(numBuf), "%.2f", absVal);
    }

    int digitCount = 0;
    for (int i = 0; numBuf[i] != '\0'; ++i) {
        if (numBuf[i] != '.') digitCount++;
    }
    if (neg) digitCount++;

    int padCount = slotCount - digitCount;
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

static double MedianRecent(const std::array<TelemetryPoint, 3>& samples,
                           size_t count, double TelemetryPoint::*member) {
    if (count == 0) return 0.0;
    double values[3]{};
    for (size_t i = 0; i < count; ++i) values[i] = samples[i].*member;
    std::sort(values, values + count);
    return values[count / 2];
}

static bool PickHexFile(std::string& outPath) {
    wchar_t path[MAX_PATH * 4]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = path;
    ofn.nMaxFile = static_cast<DWORD>(std::size(path));
    ofn.lpstrFilter = L"Intel HEX (*.hex)\0*.hex\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return false;
    outPath.resize(static_cast<size_t>(n - 1));
    WideCharToMultiByte(CP_UTF8, 0, path, -1, outPath.data(), n, nullptr, nullptr);
    return true;
}

Dashboard::Dashboard(BleScanner& bleScanner)
    : m_bleScanner(bleScanner) {
    memset(m_com1SendBuffer, 0, sizeof(m_com1SendBuffer));
    memset(m_customCmdBuffer, 0, sizeof(m_customCmdBuffer));

    // Default time window to 30s as requested by user
    AppState::Instance().timeWindowSeconds = 30.0f;
    m_relTime.reserve(12000);
    m_current_plot.reserve(12000);
    m_power_plot.reserve(12000);
    m_voltage_mv.reserve(12000);
}

Dashboard::~Dashboard() {
    CloseCsv();
}

void Dashboard::CloseCsv() {
    if (m_csvFile.is_open()) m_csvFile.close();
    m_csvPath.clear();
    m_csvHourKey.clear();
}

bool Dashboard::OpenCsvForCurrentHour(const std::tm& localTm, long long epochMs) {
    char hourKey[32]{};
    std::strftime(hourKey, sizeof(hourKey), "%Y%m%d%H", &localTm);
    if (m_csvFile.is_open() && m_csvHourKey == hourKey) return true;

    CloseCsv();
    char fileStamp[32]{};
    std::strftime(fileStamp, sizeof(fileStamp), "%Y%m%d%H%M%S", &localTm);
    char exePath[MAX_PATH * 4]{};
    DWORD len = GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    std::filesystem::path base = (len > 0) ? std::filesystem::path(exePath).parent_path()
                                           : std::filesystem::current_path();
    std::error_code ec;
    std::filesystem::create_directories(base / "csv", ec);
    std::filesystem::path path = base / "csv" / (std::string("record_") + fileStamp + ".csv");
    m_csvFile.open(path, std::ios::out | std::ios::app);
    if (!m_csvFile.is_open()) return false;
    m_csvHourKey = hourKey;
    m_csvPath = path.string();
    if (m_csvFile.tellp() == std::streampos(0)) {
        m_csvFile << "timestamp_ms,time,current_mA,voltage_V,power_mW,capacity_mAh,energy_mWh\n";
    }
    (void)epochMs;
    return true;
}

void Dashboard::ExportLatestCsvIfDue() {
    static const double intervals[] = {0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0};
    if (!m_csvEnabled) {
        if (m_csvFile.is_open()) CloseCsv();
        return;
    }

    TelemetryPoint pt{};
    double ah = 0.0, wh = 0.0;
    if (!AppState::Instance().history.GetLatest(pt, ah, wh)) return;
    if (m_csvLastSampleTimestamp >= 0.0 &&
        pt.timestamp - m_csvLastSampleTimestamp + 1e-9 < intervals[m_csvIntervalIndex]) return;

    const auto now = std::chrono::system_clock::now();
    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
    localtime_s(&localTm, &tt);
    if (!OpenCsvForCurrentHour(localTm, epochMs)) return;

    char timeBuf[40]{};
    auto ms = epochMs % 1000;
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &localTm);
    m_csvFile << epochMs << "," << timeBuf << "." << std::setfill('0') << std::setw(3) << ms
              << "," << std::fixed << std::setprecision(3) << pt.current_ma
              << "," << std::setprecision(6) << pt.voltage_v
              << "," << std::setprecision(3) << pt.power_mw
              << "," << std::setprecision(6) << ah * 1000.0
              << "," << std::setprecision(6) << wh * 1000.0 << "\n";
    m_csvFile.flush();
    m_csvLastSampleTimestamp = pt.timestamp;
}

void Dashboard::Render() {
    SerialManager::Instance().ServiceAutoReconnect();
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

    // Keep firmware versions and project attribution in a compact footer.
    {
        auto& state = AppState::Instance();
        std::string dongleVer, probeVer;
        state.GetFwVersions(dongleVer, probeVer);
        if (dongleVer.empty()) dongleVer = "未知";
        if (probeVer.empty()) probeVer = "未知";
        const std::string footer = "Dongle " + dongleVer + " | Probe " + probeVer +
                                   " | 上位机 v2.0.8 | Design by IEKSIUZE";
        const float footerSize = 15.0f;
        const ImVec2 footerSizePx = g_FontDefault->CalcTextSizeA(footerSize, FLT_MAX, 0.0f, footer.c_str());
        ImGui::GetForegroundDrawList()->AddText(g_FontDefault, footerSize,
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - footerSizePx.x - 12.0f,
                   viewport->WorkPos.y + viewport->WorkSize.y - 22.0f),
            IM_COL32(120, 120, 120, 220), footer.c_str());
    }

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
        if (UIStyle::TactileButton("停止", ImVec2(68, btnH), IM_COL32(255, 240, 240, 255), IM_COL32(196, 43, 28, 255), IM_COL32(248, 215, 218, 255), 4.0f)) {
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
        if (UIStyle::TactileButton("启动", ImVec2(68, btnH), IM_COL32(239, 249, 240, 255), IM_COL32(15, 123, 15, 255), IM_COL32(196, 232, 197, 255), 4.0f)) {
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
    const double nowElapsed = state.GetElapsedSeconds();
    const bool probeConnected = state.dongleConnected.load() &&
        (nowElapsed - state.dongleLastTimestamp) < 3.0;
    const bool probeRssiFresh = state.dongleRssiValid.load() &&
        (nowElapsed - state.dongleLastRssiTimestamp) < 5.0 && state.dongleRssi.load() < 0;
    if (probeConnected) {
        if (probeRssiFresh) {
            snprintf(dongleStatsBuf, sizeof(dongleStatsBuf), "| Probe 信号: %d dBm | 遥测: %llu 帧",
                     state.dongleRssi.load(),
                     (unsigned long long)state.donglePacketCount.load());
        } else {
            snprintf(dongleStatsBuf, sizeof(dongleStatsBuf), "| Probe 已连接 | 信号等待 | 遥测: %llu 帧",
                     (unsigned long long)state.donglePacketCount.load());
        }
    } else {
        snprintf(dongleStatsBuf, sizeof(dongleStatsBuf), "| Probe 未连接 | 遥测: %llu 帧",
                 (unsigned long long)state.donglePacketCount.load());
    }
    ImGui::SetCursorScreenPos(ImVec2(curX, textY));
    ImGui::TextColored(probeConnected ? UIStyle::ColorTextMuted() : UIStyle::ColorDanger(), "%s", dongleStatsBuf);
    curX += ImGui::CalcTextSize(dongleStatsBuf).x + 16.0f;

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

    // Display-only filtering: three-sample median rejects one-packet spikes,
    // then an EMA advances from telemetry timestamps (tau ~= 0.8 s).  This
    // keeps rendering cadence from changing the numeric response.
    std::array<TelemetryPoint, 3> recent{};
    const size_t recentCount = state.history.GetRecent(recent);
    if (recentCount > 0) {
        const double newestTs = recent[recentCount - 1].timestamp;
        if (newestTs != m_filterLastSampleTimestamp) {
            const double medianCurrent = MedianRecent(recent, recentCount, &TelemetryPoint::current_ma);
            const double medianPower = MedianRecent(recent, recentCount, &TelemetryPoint::power_mw);
            const double medianVoltage = MedianRecent(recent, recentCount, &TelemetryPoint::voltage_v);
            if (!m_smoothInitialized || newestTs < m_filterLastSampleTimestamp) {
                m_smoothCurrent = medianCurrent;
                m_smoothPower = medianPower;
                m_smoothVoltage = medianVoltage;
                m_smoothInitialized = true;
            } else {
                const double dt = (std::clamp)(newestTs - m_filterLastSampleTimestamp, 0.01, 2.0);
                const double alpha = 1.0 - std::exp(-dt / 0.8);
                m_smoothCurrent += (medianCurrent - m_smoothCurrent) * alpha;
                m_smoothPower += (medianPower - m_smoothPower) * alpha;
                m_smoothVoltage += (medianVoltage - m_smoothVoltage) * alpha;
            }
            m_filterLastSampleTimestamp = newestTs;
        }
    }

    // Unit hysteresis prevents a threshold crossing from changing units back
    // and forth. Enter at the requested value, leave with a 10% margin.
    const double currentAbs = std::abs(m_smoothCurrent);
    const double powerAbs = std::abs(m_smoothPower);
    const double voltageAbs = std::abs(m_smoothVoltage);
    if (m_currentInA ? currentAbs < 1350.0 : currentAbs >= 1500.0) m_currentInA = !m_currentInA;
    if (m_powerInW ? powerAbs < 850.0 : powerAbs >= 1000.0) m_powerInW = !m_powerInW;
    if (m_voltageInV ? voltageAbs < 1.8 : voltageAbs >= 2.0) m_voltageInV = !m_voltageInV;
    const bool currentInA = m_currentInA;
    const bool powerInW = m_powerInW;
    const bool voltageInV = m_voltageInV;
    const double currentScale = currentInA ? 0.001 : 1.0;
    const double powerScale = powerInW ? 0.001 : 1.0;
    const double voltageScale = voltageInV ? 1.0 : 1000.0;
    const char* currentUnit = currentInA ? "A" : "mA";
    const char* powerUnit = powerInW ? "W" : "mW";
    const char* voltageUnit = voltageInV ? "V" : "mV";

    float availWidth = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    // 4 cards layout: 1. Current, 2. Power, 3. Voltage (mV), 4. Capacity & Energy (mAh / mWh)
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
            std::string dsegStr = Format5SlotDseg(m_smoothCurrent * currentScale, 3);
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", dsegStr.c_str());
            ImGui::PopFont();

            // 100% Fixed Unit Position (never jumps)
            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetLarge, contentY + baselineOffset));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorCurrent(), "%s", currentUnit);
            ImGui::PopFont();

            // Right-aligned gray rolling min/max stats with guaranteed right margin & bottom clearance
            std::string minStr = "min: " + FormatMaxDigits(iStats.min_val * currentScale, 4);
            std::string maxStr = "max: " + FormatMaxDigits(iStats.max_val * currentScale, 4);

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
            std::string dsegStr = Format5SlotDseg(m_smoothPower * powerScale, powerInW ? 3 : 2);
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", dsegStr.c_str());
            ImGui::PopFont();

            // 100% Fixed Unit Position (never jumps)
            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetLarge, contentY + baselineOffset));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorPower(), "%s", powerUnit);
            ImGui::PopFont();

            // Right-aligned gray rolling min/max stats with guaranteed right margin & bottom clearance
            std::string minStr = "min: " + FormatMaxDigits(pStats.min_val * powerScale, 4);
            std::string maxStr = "max: " + FormatMaxDigits(pStats.max_val * powerScale, 4);

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

            double mv_cur = m_smoothVoltage * voltageScale;
            double mv_min = vStats.min_val * voltageScale;
            double mv_max = vStats.max_val * voltageScale;

            // Faint 7-segment unlit LCD background
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::PushFont(g_FontDseg);
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.07f), "88888");

            // Active 5-slot 7-segment numerical value
            std::string dsegStr = Format5SlotDseg(mv_cur, voltageInV ? 3 : 2);
            ImGui::SetCursorScreenPos(ImVec2(contentX, contentY));
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", dsegStr.c_str());
            ImGui::PopFont();

            // 100% Fixed Unit Position (never jumps)
            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetLarge, contentY + baselineOffset));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorVoltage(), "%s", voltageUnit);
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

    // --- Card 4: Capacity & Energy Statistics (电量累计, mAh / mWh) ---
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

            double ah = state.history.GetAccumulatedAh() * 1000.0;
            double wh = state.history.GetAccumulatedWh() * 1000.0;

            float contentX = p_min.x + 16.0f;
            float slot5W_med = g_FontDsegMedium->CalcTextSizeA(g_FontDsegMedium->LegacySize, FLT_MAX, 0.0f, "888888").x;
            float unitOffsetMed = slot5W_med + 8.0f;

            // Upper Line: mAh (DSEG Medium 5-slot + Fixed Unit)
            ImGui::SetCursorScreenPos(ImVec2(contentX, p_min.y + 45.0f));
            ImGui::PushFont(g_FontDsegMedium);
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.07f), "888888");
            ImGui::SetCursorScreenPos(ImVec2(contentX, p_min.y + 45.0f));
            std::string ahStr = Format5SlotDseg(ah, 3, 6);
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", ahStr.c_str());
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetMed, p_min.y + 46.0f));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorEnergy(), "mAh");
            ImGui::PopFont();

            // Lower Line: mWh (DSEG Medium 5-slot + Fixed Unit)
            ImGui::SetCursorScreenPos(ImVec2(contentX, p_min.y + 73.0f));
            ImGui::PushFont(g_FontDsegMedium);
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.07f), "888888");
            ImGui::SetCursorScreenPos(ImVec2(contentX, p_min.y + 73.0f));
            std::string whStr = Format5SlotDseg(wh, 3, 6);
            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", whStr.c_str());
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(contentX + unitOffsetMed, p_min.y + 74.0f));
            ImGui::PushFont(g_FontOswaldMedium);
            ImGui::TextColored(UIStyle::ColorEnergy(), "mWh");
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
    ExportLatestCsvIfDue();

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
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UIStyle::ColorTextMuted(), "时间窗口:");
        ImGui::SameLine(0, 8.0f);
        // Keep the control compact while covering short and long measurements.
        struct TimeOpt { const char* label; float sec; float width; };
        static const TimeOpt s_timeOpts[] = {
            { "30s",   30.0f,   46.0f },
            { "1min",  60.0f,   50.0f },
            { "2min",  120.0f,  50.0f },
            { "5min",  300.0f,  50.0f },
            { "10min", 600.0f,  56.0f },
            { "30min", 1800.0f, 56.0f },
            { "1h",    3600.0f, 46.0f },
            { "2h",    7200.0f, 46.0f },
            { "4h",    14400.0f, 46.0f }
        };
        for (size_t k = 0; k < IM_ARRAYSIZE(s_timeOpts); ++k) {
            if (k > 0) ImGui::SameLine(0, 6.0f);
            if (UIStyle::SegmentButton(s_timeOpts[k].label, state.timeWindowSeconds == s_timeOpts[k].sec, s_timeOpts[k].width, 24.0f)) {
                state.timeWindowSeconds = s_timeOpts[k].sec;
            }
        }

        ImGui::SameLine(0, 18.0f);
        const bool csvWasEnabled = m_csvEnabled;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 1.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::Checkbox("导出CSV", &m_csvEnabled);
        ImGui::PopStyleVar();
        if (m_csvEnabled != csvWasEnabled) {
            m_csvLastSampleTimestamp = -1.0;
            if (!m_csvEnabled) CloseCsv();
        }
        ImGui::SameLine(0, 6.0f);
        static const char* csvIntervals[] = { "0.1s", "0.5s", "1s", "2s", "5s", "10s", "30s", "60s" };
        ImGui::SetNextItemWidth(82.0f);
        ImGui::Combo("##CsvInterval", &m_csvIntervalIndex, csvIntervals, IM_ARRAYSIZE(csvIntervals));

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

    MetricStats chartV, chartI, chartP;
    state.history.GetWindowStats(state.timeWindowSeconds, chartV, chartI, chartP);
    const double voltageMax = (std::max)(1000.0, chartV.count > 0 ? chartV.max_val * 1000.0 * 1.15 : 6000.0);

    if (ImPlot::BeginPlot("##WaveformPlot", ImVec2(-1, plotHeight), ImPlotFlags_NoMouseText | ImPlotFlags_NoLegend)) {
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
            ImPlot::SetupAxisLimits(ImAxis_Y3, 0.0, voltageMax, condY3);
        } else if (state.showCurrent && state.showPower) {
            ImPlot::SetupAxis(ImAxis_Y1, "电流 (mA)", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y2, "功率 (mW)", ImPlotAxisFlags_AuxDefault);

            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 1000.0, condY1);
            ImPlot::SetupAxisLimits(ImAxis_Y2, -50.0, 3500.0, condY2);
        } else if (state.showCurrent && state.showVoltage) {
            ImPlot::SetupAxis(ImAxis_Y1, "电流 (mA)", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y2, "电压 (mV)", ImPlotAxisFlags_AuxDefault);

            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 1000.0, condY1);
            ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, voltageMax, condY2);
        } else if (state.showPower && state.showVoltage) {
            ImPlot::SetupAxis(ImAxis_Y1, "功率 (mW)", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y2, "电压 (mV)", ImPlotAxisFlags_AuxDefault);

            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 3500.0, condY1);
            ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, voltageMax, condY2);
        } else if (state.showCurrent) {
            ImPlot::SetupAxis(ImAxis_Y1, "电流 (mA)", ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 1000.0, condY1);
        } else if (state.showPower) {
            ImPlot::SetupAxis(ImAxis_Y1, "功率 (mW)", ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -50.0, 3500.0, condY1);
        } else if (state.showVoltage) {
            ImPlot::SetupAxis(ImAxis_Y1, "电压 (mV)", ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, voltageMax, condY1);
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
            /* Draw at most 12k points.  The stored samples and CSV remain
             * lossless; this only bounds ImPlot work for multi-hour views. */
            double curTime = state.GetElapsedSeconds();
            const double cutoff = curTime - window;
            size_t first = 0;
            while (first < t.size() && t[first] < cutoff) ++first;
            if (first >= t.size()) first = t.size() - 1;
            const size_t visible = t.size() - first;
            const size_t stride = (visible + 11999u) / 12000u;
            const size_t countSize = (visible + stride - 1u) / stride;
            const int count = static_cast<int>(countSize);

            m_relTime.resize(countSize);
            m_current_plot.resize(countSize);
            m_power_plot.resize(countSize);
            m_voltage_mv.resize(countSize);
            for (size_t k = 0; k < countSize; ++k) {
                const size_t source = (std::min)(first + k * stride, t.size() - 1u);
                m_relTime[k] = t[source] - curTime;
                m_current_plot[k] = i[source];
                m_power_plot[k] = p[source];
                m_voltage_mv[k] = v[source] * 1000.0;
            }

            // 1. Current (Rich Emerald #059669)
            if (state.showCurrent) {
                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
                ImPlotSpec spec;
                spec.LineColor = UIStyle::ColorCurrent();
                spec.LineWeight = 2.0f;
                ImPlot::PlotLine("电流 (mA)", m_relTime.data(), m_current_plot.data(), count, spec);
            }

            // 2. Power (Warm Amber #D97706)
            if (state.showPower) {
                ImAxis pAxis = allThree ? ImAxis_Y2 : (state.showCurrent ? ImAxis_Y2 : ImAxis_Y1);
                ImPlot::SetAxes(ImAxis_X1, pAxis);
                ImPlotSpec spec;
                spec.LineColor = UIStyle::ColorPower();
                spec.LineWeight = 1.8f;
                ImPlot::PlotLine("功率 (mW)", m_relTime.data(), m_power_plot.data(), count, spec);
            }

            // 3. Voltage in mV (Azure Sky #0284C7)
            if (state.showVoltage) {
                ImAxis vAxis = allThree ? ImAxis_Y3 : ((state.showCurrent || state.showPower) ? ImAxis_Y2 : ImAxis_Y1);
                ImPlot::SetAxes(ImAxis_X1, vAxis);
                ImPlotSpec spec;
                spec.LineColor = UIStyle::ColorVoltage();
                spec.LineWeight = 1.8f;

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
    if (ImGui::BeginTabBar("BottomTabBar", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("  硬件遥控与交互 (Dongle & Probe Control)  ")) {
            RenderControlAndLogTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  数据串口穿透 (COM1 Target MCU)  ")) {
            RenderCom1TerminalTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  链路功能诊断 (Link Diagnostics)  ")) {
            RenderBleDiagnosticsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  固件维护 (Firmware)  ")) {
            RenderFirmwareTab();
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
        static int selectedRate = 3; // Default 100ms (10 Hz)

        static const char* const fscLabels[] = { "100 mA", "500 mA", "1000 mA", "2000 mA", "3200 mA", "5000 mA", "10000 mA", "20000 mA (20A)" };
        static const uint32_t fscValues[] = { 100, 500, 1000, 2000, 3200, 5000, 10000, 20000 };
        static int selectedFsc = 7; // Default 20A to keep the current LED comfortable

        static const char* const linkLedLabels[] = { "10% (最低)", "25%", "40%", "60%", "80%", "100%" };
        static const uint32_t linkLedValues[] = { 26, 64, 102, 153, 204, 255 };
        static int selectedLinkLed = 5;

        static const char* const avgLabels[] = { "1 次 (极速)", "4 次", "16 次", "64 次 (默认推荐)", "128 次", "256 次", "512 次", "1024 次 (极平滑)" };
        static const uint32_t avgValues[] = { 1, 4, 16, 64, 128, 256, 512, 1024 };
        static int selectedAvg = 3; // Default 64

        static const char* const shuntLabels[] = { "2 mΩ", "5 mΩ", "10 mΩ", "20 mΩ (默认标配)", "50 mΩ", "100 mΩ" };
        static const uint32_t shuntValues[] = { 2, 5, 10, 20, 50, 100 };
        static int selectedShunt = 3; // Default 20mR (matching hardware BOM R104 20mR 1% 3W)

        static const char* const bleLabels[] = { "1 次/s", "2 次/s", "4 次/s (默认)", "5 次/s", "10 次/s", "20 次/s" };
        static const uint32_t bleValues[] = { 1, 2, 4, 5, 10, 20 };
        static int selectedBle = 2;

        static uint64_t appliedCfgRevision = 0;
        const uint64_t cfgRevision = state.dongleCfgRevision.load();
        if (state.dongleCfg.config_loaded && cfgRevision != appliedCfgRevision) {
            DongleConfig cfg;
            {
                std::lock_guard<std::mutex> lock(state.dongleCfgMutex);
                cfg = state.dongleCfg;
            }
            auto closest = [](const uint32_t* values, int count, uint32_t actual) {
                int best = 0;
                uint32_t distance = (actual > values[0]) ? actual - values[0] : values[0] - actual;
                for (int i = 1; i < count; ++i) {
                    uint32_t d = (actual > values[i]) ? actual - values[i] : values[i] - actual;
                    if (d < distance) { best = i; distance = d; }
                }
                return best;
            };
            selectedRate = closest(rateValues, IM_ARRAYSIZE(rateValues), cfg.sampling_rate_ms);
            selectedFsc = closest(fscValues, IM_ARRAYSIZE(fscValues), cfg.full_scale_ma);
            selectedLinkLed = closest(linkLedValues, IM_ARRAYSIZE(linkLedValues), cfg.link_led_max_duty);
            selectedAvg = closest(avgValues, IM_ARRAYSIZE(avgValues), cfg.averaging_count);
            selectedShunt = closest(shuntValues, IM_ARRAYSIZE(shuntValues), cfg.shunt_mohm);
            selectedBle = closest(bleValues, IM_ARRAYSIZE(bleValues), cfg.ble_adv_hz);
            appliedCfgRevision = cfgRevision;
        }

        float comboItemW = 175.0f;
        float col2X = 290.0f;
        float labelW = 96.0f;
        float leftComboX = labelW;
        float rightComboX = col2X + labelW;

        // Line 1: Rate | FSC (电流灯满亮)
        ImGui::AlignTextToFramePadding();
        ImGui::Text("采样周期:");
        ImGui::SameLine(leftComboX);
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##RateCombo", &selectedRate, rateLabels, IM_ARRAYSIZE(rateLabels))) {
            serial.SendCommand("CMD:RATE=" + std::to_string(rateValues[selectedRate]));
        }

        ImGui::SameLine(col2X);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("电流灯满亮:");
        ImGui::SameLine(rightComboX);
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##FscCombo", &selectedFsc, fscLabels, IM_ARRAYSIZE(fscLabels))) {
            serial.SendCommand("CMD:FSC=" + std::to_string(fscValues[selectedFsc]));
        }

        // Line 2: AVG (采样平滑) | Shunt (检流电阻)
        ImGui::AlignTextToFramePadding();
        ImGui::Text("采样平滑:");
        ImGui::SameLine(leftComboX);
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##AvgCombo", &selectedAvg, avgLabels, IM_ARRAYSIZE(avgLabels))) {
            serial.SendCommand("CMD:AVG=" + std::to_string(avgValues[selectedAvg]));
        }

        ImGui::SameLine(col2X);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("检流电阻:");
        ImGui::SameLine(rightComboX);
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##ShuntCombo", &selectedShunt, shuntLabels, IM_ARRAYSIZE(shuntLabels))) {
            serial.SendCommand("CMD:SHUNT=" + std::to_string(shuntValues[selectedShunt]));
        }

        // Line 3: short-link LED maximum duty
        ImGui::AlignTextToFramePadding();
        ImGui::Text("通信灯亮度:");
        ImGui::SameLine(leftComboX);
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##LinkLedCombo", &selectedLinkLed, linkLedLabels, IM_ARRAYSIZE(linkLedLabels))) {
            serial.SendCommand("CMD:LINKLED=" + std::to_string(linkLedValues[selectedLinkLed]));
        }

        ImGui::SameLine(col2X);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("BLE广播:");
        ImGui::SameLine(rightComboX);
        ImGui::SetNextItemWidth(comboItemW);
        if (ImGui::Combo("##BleCombo", &selectedBle, bleLabels, IM_ARRAYSIZE(bleLabels))) {
            serial.SendCommand("CMD:BLE=" + std::to_string(bleValues[selectedBle]));
        }

        ImGui::Spacing();
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
        ImGui::SameLine();
        if (UIStyle::TactileButton("复制", ImVec2(48, 20),
            IM_COL32(255, 255, 255, 255), IM_COL32(93, 93, 93, 255), IM_COL32(224, 224, 224, 255), 4.0f)) {
            std::lock_guard<std::mutex> lock(state.controlLogMutex);
            std::string text;
            for (const auto& line : state.controlLog) { text += line; text.push_back('\n'); }
            ImGui::SetClipboardText(text.c_str());
        }

        // Log scrolling box (Pure white elevated container)
        float logBoxH = 142.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, UIStyle::ColorBgCard());
        ImGui::PushStyleColor(ImGuiCol_Border, UIStyle::ColorBorder());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        ImGui::BeginChild("ControlLogChild", ImVec2(0.0f, logBoxH), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
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
        ImGui::Spacing();
        if (UIStyle::AccentButton("保存配置至 Flash (CMD:SAVE)", ImVec2(rightColW, 28), 4.0f)) {
            serial.SendCommand("CMD:SAVE");
        }
    }
    ImGui::EndGroup();
}

void Dashboard::RenderFirmwareTab() {
    auto& serial = SerialManager::Instance();
    auto& state = AppState::Instance();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float halfW = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    const float btnH = 32.0f;

    ImGui::TextColored(UIStyle::ColorTextMain(), "进入出厂 ISP");
    ImGui::TextColored(UIStyle::ColorTextMuted(), "用于重新烧录；CMD:BOOT 控制的是 Probe 后面的目标 MCU，不会让 Probe 自身进入 ISP。");
    ImGui::Spacing();

    ImGui::BeginDisabled(!serial.IsDongleCOM2Open());
    if (UIStyle::TactileButton("Dongle 进入 ISP", ImVec2(halfW, btnH),
        IM_COL32(255, 240, 240, 255), IM_COL32(196, 43, 28, 255), IM_COL32(248, 215, 218, 255), 4.0f)) {
        ImGui::OpenPopup("确认 Dongle 进入 ISP");
    }
    ImGui::SameLine();
    if (UIStyle::TactileButton("Probe 进入 ISP", ImVec2(halfW, btnH),
        IM_COL32(255, 248, 236, 255), IM_COL32(178, 91, 0, 255), IM_COL32(253, 227, 181, 255), 4.0f)) {
        ImGui::OpenPopup("确认 Probe 进入 ISP");
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextColored(UIStyle::ColorTextMuted(), "Dongle：双 COM 口断开，经 USB ISP 重新烧录。  Probe：无线链路断开，需连接 Probe 的物理 UART 重新烧录。");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(UIStyle::ColorTextMain(), "固件免拆升级 / OTA (无需 WCH 官方工具)");
    ImGui::BeginDisabled(!serial.IsDongleCOM2Open());
    if (UIStyle::TactileButton("Dongle 固件下发", ImVec2(halfW, btnH))) {
        ImGui::OpenPopup("Dongle 固件下发");
    }
    ImGui::SameLine();
    if (UIStyle::TactileButton("Probe OTA 升级", ImVec2(halfW, btnH))) {
        ImGui::OpenPopup("Probe OTA 升级");
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("基于 Flash A/B 分区镜像搬移与 CRC32 强校验：分块发送至 Slot B (0x00020000)，整体验证通过后自动在 RAM 中覆盖 Slot A 并重启；支持断电安全保护，免拆外壳、免飞线、免依赖 WCH 官方工具。");

    ImGui::SetNextWindowSize(ImVec2(470.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("确认 Dongle 进入 ISP", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("这会擦除 Dongle Flash 首扇区并重启进入出厂 ISP。双 COM 口将消失；之后需要用 WCHISPTool 重新烧录 Dongle 固件。");
        ImGui::Spacing();
        if (UIStyle::TactileButton("取消", ImVec2(110.0f, btnH))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(!serial.IsDongleCOM2Open());
        if (UIStyle::TactileButton("确认 Dongle 进入 ISP", ImVec2(200.0f, btnH),
            IM_COL32(255, 240, 240, 255), IM_COL32(196, 43, 28, 255), IM_COL32(248, 215, 218, 255), 4.0f)) {
            if (serial.SendCommand("CMD:DFU")) {
                state.AddControlLog("[DFU] 已发送 Dongle 进入 ISP 命令；请用 WCHISPTool 重新烧录。");
                serial.CloseCOM1();
                serial.CloseDongleCOM2();
            } else {
                state.AddControlLog("[DFU] 未能确认命令发送成功，请检查 Dongle 是否已进入 ISP。");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(470.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("确认 Probe 进入 ISP", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("这会经无线命令擦除 Probe Flash 首扇区并重启。Probe 随后失去无线功能，必须连接其物理 UART 并用 WCHISPTool 重新烧录；无法通过当前 Dongle 无线恢复。");
        ImGui::Spacing();
        if (UIStyle::TactileButton("取消", ImVec2(110.0f, btnH))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(!serial.IsDongleCOM2Open());
        if (UIStyle::TactileButton("确认 Probe 进入 ISP", ImVec2(200.0f, btnH),
            IM_COL32(255, 248, 236, 255), IM_COL32(178, 91, 0, 255), IM_COL32(253, 227, 181, 255), 4.0f)) {
            if (serial.SendCommand("CMD:PROBE_DFU")) {
                state.AddControlLog("[DFU] 已请求 Probe 进入 ISP；等待 Probe 回应后连接物理 UART 烧录。");
            } else {
                state.AddControlLog("[DFU] Probe ISP 命令发送失败。");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Dongle 固件下发", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("选择 Dongle 的 Intel HEX 后，上位机将独占 COM2 分块发送、等待每块 ACK，完成 CRC32 校验后由固件自动切换镜像并重启。更新期间两个虚拟串口会短暂消失。\n\n当前端口：%s",
                           state.dongleCOM2Port.empty() ? "<未连接>" : state.dongleCOM2Port.c_str());
        ImGui::Spacing();
        if (UIStyle::TactileButton("关闭", ImVec2(100.0f, btnH))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(serial.IsFirmwareUpdateRunning());
        if (UIStyle::TactileButton("选择 HEX 并开始更新", ImVec2(230.0f, btnH))) {
            std::string hexPath;
            if (PickHexFile(hexPath)) {
                if (!serial.StartFirmwareUpdate(false, hexPath)) {
                    state.AddControlLog("[DONGLE FW] 启动更新失败：COM2 未连接或已有更新任务");
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        if (serial.IsFirmwareUpdateRunning()) {
            ImGui::SameLine();
            ImGui::TextColored(UIStyle::ColorPower(), "更新进行中...");
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Probe OTA 升级", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("选择 Probe 的 Intel HEX 后，文件经 Dongle COM2 分块转发到 Probe 的 Slot B，逐块等待无线 ACK，完成 CRC32 校验后由 Probe 自动切换镜像并重启。\n\n当前端口：%s",
                           state.dongleCOM2Port.empty() ? "<未连接>" : state.dongleCOM2Port.c_str());
        ImGui::Spacing();
        if (UIStyle::TactileButton("关闭", ImVec2(100.0f, btnH))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(serial.IsFirmwareUpdateRunning());
        if (UIStyle::TactileButton("选择 HEX 并开始 OTA", ImVec2(220.0f, btnH))) {
            std::string hexPath;
            if (PickHexFile(hexPath)) {
                if (!serial.StartFirmwareUpdate(true, hexPath)) {
                    state.AddControlLog("[PROBE OTA] 启动更新失败：COM2 未连接或已有更新任务");
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        if (serial.IsFirmwareUpdateRunning()) {
            ImGui::SameLine();
            ImGui::TextColored(UIStyle::ColorPower(), "更新进行中...");
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::TextColored(UIStyle::ColorTextMain(), "固件维护日志");
    ImGui::BeginChild("FirmwareLog", ImVec2(0.0f, 105.0f), true);
    {
        std::lock_guard<std::mutex> lock(state.controlLogMutex);
        const size_t first = state.controlLog.size() > 12 ? state.controlLog.size() - 12 : 0;
        for (size_t i = first; i < state.controlLog.size(); ++i)
            ImGui::TextUnformatted(state.controlLog[i].c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
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
        ImGui::PushItemWidth(300.0f);
        auto portLabel = [](const SerialPortInfo& p) {
            std::string label = p.portName + " - " + p.friendlyName;
            if (!p.busReportedDesc.empty()) {
                label += " （总线设备描述：" + p.busReportedDesc + "）";
            }
            return label;
        };
        std::string preview = (!portList.empty() && selectedPortIdx < portList.size())
            ? portLabel(portList[selectedPortIdx])
            : "未找到串口";
        const bool previewIsPassthrough = !portList.empty() && selectedPortIdx < portList.size() &&
                                          portList[selectedPortIdx].isDongleCOM1;
        if (previewIsPassthrough) {
            ImGui::PushFont(g_FontBold);
            ImGui::PushStyleColor(ImGuiCol_Text, UIStyle::ColorVoltage());
        }
        bool comboOpen = ImGui::BeginCombo("##ComPortCombo", preview.c_str());
        if (previewIsPassthrough) {
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        if (comboOpen) {
            for (int i = 0; i < (int)portList.size(); ++i) {
                bool isSelected = (selectedPortIdx == i);
                std::string label = portLabel(portList[i]);
                const bool isPassthrough = portList[i].isDongleCOM1;
                if (isPassthrough) {
                    ImGui::PushFont(g_FontBold);
                    ImGui::PushStyleColor(ImGuiCol_Text, UIStyle::ColorVoltage());
                }
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedPortIdx = i;
                }
                if (isPassthrough) {
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
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
        ImGui::SameLine();
        if (UIStyle::TactileButton("复制日志", ImVec2(68, toolbarH),
            IM_COL32(255, 255, 255, 255), IM_COL32(93, 93, 93, 255), IM_COL32(224, 224, 224, 255), 4.0f)) {
            std::lock_guard<std::mutex> lock(state.com1RxMutex);
            std::string copyText;
            for (const auto& line : state.com1LogLines) {
                copyText += line;
                copyText.push_back('\n');
            }
            if (!state.com1LogAccumulator.empty()) copyText += "[" + MakeLogTimestamp() + "] " + state.com1LogAccumulator;
            if (copyText.empty()) copyText.assign(state.com1RxBuffer.begin(), state.com1RxBuffer.end());
            ImGui::SetClipboardText(copyText.c_str());
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
        ImGui::BeginChild("Com1TerminalOutput", ImVec2(0.0f, 116.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
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
                    if (!state.com1LogLines.empty() || !state.com1LogAccumulator.empty()) {
                        for (const auto& line : state.com1LogLines) {
                            ImGui::TextColored(UIStyle::ColorTextMain(), "%s", line.c_str());
                        }
                        if (!state.com1LogAccumulator.empty()) {
                            ImGui::TextColored(UIStyle::ColorTextMain(), "[%s] %s",
                                               MakeLogTimestamp().c_str(), state.com1LogAccumulator.c_str());
                        }
                    } else {
                        std::string text(state.com1RxBuffer.begin(), state.com1RxBuffer.end());
                        ImGui::TextColored(UIStyle::ColorTextMain(), "%s", text.c_str());
                    }
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
        float eolComboW = 110.0f;
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
    auto& serial = SerialManager::Instance();
    auto& state = AppState::Instance();

    ImGui::TextColored(UIStyle::ColorVoltage(), "Probe 蓝牙未配对广播机制 (ADV_NONCONN_IND)");
    ImGui::BulletText("目标 MAC 地址: %s", state.bleDeviceMac.c_str());
    ImGui::BulletText("服务 UUID: 0xFCD2 (16-bit Service Data 广播格式)");
    ImGui::BulletText("广播 Service Data: 电流有符号 17 位(0.125mA/LSB) + 电压 15 位(1.25mV/LSB) + 序号");
    ImGui::BulletText("实时信号强度 (RSSI): %d dBm", (int)state.bleLastRssi.load());
    ImGui::BulletText("累计捕获广播包数: %llu", (unsigned long long)state.blePacketCount.load());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // On-demand 2.4 GHz link test. Throughput is measured at the Dongle,
    // while RTT statistics come from Probe acknowledgements.
    ImGui::TextColored(UIStyle::ColorTextMain(), "无线链路有效速率与延迟");
    ImGui::TextColored(UIStyle::ColorTextMuted(), "测试期间会占用无线链路，完成后结果写入下方控制日志。");
    static const char* const rfTestDurationLabels[] = { "1 秒", "3 秒", "5 秒" };
    static const uint32_t rfTestDurationValues[] = { 1000u, 3000u, 5000u };
    static int rfTestDurationIndex = 1;

    ImGui::SetNextItemWidth(92.0f);
    ImGui::Combo("##RfTestDuration", &rfTestDurationIndex,
                 rfTestDurationLabels, IM_ARRAYSIZE(rfTestDurationLabels));
    ImGui::SameLine();
    bool rfTestRunning = false;
    RfTestResult rfTestResult;
    bool rfTimedOut = false;
    {
        std::lock_guard<std::mutex> lock(state.rfTestMutex);
        const double now = state.GetElapsedSeconds();
        if (state.rfTest.running && state.rfTest.started_at > 0.0 &&
            state.rfTest.requested_ms > 0 &&
            now - state.rfTest.started_at >
                (static_cast<double>(state.rfTest.requested_ms) / 1000.0 + 3.0)) {
            state.rfTest.running = false;
            state.rfTest.valid = false;
            state.rfTest.started_at = 0.0;
            rfTimedOut = true;
        }
        rfTestResult = state.rfTest;
        rfTestRunning = state.rfTest.running;
    }
    if (rfTimedOut) {
        state.AddControlLog("[RFTEST] 超时，已结束本次测试");
    }
    ImGui::BeginDisabled(!serial.IsDongleCOM2Open() || rfTestRunning);
    if (UIStyle::AccentButton("开始测试", ImVec2(92.0f, 24.0f), 4.0f)) {
        {
            std::lock_guard<std::mutex> lock(state.rfTestMutex);
            state.rfTest.running = true;
            state.rfTest.valid = false;
            state.rfTest.started_at = state.GetElapsedSeconds();
            state.rfTest.requested_ms = rfTestDurationValues[rfTestDurationIndex];
        }
        if (!serial.SendCommand("CMD:RFTEST=" +
                                std::to_string(rfTestDurationValues[rfTestDurationIndex]))) {
            std::lock_guard<std::mutex> lock(state.rfTestMutex);
            state.rfTest.running = false;
            state.rfTest.started_at = 0.0;
            state.AddControlLog("[RFTEST] 命令发送失败");
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (rfTestRunning) {
        ImGui::TextColored(UIStyle::ColorTextMuted(), "测试进行中...");
    } else if (rfTestResult.valid) {
        ImGui::Text("速率 %.2f KB/s  RTT 平均 %lu us  最小 %lu  最大 %lu  (%lu 包)",
                    static_cast<double>(rfTestResult.rate_bps) / 1024.0,
                    static_cast<unsigned long>(rfTestResult.avg_us),
                    static_cast<unsigned long>(rfTestResult.min_us),
                    static_cast<unsigned long>(rfTestResult.max_us),
                    static_cast<unsigned long>(rfTestResult.packets));
    } else {
        ImGui::TextColored(UIStyle::ColorTextMuted(), "暂无测试结果");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(UIStyle::ColorCurrent(), "INA226 满分辨率高精度遥测设计指标");
    ImGui::BulletText("分流电压分辨率: 1 LSB = 2.5 uV (搭配 20 mΩ 采样电阻即 1 LSB = 0.125 mA = 125 uA)");
    ImGui::BulletText("母线电压分辨率: 1 LSB = 1.25 mV");
    ImGui::BulletText("功率由上位机高精度浮点计算: P = V_bus * I_shunt (保留 2 位小数精确至 0.01 mW)");
    ImGui::BulletText("极低射频开销与功耗: 工程量载荷 5 字节，扫描端无需配置采样电阻");
}

} // namespace CH570App
