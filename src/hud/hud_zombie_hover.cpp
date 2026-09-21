#include "hud/hud_zombie_hover.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/settings.h"
#include "hud/hud_text.h"
#include "render/renderer.h"
#include "tools/zombie_hover.h"
#include "ui/ui_hotkey.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include <float.h>
#include <stdio.h>

namespace Hud {
namespace ZombieHover {
using Tools::ZombieHover::Run;
using Tools::ZombieHover::Input;

static const float kBaseW = 164.0f;
static const float kRowStep = 13.0f;
static const float kPadding = 23.0f;
static const float kValueRightX = 152.0f;
static const float kFirstRowY = 12.0f;
static const float kLabelX = 12.0f;
static const float kFontSize = 9.3f;
static const float kGraphH = 44.0f;
static const unsigned kTextRows = 10;
static const float kBaseH = kPadding + kRowStep * kTextRows + kGraphH;

static const uint32_t kLiveWindowFrames = 300;
static const float kGraphMaxGap = 8.0f;
static const float kGamePadFps = 30.0f;

static Ui::WindowState s_win = { false, 40.0f, 200.0f, kBaseW, kBaseH };
static bool s_applyPos = false, s_applySize = false;
static bool s_historyOpen = false;
static int  s_selected = 0;

static const ImU32 kColMuted   = IM_COL32(156, 164, 176, 200);
static const ImU32 kColLabel   = IM_COL32(180, 190, 204, 230);
static const ImU32 kColValue   = IM_COL32(255, 255, 255, 255);
static const ImU32 kColFirst   = IM_COL32(170, 178, 190, 220);
static const ImU32 kColPerfect = IM_COL32(90, 220, 120, 255);
static const ImU32 kColGood    = IM_COL32(200, 225, 90, 255);
static const ImU32 kColOk      = IM_COL32(255, 190, 70, 255);
static const ImU32 kColBad     = IM_COL32(255, 96, 96, 255);
static const ImU32 kColHeld    = IM_COL32(220, 50, 50, 255);
static const ImU32 kColWasted  = IM_COL32(190, 120, 255, 255);

static ImU32 gradeColor(uint8_t grade)
{
    switch (grade) {
    case Tools::ZombieHover::GRADE_PERFECT: return kColPerfect;
    case Tools::ZombieHover::GRADE_GOOD:    return kColGood;
    case Tools::ZombieHover::GRADE_OK:      return kColOk;
    case Tools::ZombieHover::GRADE_BAD:     return kColBad;
    case Tools::ZombieHover::GRADE_HELD:    return kColHeld;
    case Tools::ZombieHover::GRADE_WASTED:  return kColWasted;
    }
    return kColFirst;
}

static ImVec4 gradeColor4(uint8_t grade)
{
    return ImGui::ColorConvertU32ToFloat4(gradeColor(grade));
}

Ui::WindowState& State() { return s_win; }

void ApplyState()
{
    if (s_win.w < MIN_WIDTH) s_win.w = MIN_WIDTH;
    if (s_win.w > MAX_WIDTH) s_win.w = MAX_WIDTH;
    s_win.h = s_win.w / (kBaseW / kBaseH);
    s_applyPos = s_applySize = true;
}

void ResetToDefaults()
{
    s_historyOpen = false;
    s_selected = 0;
    s_win.enabled = false;
    s_win.x = 40.0f;
    s_win.y = 200.0f;
    s_win.w = kBaseW;
    ApplyState();
}

void DrawHistoryButton()
{
    Ui::Window::Button("Runs##ZombieHover", &s_historyOpen, "Zombie Hover Runs");
}

static uint32_t graded(const Run& r)
{
    return r.inputs - r.counts[Tools::ZombieHover::GRADE_FIRST];
}

static const char* formatSeconds(char* buf, size_t n, uint32_t frames)
{
    snprintf(buf, n, "%.2f s", (double)frames / (double)kGamePadFps);
    return buf;
}

static void drawRunGraph(ImDrawList* dl, const Run& r, ImVec2 p0, ImVec2 p1,
                         uint32_t windowFrames, bool live, float rounding)
{
    const float w = p1.x - p0.x, h = p1.y - p0.y;
    dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 16), rounding);

    const float perfectT = ((float)Tools::ZombieHover::GAP_PERFECT - 1.0f) / (kGraphMaxGap - 1.0f);
    const float neutralT = ((float)Tools::ZombieHover::GAP_OK - 1.0f) / (kGraphMaxGap - 1.0f);
    dl->AddLine(ImVec2(p0.x, p1.y - perfectT * h), ImVec2(p1.x, p1.y - perfectT * h),
                IM_COL32(90, 220, 120, 70), 1.0f);
    dl->AddLine(ImVec2(p0.x, p1.y - neutralT * h), ImVec2(p1.x, p1.y - neutralT * h),
                IM_COL32(255, 190, 70, 60), 1.0f);

    if (!r.valid || r.recorded == 0)
        return;

    uint32_t f0 = 0, f1 = r.frames;
    if (live && windowFrames) {
        f1 = r.frames > windowFrames ? r.frames : windowFrames;
        f0 = f1 - windowFrames;
    }
    if (f1 <= f0) f1 = f0 + 1;
    const float perFrame = w / (float)(f1 - f0);
    float barW = perFrame * 1.5f;
    if (barW < 1.0f) barW = 1.0f;
    if (barW > 6.0f) barW = 6.0f;

    for (uint32_t i = 0; i < r.recorded; ++i) {
        const Input& in = r.samples[i];
        if (in.frame < f0 || in.frame > f1)
            continue;
        const float x = p0.x + (float)(in.frame - f0) * perFrame;
        float x1 = x + barW;
        if (x1 > p1.x) x1 = p1.x;
        if (in.grade == Tools::ZombieHover::GRADE_HELD ||
            in.grade == Tools::ZombieHover::GRADE_WASTED) {
            dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(x1, p0.y + 0.22f * h), gradeColor(in.grade));
            continue;
        }
        float gap = in.gap ? (float)in.gap : (float)Tools::ZombieHover::GAP_PERFECT;
        float t = (gap - 1.0f) / (kGraphMaxGap - 1.0f);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        dl->AddRectFilled(ImVec2(x, p1.y - t * h), ImVec2(x1, p1.y), gradeColor(in.grade));
    }
}

static const char* lastAttackText(char* buf, size_t n, const Run& r, ImU32* color)
{
    if (!r.valid || r.recorded == 0) {
        *color = kColMuted;
        return "--";
    }
    const Input& in = r.samples[r.recorded - 1];
    *color = gradeColor(in.grade);
    if (in.grade == Tools::ZombieHover::GRADE_FIRST)
        return "First";
    if (in.grade == Tools::ZombieHover::GRADE_HELD)
        return "Too fast (held)";
    if (in.grade == Tools::ZombieHover::GRADE_WASTED)
        return "Press, no attack";
    const int late = (int)in.gap - Tools::ZombieHover::GAP_PERFECT;
    if (late <= 0)
        return Tools::ZombieHover::GradeName(in.grade);
    snprintf(buf, n, "%s +%df", Tools::ZombieHover::GradeName(in.grade), late);
    return buf;
}

static const char* statusText(const Run& r, bool hovering, ImU32* color)
{
    if (hovering) {
        *color = kColPerfect;
        return "Hovering";
    }
    if (!r.valid) {
        *color = kColMuted;
        return "Idle";
    }
    *color = kColValue;
    switch (r.endReason) {
    case Tools::ZombieHover::END_GROUND: return "Landed";
    case Tools::ZombieHover::END_WATER:  return "Water";
    case Tools::ZombieHover::END_SHIP:   return "Ship";
    case Tools::ZombieHover::END_DIED:   *color = kColBad; return "Died";
    }
    return Tools::ZombieHover::EndReasonName(r.endReason);
}

static float s_layoutK = 0.0f;

static bool moved(float a, float b) { return a - b > 0.5f || b - a > 0.5f; }

void DrawWindow(bool menuActive)
{
    if (!s_win.enabled || !Tools::ZombieHover::IsEnabled()) return;

    const float k = 1.0f / Renderer::UiScale();
    if (k != s_layoutK) {
        s_layoutK = k;
        s_applyPos = s_applySize = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float aspect = kBaseW / kBaseH;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive) flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    const float background = Config::g_settings.overlayOpacity;

    struct Aspect { static void keep(ImGuiSizeCallbackData* d) {
        const float ratio = *(float*)d->UserData;
        float w = d->DesiredSize.x, h = d->DesiredSize.y;
        if (w / ratio > h) h = w / ratio; else w = h * ratio;
        d->DesiredSize = ImVec2(w, h);
    } };
    ImGui::SetNextWindowSizeConstraints(ImVec2(MIN_WIDTH * k, MIN_WIDTH * k / aspect),
                                        ImVec2(MAX_WIDTH * k, MAX_WIDTH * k / aspect),
                                        Aspect::keep, (void*)&aspect);
    ImGui::SetNextWindowPos(ImVec2(s_win.x * k, s_win.y * k), s_applyPos ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(s_win.w * k, s_win.h * k), s_applySize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    s_applyPos = s_applySize = false;
    ImGui::SetNextWindowBgAlpha(Renderer::BackdropAlpha(0.5f * background));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * k);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * k, style.FramePadding.y * k));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    const bool wasEnabled = s_win.enabled;
    const float oldX = s_win.x, oldY = s_win.y, oldW = s_win.w, oldH = s_win.h;
    const bool open = ImGui::Begin("Zombie Hover", menuActive ? &s_win.enabled : nullptr, flags);
    ImGui::SetWindowFontScale(k);
    Renderer::MarkGameScreenOnly(ImGui::GetWindowDrawList());
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    s_win.x = wp.x / k; s_win.y = wp.y / k; s_win.w = ws.x / k; s_win.h = ws.y / k;

    if (open) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        if (avail.y / kBaseH < scale) scale = avail.y / kBaseH;
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImVec2 origin(cursor.x + (avail.x - kBaseW * scale) * 0.5f,
                      cursor.y + (avail.y - kBaseH * scale) * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const float fontSize = kFontSize * scale;
        dl->AddRectFilled(origin, ImVec2(origin.x + kBaseW * scale, origin.y + kBaseH * scale),
                          IM_COL32(0, 0, 0, (int)(115.0f * background)), 6.0f * scale);

        const bool hovering = Tools::ZombieHover::IsHovering();
        const Run& r = Tools::ZombieHover::Current();
        const bool bold = Config::g_settings.boldLetters;
        const bool have = r.valid;

        struct Row { const char* label; char value[40]; ImU32 color; };
        Row rows[kTextRows];
        unsigned n = 0;
        char tmp[40];
        const char* text;

        text = statusText(r, hovering, &rows[n].color);
        rows[n].label = "Status:"; snprintf(rows[n].value, sizeof(rows[n].value), "%s", text); ++n;

        rows[n].label = "Air time:"; rows[n].color = have ? kColValue : kColMuted;
        if (have) snprintf(rows[n].value, sizeof(rows[n].value), "%.2f s (%u)",
                           (double)r.frames / (double)kGamePadFps, (unsigned)r.frames);
        else snprintf(rows[n].value, sizeof(rows[n].value), "--");
        ++n;

        rows[n].label = "Attacks:"; rows[n].color = have ? kColValue : kColMuted;
        if (have) snprintf(rows[n].value, sizeof(rows[n].value), "%u", (unsigned)r.inputs);
        else snprintf(rows[n].value, sizeof(rows[n].value), "--");
        ++n;

        static const struct { const char* label; uint8_t grade; } kGrades[] = {
            { "Perfect:", Tools::ZombieHover::GRADE_PERFECT },
            { "Good:",    Tools::ZombieHover::GRADE_GOOD },
            { "OK:",      Tools::ZombieHover::GRADE_OK },
            { "Bad:",     Tools::ZombieHover::GRADE_BAD },
        };
        const uint32_t total = have ? graded(r) : 0;
        for (unsigned g = 0; g < 4; ++g) {
            rows[n].label = kGrades[g].label;
            const uint32_t c = have ? r.counts[kGrades[g].grade] : 0;
            rows[n].color = have && c ? gradeColor(kGrades[g].grade) : kColMuted;
            if (have && total)
                snprintf(rows[n].value, sizeof(rows[n].value), "%u  %u%%", (unsigned)c,
                         (unsigned)(c * 100u / total));
            else
                snprintf(rows[n].value, sizeof(rows[n].value), have ? "0" : "--");
            ++n;
        }

        rows[n].label = "Too fast:";
        {
            const uint32_t held = have ? r.counts[Tools::ZombieHover::GRADE_HELD] : 0;
            const uint32_t wasted = have ? r.counts[Tools::ZombieHover::GRADE_WASTED] : 0;
            rows[n].color = have && (held || wasted) ? kColHeld : kColMuted;
            if (!have) snprintf(rows[n].value, sizeof(rows[n].value), "--");
            else if (wasted) snprintf(rows[n].value, sizeof(rows[n].value), "%u  (%u lost)",
                                      (unsigned)held, (unsigned)wasted);
            else snprintf(rows[n].value, sizeof(rows[n].value), "%u", (unsigned)held);
        }
        ++n;

        rows[n].label = "Last:";
        text = lastAttackText(tmp, sizeof(tmp), r, &rows[n].color);
        snprintf(rows[n].value, sizeof(rows[n].value), "%s", text); ++n;

        rows[n].label = "Height:"; rows[n].color = have ? kColValue : kColMuted;
        if (have) snprintf(rows[n].value, sizeof(rows[n].value), "%+.0f (max %+.0f)",
                           (double)r.gainY, (double)r.maxGainY);
        else snprintf(rows[n].value, sizeof(rows[n].value), "--");
        ++n;

        float rowY = kFirstRowY;
        for (unsigned i = 0; i < n; ++i) {
            ImVec2 lp(origin.x + kLabelX * scale, origin.y + rowY * scale);
            DrawText(dl, font, fontSize, lp, kColLabel, rows[i].label, bold);
            ImVec2 valueSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, rows[i].value);
            ImVec2 vp(origin.x + kValueRightX * scale - valueSize.x, lp.y);
            DrawText(dl, font, fontSize, vp, rows[i].color, rows[i].value, bold);
            rowY += kRowStep;
        }

        const ImVec2 g0(origin.x + kLabelX * scale, origin.y + (rowY - 2.0f) * scale);
        const ImVec2 g1(origin.x + kValueRightX * scale, g0.y + (kGraphH - 6.0f) * scale);
        drawRunGraph(dl, r, g0, g1, kLiveWindowFrames, hovering, 2.0f * scale);
        ImGui::Dummy(avail);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    if (menuActive && (wasEnabled != s_win.enabled || moved(oldX, s_win.x) || moved(oldY, s_win.y) ||
                       moved(oldW, s_win.w) || moved(oldH, s_win.h))) Config::MarkDirty();
}

static const int kListRows = 8;

static void drawRunList(int count)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    const int shown = count < kListRows ? count : kListRows;
    const float rowH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2.0f;
    const ImVec2 size(0.0f, rowH * (float)(shown + 1));
    if (!ImGui::BeginTable("##runs", 6, flags, size))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("End",      ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("Air time", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Attacks",  ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Perfect",  ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Too fast", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableHeadersRow();
    for (int i = 0; i < count; ++i) {
        const Run& r = Tools::ZombieHover::HistoryAt(i);
        const uint32_t total = graded(r);
        const unsigned pct = total ? (unsigned)(r.counts[Tools::ZombieHover::GRADE_PERFECT] * 100u / total) : 0u;
        char id[32];
        snprintf(id, sizeof(id), "%d##run%d", i + 1, i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Selectable(id, i == s_selected, ImGuiSelectableFlags_SpanAllColumns))
            s_selected = i;
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tools::ZombieHover::EndReasonName(r.endReason));
        ImGui::TableNextColumn();
        ImGui::Text("%.2f s", (double)r.frames / (double)kGamePadFps);
        ImGui::TableNextColumn();
        ImGui::Text("%u", (unsigned)r.inputs);
        ImGui::TableNextColumn();
        ImGui::Text("%u%%", pct);
        ImGui::TableNextColumn();
        {
            const uint32_t held = r.counts[Tools::ZombieHover::GRADE_HELD];
            const uint32_t wasted = r.counts[Tools::ZombieHover::GRADE_WASTED];
            if (wasted)
                ImGui::Text("%u +%u", (unsigned)held, (unsigned)wasted);
            else
                ImGui::Text("%u", (unsigned)held);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%u frames with B held over, %u presses with no attack",
                                  (unsigned)held, (unsigned)wasted);
        }
    }
    ImGui::EndTable();
}

static void detailLabel(const char* label, const ImVec4* color)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (color)
        ImGui::TextColored(*color, "%s", label);
    else
        ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
}

static void drawRunDetails(const Run& r)
{
    const uint32_t total = graded(r);
    char buf[64];

    if (ImGui::BeginTable("##details", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        detailLabel("End:", nullptr);
        ImGui::TextWrapped("%s", Tools::ZombieHover::EndReasonName(r.endReason));

        detailLabel("Air time:", nullptr);
        ImGui::TextWrapped("%s  (%u frames)", formatSeconds(buf, sizeof(buf), r.frames),
                           (unsigned)r.frames);

        detailLabel("Jump attacks:", nullptr);
        ImGui::TextWrapped("%u  (%u graded, %u B presses seen)", (unsigned)r.inputs,
                           (unsigned)total, (unsigned)r.presses);

        static const struct { const char* label; uint8_t grade; } kGrades[] = {
            { "Perfect (2 frames):", Tools::ZombieHover::GRADE_PERFECT },
            { "Good (3 frames):",    Tools::ZombieHover::GRADE_GOOD },
            { "OK (4 frames):",      Tools::ZombieHover::GRADE_OK },
            { "Bad (5+ frames):",    Tools::ZombieHover::GRADE_BAD },
        };
        for (unsigned g = 0; g < 4; ++g) {
            const uint32_t c = r.counts[kGrades[g].grade];
            const ImVec4 color = gradeColor4(kGrades[g].grade);
            detailLabel(kGrades[g].label, &color);
            if (total)
                ImGui::Text("%u  (%u%%)", (unsigned)c, (unsigned)(c * 100u / total));
            else
                ImGui::TextUnformatted("0");
        }

        {
            const ImVec4 held = ImGui::ColorConvertU32ToFloat4(kColHeld);
            detailLabel("Too fast (B held over):", &held);
            ImGui::TextWrapped("%u frames where B was still down from the last press",
                               (unsigned)r.counts[Tools::ZombieHover::GRADE_HELD]);
            const ImVec4 wasted = ImGui::ColorConvertU32ToFloat4(kColWasted);
            detailLabel("Presses with no attack:", &wasted);
            ImGui::Text("%u", (unsigned)r.counts[Tools::ZombieHover::GRADE_WASTED]);
        }

        detailLabel("Average gap:", nullptr);
        if (total) {
            const double avg = (double)r.gapSum / (double)total;
            ImGui::TextWrapped("%.2f frames  (+%.2f late, worst +%u)", avg,
                               avg - (double)Tools::ZombieHover::GAP_PERFECT,
                               (unsigned)(r.maxGap > (uint32_t)Tools::ZombieHover::GAP_PERFECT
                                              ? r.maxGap - (uint32_t)Tools::ZombieHover::GAP_PERFECT : 0u));
        } else {
            ImGui::TextUnformatted("--");
        }

        detailLabel("Longest perfect streak:", nullptr);
        ImGui::Text("%u", (unsigned)r.perfectStreak);

        detailLabel("Height:", nullptr);
        ImGui::TextWrapped("%+.1f at the end, %+.1f at the peak", (double)r.gainY, (double)r.maxGainY);

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Lateness over time");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gw = avail.x > 80.0f ? avail.x : 80.0f;
    const float gh = 110.0f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + gw, p0.y + gh);
    drawRunGraph(ImGui::GetWindowDrawList(), r, p0, p1, 0, false, 3.0f);
    ImGui::Dummy(ImVec2(gw, gh));
    ImGui::TextDisabled("0 s");
    char end[32];
    snprintf(end, sizeof(end), "%.1f s", (double)r.frames / (double)kGamePadFps);
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(p1.x - ImGui::CalcTextSize(end).x, ImGui::GetCursorScreenPos().y));
    ImGui::TextDisabled("%s", end);
    if (r.recorded < r.inputs)
        ImGui::TextDisabled("Only the first %d attacks are drawn.", Tools::ZombieHover::MAX_INPUTS);
}

void DrawHistoryWindow()
{
    if (!s_historyOpen) return;
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(340.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowPos(ImVec2(120, 90), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Zombie Hover Runs", &s_historyOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        const int count = Tools::ZombieHover::HistoryCount();
        ImGui::Text("Last %d of up to %d runs, newest first.", count, Tools::ZombieHover::HISTORY);
        if (ImGui::Button("Reset stats"))
            Tools::ZombieHover::ResetStats();
        ImGui::SameLine(0.0f, 0.0f);
        Ui::Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_ZOMBIE_RESET), "  ", nullptr, true);
        ImGui::SameLine();
        if (ImGui::Button("Clear history")) {
            Tools::ZombieHover::ClearHistory();
            s_selected = 0;
        }
        if (Tools::ZombieHover::IsHovering())
            ImGui::TextColored(gradeColor4(Tools::ZombieHover::GRADE_PERFECT),
                               "Hovering now - the HUD window has the live run.");
        ImGui::Separator();

        if (count == 0) {
            ImGui::TextDisabled("No runs yet. Reach zero life in the air and mash B.");
        } else {
            if (s_selected >= count) s_selected = count - 1;
            if (s_selected < 0) s_selected = 0;
            drawRunList(count);
            ImGui::Separator();
            drawRunDetails(Tools::ZombieHover::HistoryAt(s_selected));
        }
    }
    ImGui::End();
}
}
}
