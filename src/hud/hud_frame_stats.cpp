#include "hud/hud_frame_stats.h"

#include "core/config.h"
#include "core/frame_stats.h"
#include "core/settings.h"
#include "hud/hud_text.h"
#include "render/renderer.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

namespace Hud {
namespace FrameStats {
static const float kBaseW = 164.0f;
static const float kRowStep = 13.0f;
static const float kPadding = 23.0f;
static const float kMinBaseH = 62.0f;
static const float kDefaultW = 164.0f;
static const float kValueRightX = 152.0f;
static const float kFirstRowY = 12.0f;
static const float kLabelX = 12.0f;
static const float kFontSize = 9.3f;
static const float kGraphH = 44.0f;
static const int   kGraphFrames = 120;

struct RowOption { uint32_t bit; const char* label; const char* shortLabel; };
static const RowOption kRows[ROW_COUNT] = {
    { ROW_FPS,     "FPS",                        "FPS:" },
    { ROW_FRAME,   "Frame time",                 "Frame:" },
    { ROW_AVG,     "Average frame time",         "Avg:" },
    { ROW_P50,     "Median frame time (p50)",    "p50:" },
    { ROW_P95,     "p95 frame time",             "p95:" },
    { ROW_P99,     "p99 frame time",             "p99:" },
    { ROW_LOW1,    "1% low FPS",                 "1% low:" },
    { ROW_MAX,     "Worst frame time",           "Worst:" },
    { ROW_CPU,     "CPU time (game execute)",    "CPU:" },
    { ROW_GPU,     "GPU time",                   "GPU:" },
    { ROW_OVERLAY, "Overlay time (this plugin)", "Overlay:" },
    { ROW_DRAWS,   "Draw calls",                 "Draws:" },
    { ROW_TRIS,    "Triangles",                  "Tris:" },
    { ROW_GRAPH,   "Frame time graph",           "" },
    { ROW_WAIT,    "Wait (frame minus CPU)",     "Wait:" },
    { ROW_GPU_OVL, "GPU time of the overlay",    "GPU ovl:" },
};

static Ui::WindowState s_win = { false, 40.0f, 420.0f, kDefaultW, 120.0f };
static bool s_settingsOpen = false;
static uint32_t s_visibleRows = DEFAULT_VISIBLE_ROWS;
static uint32_t s_graphSeries = DEFAULT_GRAPH_SERIES;
static uint32_t s_order[ROW_COUNT] = {
    ROW_FPS, ROW_FRAME, ROW_AVG, ROW_P50, ROW_P95, ROW_P99, ROW_LOW1, ROW_MAX,
    ROW_CPU, ROW_GPU, ROW_GPU_OVL, ROW_WAIT, ROW_OVERLAY, ROW_DRAWS, ROW_TRIS,
    ROW_GRAPH
};
static bool s_applyPos = false, s_applySize = false;

static unsigned visibleCount(uint32_t rows)
{
    unsigned count = 0;
    for (rows &= ALL_VISIBLE_ROWS; rows; rows >>= 1) count += rows & 1u;
    return count;
}

static float baseHeight()
{
    const uint32_t text = s_visibleRows & ~ROW_GRAPH;
    float h = kPadding + kRowStep * visibleCount(text);
    if (s_visibleRows & ROW_GRAPH)
        h += kGraphH;
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
    s_graphSeries = DEFAULT_GRAPH_SERIES;
    ResetRowOrder();
    s_win.enabled = false;
    s_win.x = 40.0f;
    s_win.y = 420.0f;
    s_win.w = kDefaultW;
    ApplyState();
}

void DrawSettingsButton()
{
    Ui::Window::Button("Settings##FrameStats", &s_settingsOpen, "Frame Stats Settings");
}

void DrawSettingsWindow()
{
    if (!s_settingsOpen) return;
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Frame Stats Settings", &s_settingsOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted("Visible statistics and display order");
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
        ImGui::TextUnformatted("Graph");
        bool frame = (s_graphSeries & GRAPH_FRAME) != 0;
        bool cpu = (s_graphSeries & GRAPH_CPU) != 0;
        bool gpu = (s_graphSeries & GRAPH_GPU) != 0;
        bool changed = false;
        changed |= ImGui::Checkbox("Frame time bars", &frame);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("CPU line", &cpu);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("GPU line", &gpu);
        if (changed) {
            s_graphSeries = (frame ? GRAPH_FRAME : 0u) | (cpu ? GRAPH_CPU : 0u) | (gpu ? GRAPH_GPU : 0u);
            Config::MarkDirty();
        }
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

uint32_t GetGraphSeries() { return s_graphSeries; }
void SetGraphSeries(uint32_t series) { s_graphSeries = series & ALL_GRAPH_SERIES; }

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

static void aspectConstraint(ImGuiSizeCallbackData* data)
{
    const float ratio = *(float*)data->UserData;
    float w = data->DesiredSize.x, h = data->DesiredSize.y;
    if (w / ratio > h) h = w / ratio; else w = h * ratio;
    data->DesiredSize = ImVec2(w, h);
}

static float budgetMs(const ::FrameStats::Summary& s)
{
    return s.fps > 45.0f ? 1000.0f / 60.0f : 1000.0f / 30.0f;
}

static ImU32 timeColor(float ms, float budget)
{
    if (ms < 0.0f) return IM_COL32(156, 164, 176, 200);
    if (ms <= budget * 1.05f) return IM_COL32(255, 255, 255, 255);
    if (ms <= budget * 1.5f) return IM_COL32(255, 214, 90, 255);
    return IM_COL32(255, 96, 96, 255);
}

static ImU32 fpsColor(float fps, float budget)
{
    return fps > 0.0f ? timeColor(1000.0f / fps, budget) : timeColor(-1.0f, budget);
}

static const char* formatMs(char* buf, size_t n, float ms)
{
    if (ms < 0.0f) return "--";
    snprintf(buf, n, "%.1f ms", (double)ms);
    return buf;
}

static const char* formatCount(char* buf, size_t n, uint32_t v)
{
    if (v >= 1000000u) snprintf(buf, n, "%.2fM", (double)v / 1000000.0);
    else if (v >= 10000u) snprintf(buf, n, "%.1fk", (double)v / 1000.0);
    else snprintf(buf, n, "%u", (unsigned)v);
    return buf;
}

static const char* formatRow(uint32_t bit, char* buf, size_t n,
                             const ::FrameStats::Summary& s, ImU32* color)
{
    const float budget = budgetMs(s);
    *color = IM_COL32(255, 255, 255, 255);
    if (!s.samples) {
        *color = IM_COL32(156, 164, 176, 200);
        return "--";
    }
    switch (bit) {
    case ROW_FPS:
        *color = fpsColor(s.fps, budget);
        snprintf(buf, n, "%.1f", (double)s.fps);
        return buf;
    case ROW_LOW1:
        *color = fpsColor(s.low1Fps, budget);
        snprintf(buf, n, "%.1f fps", (double)s.low1Fps);
        return buf;
    case ROW_FRAME:   *color = timeColor(s.frameMs, budget); return formatMs(buf, n, s.frameMs);
    case ROW_AVG:     *color = timeColor(s.avgMs, budget);   return formatMs(buf, n, s.avgMs);
    case ROW_P50:     *color = timeColor(s.p50Ms, budget);   return formatMs(buf, n, s.p50Ms);
    case ROW_P95:     *color = timeColor(s.p95Ms, budget);   return formatMs(buf, n, s.p95Ms);
    case ROW_P99:     *color = timeColor(s.p99Ms, budget);   return formatMs(buf, n, s.p99Ms);
    case ROW_MAX:     *color = timeColor(s.maxMs, budget);   return formatMs(buf, n, s.maxMs);
    case ROW_CPU:     *color = timeColor(s.cpuMs, budget);   return formatMs(buf, n, s.cpuMs);
    case ROW_WAIT:    *color = IM_COL32(180, 190, 204, 255); return formatMs(buf, n, s.waitMs);
    case ROW_GPU_OVL:
        *color = IM_COL32(180, 190, 204, 255);
        if (s.gpuState == ::FrameStats::GPU_UNAVAILABLE)
            return "n/a";
        return formatMs(buf, n, s.gpuOverlayMs);
    case ROW_GPU:
        switch (s.gpuState) {
        case ::FrameStats::GPU_READY:
            *color = timeColor(s.gpuMs, budget);
            return formatMs(buf, n, s.gpuMs);
        case ::FrameStats::GPU_UNAVAILABLE:
            *color = IM_COL32(156, 164, 176, 200);
            return "n/a";
        default:
            *color = IM_COL32(156, 164, 176, 200);
            return "...";
        }
    case ROW_OVERLAY: return formatMs(buf, n, s.overlayMs);
    case ROW_DRAWS:
        if (!s.drawsSeen) { *color = IM_COL32(156, 164, 176, 200); return "--"; }
        return formatCount(buf, n, s.draws);
    case ROW_TRIS:
        if (!s.drawsSeen) { *color = IM_COL32(156, 164, 176, 200); return "--"; }
        return formatCount(buf, n, s.tris);
    }
    return "--";
}

static void drawGraph(ImDrawList* dl, ImVec2 origin, float scale, float rowY,
                      const ::FrameStats::Summary& s)
{
    const float x0 = origin.x + kLabelX * scale;
    const float x1 = origin.x + kValueRightX * scale;
    const float y0 = origin.y + (rowY - 2.0f) * scale;
    const float y1 = y0 + (kGraphH - 6.0f) * scale;
    const float h = y1 - y0;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 255, 255, 16), 2.0f * scale);

    const float budget = budgetMs(s);
    float yMax = budget * 2.0f;
    if (s.p99Ms * 1.25f > yMax) yMax = s.p99Ms * 1.25f;
    if (yMax > 200.0f) yMax = 200.0f;

    const float guide = y1 - budget / yMax * h;
    dl->AddLine(ImVec2(x0, guide), ImVec2(x1, guide), IM_COL32(255, 255, 255, 60), 1.0f);
    const float guide2 = y1 - budget * 2.0f / yMax * h;
    if (guide2 > y0)
        dl->AddLine(ImVec2(x0, guide2), ImVec2(x1, guide2), IM_COL32(255, 255, 255, 30), 1.0f);

    int n = ::FrameStats::FrameCount();
    if (n > kGraphFrames) n = kGraphFrames;
    if (n <= 0)
        return;
    const float barW = (x1 - x0) / (float)kGraphFrames;
    const float xStart = x0 + (float)(kGraphFrames - n) * barW;

    if (s_graphSeries & GRAPH_FRAME) {
        for (int i = 0; i < n; ++i) {
            const ::FrameStats::Frame& f = ::FrameStats::FrameAt(n - 1 - i);
            float t = f.frameMs / yMax;
            if (t > 1.0f) t = 1.0f;
            const float x = xStart + (float)i * barW;
            ImU32 col = IM_COL32(120, 220, 120, 200);
            if (f.frameMs > budget * 1.5f) col = IM_COL32(255, 96, 96, 220);
            else if (f.frameMs > budget * 1.05f) col = IM_COL32(255, 214, 90, 210);
            dl->AddRectFilled(ImVec2(x, y1 - t * h), ImVec2(x + barW, y1), col);
        }
    }

    ImVec2 pts[kGraphFrames];
    if (s_graphSeries & GRAPH_CPU) {
        int m = 0;
        for (int i = 0; i < n; ++i) {
            const ::FrameStats::Frame& f = ::FrameStats::FrameAt(n - 1 - i);
            if (f.cpuMs < 0.0f) continue;
            float t = f.cpuMs / yMax;
            if (t > 1.0f) t = 1.0f;
            pts[m++] = ImVec2(xStart + ((float)i + 0.5f) * barW, y1 - t * h);
        }
        if (m > 1)
            dl->AddPolyline(pts, m, IM_COL32(90, 170, 255, 230), 0, 1.0f);
    }
    if (s_graphSeries & GRAPH_GPU) {
        int m = 0;
        for (int i = 0; i < n; ++i) {
            const float ms = ::FrameStats::GpuMs(::FrameStats::FrameAt(n - 1 - i).gpuCycles);
            if (ms < 0.0f) continue;
            float t = ms / yMax;
            if (t > 1.0f) t = 1.0f;
            pts[m++] = ImVec2(xStart + ((float)i + 0.5f) * barW, y1 - t * h);
        }
        if (m > 1)
            dl->AddPolyline(pts, m, IM_COL32(255, 120, 255, 230), 0, 1.0f);
    }
}

static float s_layoutK = 0.0f;

static bool moved(float a, float b) { return a - b > 0.5f || b - a > 0.5f; }

void DrawWindow(bool menuActive)
{
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
    const bool open = ImGui::Begin("Frame Stats", menuActive ? &s_win.enabled : nullptr, flags);
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

        const ::FrameStats::Summary& s = ::FrameStats::Current();
        const bool bold = Config::g_settings.boldLetters;
        float rowY = kFirstRowY;
        for (unsigned oi = 0; oi < ROW_COUNT; ++oi) {
            const uint32_t bit = s_order[oi];
            if (!(s_visibleRows & bit)) continue;
            const RowOption* row = rowFor(bit);
            if (!row) continue;
            if (bit == ROW_GRAPH) {
                drawGraph(dl, origin, scale, rowY, s);
                rowY += kGraphH;
                continue;
            }
            ImVec2 lp(origin.x + kLabelX * scale, origin.y + rowY * scale);
            DrawText(dl, font, fontSize, lp, IM_COL32(180, 190, 204, 230), row->shortLabel, bold);
            char valueBuf[48];
            ImU32 valueColor;
            const char* value = formatRow(bit, valueBuf, sizeof(valueBuf), s, &valueColor);
            ImVec2 valueSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, value);
            ImVec2 vp(origin.x + kValueRightX * scale - valueSize.x, lp.y);
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
