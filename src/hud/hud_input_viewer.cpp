#include "hud/hud_input_viewer.h"

#include "core/config.h"
#include "core/input.h"
#include "core/settings.h"
#include "hud/hud_text.h"
#include "render/renderer.h"

#include "imgui.h"

#include <float.h>
#include <stdio.h>

namespace Hud {
namespace InputViewer {
static const float kBaseW = 360.0f;
static const float kBaseH = 210.0f;
static const float kAspect = kBaseW / kBaseH;
static const float kReadoutSize = 15.0f;
static const float kDefaultW = 330.0f;

static Ui::WindowState s_win = { false, 40.0f, 260.0f, kDefaultW, kDefaultW / kAspect };
static float s_opacity = 1.0f;
static bool s_applyPos = false, s_applySize = false;

struct DrawCtx {
    ImDrawList* dl;
    ImFont* font;
    ImVec2 origin;
    float scale;
};

static ImVec2 p(const DrawCtx& c, float x, float y)
{
    return ImVec2(c.origin.x + x * c.scale, c.origin.y + y * c.scale);
}

static float sc(const DrawCtx& c, float v) { return v * c.scale; }
static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static ImU32 rgba(int r, int g, int b, int a)
{
    return IM_COL32(r, g, b, a);
}

static ImU32 buttonColor(bool down, bool pressed)
{
    if (pressed) return rgba(255, 214, 92, 255);
    if (down) return rgba(64, 207, 142, 255);
    return rgba(42, 47, 55, 255);
}

static void centered(const DrawCtx& c, ImVec2 center, const char* text,
                     ImU32 color, float size)
{
    const float fontSize = sc(c, size);
    ImVec2 ts = c.font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    DrawText(c.dl, c.font, fontSize,
                 ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                 color, text, Config::g_settings.boldLetters);
}

static void drawButton(const DrawCtx& c, float x, float y, float r,
                       const char* label, bool down, bool pressed)
{
    const ImVec2 center = p(c, x, y);
    c.dl->AddCircleFilled(center, sc(c, r), buttonColor(down, pressed), 24);
    c.dl->AddCircle(center, sc(c, r), rgba(208, 216, 226, 180),
                    24, sc(c, 1.5f));
    centered(c, center, label,
             down || pressed ? rgba(8, 14, 14, 255)
                             : rgba(220, 226, 236, 255),
             r * 0.85f);
}

static void drawPill(const DrawCtx& c, float x1, float y1, float x2, float y2,
                     const char* label, bool down, bool pressed)
{
    const ImVec2 min = p(c, x1, y1), max = p(c, x2, y2);
    c.dl->AddRectFilled(min, max, buttonColor(down, pressed), sc(c, 6.0f));
    c.dl->AddRect(min, max, rgba(208, 216, 226, 165), sc(c, 6.0f),
                  0, sc(c, 1.4f));
    centered(c, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), label,
             rgba(225, 231, 239, 255), 11.0f);
}

static void drawStick(const DrawCtx& c, float x, float y, float sx, float sy,
                      bool clicked, bool pressed)
{
    const ImVec2 center = p(c, x, y);
    const float radius = sc(c, 18.0f);
    c.dl->AddCircleFilled(center, radius, rgba(26, 31, 39, 255), 32);
    c.dl->AddCircle(center, radius,
                    pressed ? rgba(255, 214, 92, 255)
                            : clicked ? rgba(64, 207, 142, 255)
                                      : rgba(182, 194, 210, 150),
                    32, sc(c, clicked ? 2.7f : 1.5f));
    c.dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y),
                  rgba(130, 142, 158, 75));
    c.dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius),
                  rgba(130, 142, 158, 75));
    sx = clampf(sx, -1.0f, 1.0f);
    sy = clampf(sy, -1.0f, 1.0f);
    const float dotR = sc(c, 4.3f);
    const ImVec2 dot(center.x + sx * (radius - dotR - sc(c, 2.5f)),
                     center.y - sy * (radius - dotR - sc(c, 2.5f)));
    c.dl->AddCircleFilled(dot, dotR, rgba(75, 192, 235, 255), 20);
}

static void dpadPart(const DrawCtx& c, float x1, float y1, float x2, float y2,
                     bool down, bool pressed)
{
    c.dl->AddRectFilled(p(c, x1, y1), p(c, x2, y2),
                        buttonColor(down, pressed), sc(c, 3.0f));
}

static void drawDpad(const DrawCtx& c, float x, float y,
                     uint32_t held, uint32_t pressed)
{
    dpadPart(c, x - 5.5f, y - 22.0f, x + 5.5f, y - 4.0f,
             held & Input::BTN_UP, pressed & Input::BTN_UP);
    dpadPart(c, x - 5.5f, y + 4.0f, x + 5.5f, y + 22.0f,
             held & Input::BTN_DOWN, pressed & Input::BTN_DOWN);
    dpadPart(c, x - 22.0f, y - 5.5f, x - 4.0f, y + 5.5f,
             held & Input::BTN_LEFT, pressed & Input::BTN_LEFT);
    dpadPart(c, x + 4.0f, y - 5.5f, x + 22.0f, y + 5.5f,
             held & Input::BTN_RIGHT, pressed & Input::BTN_RIGHT);
}

static const char* sourceName(uint32_t mask)
{
    if (!mask) return "No controller";
    if ((mask & (mask - 1)) != 0) return "Mixed";
    if (mask & Input::SOURCE_GAMEPAD) return "GamePad";
    if (mask & Input::SOURCE_PRO) return "Pro Controller";
    if (mask & Input::SOURCE_CLASSIC) return "Classic Controller";
    return "Controller";
}

static void aspectConstraint(ImGuiSizeCallbackData* data)
{
    const float ratio = *(float*)data->UserData;
    float w = data->DesiredSize.x, h = data->DesiredSize.y;
    if (w / ratio > h) h = w / ratio; else w = h * ratio;
    data->DesiredSize = ImVec2(w, h);
}

Ui::WindowState& State() { return s_win; }

void ApplyState()
{
    s_win.w = clampf(s_win.w, MIN_WIDTH, MAX_WIDTH);
    s_win.h = s_win.w / kAspect;
    s_applyPos = s_applySize = true;
}

static bool s_intReadout = false;

static int toPadUnits(float v)
{
    const int i = (int)(v * 127.0f + (v >= 0.0f ? 0.5f : -0.5f));
    return i < -127 ? -127 : (i > 127 ? 127 : i);
}

void ResetToDefaults()
{
    s_opacity = 1.0f;
    s_intReadout = false;
    s_win.enabled = false;
    s_win.x = 40.0f;
    s_win.y = 260.0f;
    s_win.w = kDefaultW;
    ApplyState();
}

float GetOpacity() { return s_opacity; }
void SetOpacity(float opacity) { s_opacity = clampf(opacity, 0.0f, 1.0f); }

bool GetIntReadout() { return s_intReadout; }
void SetIntReadout(bool on) { s_intReadout = on; }

static float s_layoutK = 0.0f;

static bool moved(float a, float b) { return a - b > 0.5f || b - a > 0.5f; }

void DrawWindow(bool menuActive)
{
    if (!s_win.enabled)
        return;

    const float k = 1.0f / Renderer::UiScale();
    if (k != s_layoutK) {
        s_layoutK = k;
        s_applyPos = s_applySize = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const Input::Snapshot& snap = Input::Current();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowSizeConstraints(ImVec2(MIN_WIDTH * k, MIN_WIDTH * k / kAspect),
                                        ImVec2(MAX_WIDTH * k, MAX_WIDTH * k / kAspect),
                                        aspectConstraint, (void*)&kAspect);
    ImGui::SetNextWindowPos(ImVec2(s_win.x * k, s_win.y * k),
                            s_applyPos ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(s_win.w * k, s_win.h * k),
                             s_applySize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    s_applyPos = s_applySize = false;
    const float background = Config::g_settings.overlayOpacity;
    ImGui::SetNextWindowBgAlpha(Renderer::BackdropAlpha(0.42f * background));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9.0f * k);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * k, style.FramePadding.y * k));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    const bool wasEnabled = s_win.enabled;
    const float oldX = s_win.x, oldY = s_win.y, oldW = s_win.w, oldH = s_win.h;
    bool open = ImGui::Begin("Input Viewer", menuActive ? &s_win.enabled : nullptr, flags);
    ImGui::SetWindowFontScale(k);
    Renderer::MarkGameScreenOnly(ImGui::GetWindowDrawList());
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    s_win.x = wp.x / k; s_win.y = wp.y / k; s_win.w = ws.x / k; s_win.h = ws.y / k;

    if (open) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        if (avail.y / kBaseH < scale) scale = avail.y / kBaseH;
        DrawCtx c = { ImGui::GetWindowDrawList(), ImGui::GetFont(), ImVec2(), scale };
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        c.origin = ImVec2(cursor.x + (avail.x - kBaseW * scale) * 0.5f,
                          cursor.y + (avail.y - kBaseH * scale) * 0.5f);
        c.dl->AddRectFilled(c.origin, p(c, kBaseW, kBaseH),
                            rgba(7, 10, 14, (int)(190.0f * background + 0.5f)), sc(c, 9));
        c.dl->AddRectFilled(p(c, 42, 67), p(c, 318, 177),
                            rgba(24, 29, 37, (int)(238.0f * s_opacity + 0.5f)), sc(c, 45));

        const char* source = sourceName(snap.sourceMask);
        const float fs = sc(c, 10.0f);
        ImVec2 sz = c.font->CalcTextSizeA(fs, FLT_MAX, 0.0f, source);
        DrawText(c.dl, c.font, fs, ImVec2(p(c, 345, 11).x - sz.x, p(c, 0, 11).y),
                     snap.valid ? rgba(116, 220, 164, 255)
                                : rgba(170, 176, 185, 255),
                     source, Config::g_settings.boldLetters);

        drawPill(c, 67, 56, 112, 68, "ZL", snap.held & Input::BTN_ZL, snap.pressed & Input::BTN_ZL);
        drawPill(c, 248, 56, 293, 68, "ZR", snap.held & Input::BTN_ZR, snap.pressed & Input::BTN_ZR);
        drawPill(c, 79, 70, 119, 82, "L", snap.held & Input::BTN_L, snap.pressed & Input::BTN_L);
        drawPill(c, 241, 70, 281, 82, "R", snap.held & Input::BTN_R, snap.pressed & Input::BTN_R);
        drawStick(c, 87, 105, snap.lx, snap.ly, snap.held & Input::BTN_L3, snap.pressed & Input::BTN_L3);
        drawStick(c, 273, 105, snap.rx, snap.ry, snap.held & Input::BTN_R3, snap.pressed & Input::BTN_R3);
        drawDpad(c, 125, 142, snap.held, snap.pressed);
        drawButton(c, 244, 127, 8.5f, "X", snap.held & Input::BTN_X, snap.pressed & Input::BTN_X);
        drawButton(c, 226, 142, 8.5f, "Y", snap.held & Input::BTN_Y, snap.pressed & Input::BTN_Y);
        drawButton(c, 261, 142, 8.5f, "A", snap.held & Input::BTN_A, snap.pressed & Input::BTN_A);
        drawButton(c, 244, 159, 8.5f, "B", snap.held & Input::BTN_B, snap.pressed & Input::BTN_B);
        drawPill(c, 156, 98, 174, 110, "-", snap.held & Input::BTN_MINUS, snap.pressed & Input::BTN_MINUS);
        drawPill(c, 186, 98, 204, 110, "+", snap.held & Input::BTN_PLUS, snap.pressed & Input::BTN_PLUS);

        char readout[48];
        if (s_intReadout)
            snprintf(readout, sizeof(readout), "L  %d, %d", toPadUnits(snap.lx), toPadUnits(snap.ly));
        else
            snprintf(readout, sizeof(readout), "L  %.2f, %.2f", (double)snap.lx, (double)snap.ly);
        centered(c, p(c, 87, 193), readout, rgba(150, 205, 240, 235), kReadoutSize);
        if (s_intReadout)
            snprintf(readout, sizeof(readout), "R  %d, %d", toPadUnits(snap.rx), toPadUnits(snap.ry));
        else
            snprintf(readout, sizeof(readout), "R  %.2f, %.2f", (double)snap.rx, (double)snap.ry);
        centered(c, p(c, 273, 193), readout, rgba(150, 205, 240, 235), kReadoutSize);
        ImGui::Dummy(avail);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    if (menuActive && (wasEnabled != s_win.enabled || moved(oldX, s_win.x) || moved(oldY, s_win.y) ||
                       moved(oldW, s_win.w) || moved(oldH, s_win.h)))
        Config::MarkDirty();
}
}
}
