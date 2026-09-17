#include "hud/hud_game_info.h"

#include "hud/hud_text.h"

#include "core/config.h"
#include "core/settings.h"
#include "libwwhd/libwwhd.h"
#include "render/renderer.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include <coreinit/time.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

namespace Hud {
namespace GameInfo {
static const float kBaseW = 164.0f;
static const float kRowStep = 13.0f;
static const float kPadding = 23.0f;
static const float kMinBaseH = 62.0f;
static const float kDefaultW = 164.0f;
static const float kDefaultH = 153.0f;
static const float kValueRightX = 152.0f;
static const float kFirstRowY = 12.0f;
static const float kLabelX = 12.0f;
static const float kFontSize = 9.3f;

struct RowOption { uint32_t bit; const char* label; const char* shortLabel; };
static const RowOption kRows[ROW_COUNT] = {
    { ROW_TIME_OF_DAY, "Time of Day", "ToD:" },
    { ROW_CURRENT_SESSION, "Current Session", "Current Session:" },
    { ROW_ANGLE, "Angle", "Angle:" },
    { ROW_Y_ANGLE, "Y-Angle", "Y-Angle:" },
    { ROW_SPEED, "Speed", "Speed:" },
    { ROW_POTENTIAL_SPEED, "Potential Speed", "Potential:" },
    { ROW_SPEED_ANGLE, "Speed Angle", "Spd Angle:" },
    { ROW_X, "X Position", "X:" },
    { ROW_Y, "Y Position", "Y:" },
    { ROW_Z, "Z Position", "Z:" },
    { ROW_ACTION, "Action", "Action:" },
    { ROW_FRAME, "Frame", "Frame:" },
    { ROW_DATE, "System Date", "Date:" },
    { ROW_CLOCK, "System Time", "Clock:" },
    { ROW_REGION, "Region", "Region:" },
    { ROW_DEMO, "Demo State", "Demo:" },
    { ROW_STORAGE, "Storage", "Storage:" },
};

static Ui::WindowState s_win = { false, 40.0f, 90.0f, kDefaultW, kDefaultH };
static bool s_settingsOpen = false;
static uint32_t s_visibleRows = DEFAULT_VISIBLE_ROWS;
static uint32_t s_order[ROW_COUNT] = {
    ROW_TIME_OF_DAY, ROW_CURRENT_SESSION, ROW_ANGLE, ROW_Y_ANGLE,
    ROW_SPEED, ROW_POTENTIAL_SPEED, ROW_SPEED_ANGLE, ROW_X, ROW_Y, ROW_Z,
    ROW_ACTION, ROW_FRAME, ROW_DATE, ROW_CLOCK, ROW_REGION, ROW_DEMO,
    ROW_STORAGE
};
static bool s_showProcNames = false;
static bool s_boatValues = true;
static bool s_applyPos = false, s_applySize = false;
static OSTime s_sessionStart = 0;

static unsigned visibleCount(uint32_t rows)
{
    unsigned count = 0;
    for (rows &= ALL_VISIBLE_ROWS; rows; rows >>= 1) count += rows & 1u;
    return count;
}

static float baseHeight()
{
    const float h = kPadding + kRowStep * visibleCount(s_visibleRows);
    return h < kMinBaseH ? kMinBaseH : h;
}

static const RowOption* rowFor(uint32_t bit)
{
    for (unsigned i = 0; i < ROW_COUNT; ++i)
        if (kRows[i].bit == bit) return &kRows[i];
    return nullptr;
}

Ui::WindowState& State() { return s_win; }

void ApplyState()
{
    if (s_win.w < MIN_WIDTH) s_win.w = MIN_WIDTH;
    if (s_win.w > MAX_WIDTH) s_win.w = MAX_WIDTH;
    s_win.h = s_win.w / (kBaseW / baseHeight());
    s_applyPos = s_applySize = true;
}

static void toggleVisibleRows(uint32_t rows)
{
    rows &= ALL_VISIBLE_ROWS;
    if (!rows || rows == s_visibleRows) return;
    const float oldH = s_win.h;
    s_visibleRows = rows;
    ApplyState();
    s_win.y -= s_win.h - oldH;
}

void ResetRowOrder()
{
    for (unsigned i = 0; i < ROW_COUNT; ++i) s_order[i] = kRows[i].bit;
}

void ResetToDefaults()
{
    s_settingsOpen = false;
    s_visibleRows = DEFAULT_VISIBLE_ROWS;
    s_showProcNames = false;
    s_boatValues = true;
    ResetRowOrder();
    s_win.enabled = false;
    s_win.x = 40.0f;
    s_win.y = 90.0f;
    s_win.w = kDefaultW;
    ApplyState();
}

void DrawSettingsButton()
{
    Ui::Window::Button("Settings##GameInfo", &s_settingsOpen, "Game Info Settings");
}

void DrawSettingsWindow()
{
    if (!s_settingsOpen) return;
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Game Info Settings", &s_settingsOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted("Visible information and display order");
        ImGui::Separator();
        int moveFrom = -1, moveTo = -1;
        const unsigned count = visibleCount(s_visibleRows);
        for (unsigned i = 0; i < ROW_COUNT; ++i) {
            const RowOption* row = rowFor(s_order[i]);
            if (!row) continue;

            ImGui::PushID((int)row->bit);
            bool visible = (s_visibleRows & row->bit) != 0;

            if (visible && count == 1) ImGui::BeginDisabled();
            if (ImGui::Checkbox(row->label, &visible)) {
                toggleVisibleRows(visible ? s_visibleRows | row->bit
                                          : s_visibleRows & ~row->bit);
                Config::MarkDirty();
            }
            if (visible && count == 1) ImGui::EndDisabled();
            ImGui::SameLine(300.0f);
            if (i == 0) ImGui::BeginDisabled();
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { moveFrom = (int)i; moveTo = (int)i - 1; }
            if (i == 0) ImGui::EndDisabled();
            ImGui::SameLine();
            if (i + 1 == ROW_COUNT) ImGui::BeginDisabled();
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { moveFrom = (int)i; moveTo = (int)i + 1; }
            if (i + 1 == ROW_COUNT) ImGui::EndDisabled();
            ImGui::PopID();
        }

        if (moveFrom >= 0 && moveTo >= 0) {
            const uint32_t temp = s_order[moveFrom];
            s_order[moveFrom] = s_order[moveTo]; s_order[moveTo] = temp;
            Config::MarkDirty();
        }

        ImGui::Separator();
        if (ImGui::Checkbox("Name the Action procedure", &s_showProcNames))
            Config::MarkDirty();
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Procedure ids are verified; the names are inferred "
                              "from the GameCube build and may be wrong. Long "
                              "names also overflow a narrow window.");
        if (ImGui::Checkbox("Show the boat's values while sailing", &s_boatValues))
            Config::MarkDirty();
    }
    ImGui::End();
}

uint32_t GetVisibleRows() { return s_visibleRows; }

void SetVisibleRows(uint32_t rows)
{
    rows &= ALL_VISIBLE_ROWS;
    if (rows)
        s_visibleRows = rows;
}

bool GetShowProcNames() { return s_showProcNames; }
void SetShowProcNames(bool show) { s_showProcNames = show; }

bool GetBoatValues() { return s_boatValues; }
void SetBoatValues(bool show) { s_boatValues = show; }

void GetRowOrder(uint32_t* rows, unsigned count)
{
    if (!rows) return;
    if (count > ROW_COUNT) count = ROW_COUNT;
    memcpy(rows, s_order, count * sizeof(uint32_t));
}

bool SetRowOrder(const uint32_t* rows, unsigned count)
{
    if (!rows || count != ROW_COUNT) return false;

    uint32_t seen = 0;
    for (unsigned i = 0; i < ROW_COUNT; ++i) {
        const uint32_t bit = rows[i];
        if (!bit || (bit & ~ALL_VISIBLE_ROWS) || (bit & (bit - 1u)) || (seen & bit)) return false;
        seen |= bit;
    }
    if (seen != ALL_VISIBLE_ROWS) return false;
    memcpy(s_order, rows, sizeof(s_order));
    return true;
}

void OnApplicationStart() { s_sessionStart = 0; }

static void aspectConstraint(ImGuiSizeCallbackData* data)
{
    const float ratio = *(float*)data->UserData;
    float w = data->DesiredSize.x, h = data->DesiredSize.y;
    if (w / ratio > h) h = w / ratio; else w = h * ratio;
    data->DesiredSize = ImVec2(w, h);
}

static const char* formatRow(uint32_t bit, char* buf, size_t n,
                             daPy_lk_c* link, const daShip_c* ship, OSTime now)
{
    switch (bit) {
    case ROW_TIME_OF_DAY: {
        u8 h = 0, m = 0;
        if (!dKy_getTimeHM(&h, &m)) return "--:--";
        snprintf(buf, n, "%02u:%02u", (unsigned)h, (unsigned)m);
        return buf;
    }
    case ROW_CURRENT_SESSION: {
        const uint64_t sec =
            (uint64_t)(now - s_sessionStart) / (uint64_t)OSTimerClockSpeed;
        snprintf(buf, n, "%02llu:%02llu:%02llu",
                 (unsigned long long)(sec / 3600),
                 (unsigned long long)((sec / 60) % 60),
                 (unsigned long long)(sec % 60));
        return buf;
    }
    case ROW_FRAME: {
        counter_class* ctr = cCt_getCounter();
        if (!ctr) return "--";
        snprintf(buf, n, "%u", (unsigned)ctr->mCounter0);
        return buf;
    }
    case ROW_DATE: {
        OSCalendarTime ct;
        OSTicksToCalendarTime(now, &ct);
        snprintf(buf, n, "%04d-%02d-%02d", ct.tm_year, ct.tm_mon + 1, ct.tm_mday);
        return buf;
    }
    case ROW_CLOCK: {
        OSCalendarTime ct;
        OSTicksToCalendarTime(now, &ct);
        snprintf(buf, n, "%02d:%02d:%02d", ct.tm_hour, ct.tm_min, ct.tm_sec);
        return buf;
    }
    case ROW_REGION:
        return wwhd_regionCode();
    }

    if (!link)
        return "--";

    const fopAc_ac_c* ac = ship ? &ship->base : &link->base;
    switch (bit) {
    case ROW_ANGLE:
        snprintf(buf, n, "%u", (unsigned)(uint16_t)ac->shape_angle.y);
        return buf;
    case ROW_Y_ANGLE:
        snprintf(buf, n, "%d", (int)daPy_getLookAngleY(link));
        return buf;
    case ROW_SPEED:
        snprintf(buf, n, "%.7f", (double)ac->speedF);
        return buf;
    case ROW_POTENTIAL_SPEED: {
        const f32* wanted = daPy_getNormalSpeedPtr();
        if (!wanted) return "--";
        snprintf(buf, n, "%.7f", (double)*wanted);
        return buf;
    }
    case ROW_SPEED_ANGLE:
        snprintf(buf, n, "%u", (unsigned)(uint16_t)ac->current.angle.y);
        return buf;
    case ROW_X:
        snprintf(buf, n, "%.7f", (double)ac->current.pos.x);
        return buf;
    case ROW_Y:
        snprintf(buf, n, "%.7f", (double)ac->current.pos.y);
        return buf;
    case ROW_Z:
        snprintf(buf, n, "%.7f", (double)ac->current.pos.z);
        return buf;
    case ROW_ACTION:
        if (s_showProcNames)
            snprintf(buf, n, "%s(%d)", daPy_procName(link->mCurProc),
                     (int)link->mCurProc);
        else
            snprintf(buf, n, "%d", (int)link->mCurProc);
        return buf;
    case ROW_DEMO:
        snprintf(buf, n, "%s(%u)", daPy_demoModeName(link->mDemo.mDemoMode),
                 (unsigned)link->mDemo.mDemoMode);
        return buf;
    case ROW_STORAGE: {
        const char* base = dEvt_isStorageArmed() ? "armed"
                         : dEvt_isEventRunning() ? "event" : "off";
        const u32* acch = daPy_getAcchFlagsPtr();
        const u32 flags = acch ? *acch : 0u;
        if (!(flags & WWHD_ACCH_WALL_NONE))
            return base;
        snprintf(buf, n, "%s +%s", base,
                 (flags & WWHD_ACCH_LINE_CHECK_NONE) ? "door" : "chest");
        return buf;
    }
    }
    return "--";
}

static float s_layoutK = 0.0f;

static bool moved(float a, float b) { return a - b > 0.5f || b - a > 0.5f; }

void DrawWindow(bool menuActive)
{
    const OSTime now = OSGetTime();
    if (!s_sessionStart || now < s_sessionStart) s_sessionStart = now;
    if (!s_win.enabled) return;

    const float k = 1.0f / Renderer::UiScale();
    if (k != s_layoutK) {
        s_layoutK = k;
        s_applyPos = s_applySize = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float hBase = baseHeight(), aspect = kBaseW / hBase;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive) flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    const float background = Config::g_settings.overlayOpacity;

    ImGui::SetNextWindowSizeConstraints(ImVec2(MIN_WIDTH * k, MIN_WIDTH * k / aspect),
                                        ImVec2(MAX_WIDTH * k, MAX_WIDTH * k / aspect),
                                        aspectConstraint, (void*)&aspect);
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
    const bool open = ImGui::Begin("Game Info", menuActive ? &s_win.enabled : nullptr, flags);
    ImGui::SetWindowFontScale(k);
    Renderer::MarkGameScreenOnly(ImGui::GetWindowDrawList());
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    s_win.x = wp.x / k; s_win.y = wp.y / k; s_win.w = ws.x / k; s_win.h = ws.y / k;

    if (open) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        if (avail.y / hBase < scale) scale = avail.y / hBase;
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImVec2 origin(cursor.x + (avail.x - kBaseW * scale) * 0.5f,
                      cursor.y + (avail.y - hBase * scale) * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const float fontSize = kFontSize * scale;
        dl->AddRectFilled(origin, ImVec2(origin.x + kBaseW * scale, origin.y + hBase * scale),
                          IM_COL32(0, 0, 0, (int)(115.0f * background)), 6.0f * scale);

        daPy_lk_c* link = daPy_lk_c_getPlayer();
        const daShip_c* ship = (s_boatValues && link && daPy_isRidingShip())
                                   ? get_daShip() : nullptr;
        const uint32_t boatRows = ROW_ANGLE | ROW_SPEED | ROW_SPEED_ANGLE |
                                  ROW_X | ROW_Y | ROW_Z;

        const uint32_t playerRows = ROW_ANGLE | ROW_Y_ANGLE | ROW_SPEED |
                                    ROW_POTENTIAL_SPEED | ROW_SPEED_ANGLE |
                                    ROW_X | ROW_Y | ROW_Z | ROW_ACTION |
                                    ROW_DEMO;
        const bool bold = Config::g_settings.boldLetters;
        float rowY = kFirstRowY;
        for (unsigned oi = 0; oi < ROW_COUNT; ++oi) {
            const uint32_t bit = s_order[oi];
            if (!(s_visibleRows & bit)) continue;
            unsigned idx = 0; while (idx < ROW_COUNT && kRows[idx].bit != bit) ++idx;
            if (idx == ROW_COUNT) continue;
            ImVec2 lp(origin.x + kLabelX * scale, origin.y + rowY * scale);
            const bool boatRow = ship && (bit & boatRows);
            char labelBuf[32];
            const char* label = kRows[idx].shortLabel;
            if (boatRow) {
                const size_t len = strlen(label);
                const int stem = (int)(len && label[len - 1] == ':' ? len - 1 : len);
                snprintf(labelBuf, sizeof(labelBuf), "%.*s (B):", stem, label);
                label = labelBuf;
            }
            DrawText(dl, font, fontSize, lp, IM_COL32(180, 190, 204, 230), label, bold);
            char valueBuf[48];
            const char* value = formatRow(bit, valueBuf, sizeof(valueBuf), link,
                                          boatRow ? ship : nullptr, now);
            ImVec2 valueSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, value);
            ImVec2 vp(origin.x + kValueRightX * scale - valueSize.x, lp.y);
            const ImU32 valueColor = link || !(bit & playerRows)
                                         ? IM_COL32(255, 255, 255, 255)
                                         : IM_COL32(156, 164, 176, 200);
            DrawText(dl, font, fontSize, vp, valueColor, value, bold);
            rowY += kRowStep;
        }
        ImGui::Dummy(avail);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    if (menuActive && (wasEnabled != s_win.enabled || moved(oldX, s_win.x) || moved(oldY, s_win.y) ||
                       moved(oldW, s_win.w) || moved(oldH, s_win.h))) Config::MarkDirty();
}
}
}
