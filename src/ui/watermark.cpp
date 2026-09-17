#include "ui/watermark.h"

#include "core/config.h"
#include "core/input.h"
#include "core/settings.h"
#include "render/image.h"
#include "render/renderer.h"

#include "imgui.h"

#include "icon_64_bin.h"

#include <math.h>

namespace Ui {
namespace Watermark {
static const float kLogicalWidth  = 1920.0f;
static const float kLogicalHeight = 1080.0f;
static const float kHitPad        = 12.0f;
static const float kDragSlop      = 0.012f;
static const int   kMissFrames    = 4;
static const int   kHoldOffFrames = 8;

static bool  s_down = false;
static bool  s_dragging = false;
static int   s_missed = 0;
static int   s_holdOff = 0;
static float s_grabX = 0.0f;
static float s_grabY = 0.0f;
static float s_startX = 0.0f;
static float s_startY = 0.0f;

static float clamp01(float v, float span)
{
    const float hi = 1.0f - span;
    if (v < 0.0f) return 0.0f;
    if (v > hi)   return hi < 0.0f ? 0.0f : hi;
    return v;
}

static float sideLogical()
{
    float side = Config::g_settings.watermarkSize;
    if (side < 32.0f)  side = 32.0f;
    if (side > 320.0f) side = 320.0f;
    return side;
}

static int alpha255()
{
    float a = Config::g_settings.watermarkOpacity;
    if (a < 0.05f) a = 0.05f;
    if (a > 1.0f)  a = 1.0f;
    return (int)(a * 255.0f + 0.5f);
}

static float spanX() { return sideLogical() / kLogicalWidth; }
static float spanY() { return sideLogical() / kLogicalHeight; }

bool HitTest(float x, float y)
{
    const Config::Settings& s = Config::g_settings;
    const float padX = kHitPad / kLogicalWidth;
    const float padY = kHitPad / kLogicalHeight;
    return x >= s.watermarkX - padX && x <= s.watermarkX + spanX() + padX &&
           y >= s.watermarkY - padY && y <= s.watermarkY + spanY() + padY;
}

bool IsInteracting() { return s_down || s_holdOff > 0; }

bool Tick(bool acceptTouch)
{
    Config::Settings& s = Config::g_settings;

    if (!s.watermarkEnabled) {
        s_down = false;
        s_dragging = false;
        s_missed = 0;
        s_holdOff = 0;
        return false;
    }

    float tx = 0.0f, ty = 0.0f;
    const bool touched = acceptTouch && Input::GetTouchPoint(&tx, &ty);

    if (!s_down) {
        if (s_holdOff > 0)
            --s_holdOff;
        if (touched && HitTest(tx, ty)) {
            s_down = true;
            s_dragging = false;
            s_missed = 0;
            s_startX = tx;
            s_startY = ty;
            s_grabX = tx - s.watermarkX;
            s_grabY = ty - s.watermarkY;
        }
        return false;
    }

    if (touched) {
        s_missed = 0;
        if (!s_dragging &&
            (fabsf(tx - s_startX) > kDragSlop || fabsf(ty - s_startY) > kDragSlop))
            s_dragging = true;
        if (s_dragging) {
            s.watermarkX = clamp01(tx - s_grabX, spanX());
            s.watermarkY = clamp01(ty - s_grabY, spanY());
        }
        return false;
    }

    if (++s_missed < kMissFrames)
        return false;

    const bool tapped = !s_dragging;
    if (s_dragging)
        Config::MarkDirty();
    s_down = false;
    s_dragging = false;
    s_missed = 0;
    s_holdOff = kHoldOffFrames;
    return tapped;
}

void Draw()
{
    Renderer::SetGamePadOnlyList(0);

    if (!Config::g_settings.watermarkEnabled)
        return;

    const Image::Texture* icon = Image::Load(icon_64_bin, icon_64_bin_size);
    if (!icon)
        return;

    const Config::Settings& s = Config::g_settings;
    const ImGuiIO& io = ImGui::GetIO();
    const float w = io.DisplaySize.x;
    const float h = io.DisplaySize.y;
    if (w <= 0.0f || h <= 0.0f)
        return;

    const ImVec2 pos(s.watermarkX * w, s.watermarkY * h);
    const float side = sideLogical();
    const ImVec2 size(side * (w / kLogicalWidth), side * (h / kLogicalHeight));

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::Begin("##wwhd_watermark", 0,
                                      ImGuiWindowFlags_NoDecoration |
                                      ImGuiWindowFlags_NoBackground |
                                      ImGuiWindowFlags_NoInputs |
                                      ImGuiWindowFlags_NoNav |
                                      ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoSavedSettings);
    if (visible) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b(pos.x + size.x, pos.y + size.y);
        const int a = alpha255();
        dl->AddRectFilled(pos, b, IM_COL32(0, 0, 0, a));
        dl->AddImage(icon->id, pos, b, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                     IM_COL32(255, 255, 255, a));
        Renderer::SetGamePadOnlyList(dl);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
}
}
