#include "ui/panels.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/settings.h"
#include "hud/hud_collision.h"
#include "render/scene_depth.h"
#include "hud/hud_frame_stats.h"
#include "hud/hud_game_info.h"
#include "hud/hud_input_viewer.h"
#include "tools/flycam.h"
#include "tools/mss.h"
#include "tools/stage_control.h"
#include "ui/quick_access.h"
#include "ui/ui_control.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static bool drawFlyCam(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Tools::FlyCam::IsEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Tools::FlyCam::SetEnabled(enabled);
        Config::MarkDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_FLY_CAM), "Press ",
                         " to enter Fly Cam");
        ImGui::TextUnformatted("L3 leaves and restores the camera; R3 teleports Link to the camera");
        ImGui::TextUnformatted("D-pad Down un/freeze world");
        ImGui::TextUnformatted("Left/right stick moves/looks, ZR/ZL rise/descend");
        ImGui::TextUnformatted("R/L double/halve speed; hold A/B for max/min speed");
        ImGui::EndTooltip();
    }
    return changed;
}

static bool drawMss(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Tools::Mss::IsEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Tools::Mss::SetEnabled(enabled);
        Config::MarkDirty();
    }
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_MSS), " (hold ", ")", true);
    return changed;
}

static bool drawGameInfo(const Control::Descriptor* d, Control::Surface)
{
    Ui::WindowState& win = Hud::GameInfo::State();
    const bool changed = ImGui::Checkbox(d->name, &win.enabled);
    if (changed)
        Config::MarkDirty();
    return changed;
}

static bool drawFrameStats(const Control::Descriptor* d, Control::Surface)
{
    Ui::WindowState& win = Hud::FrameStats::State();
    const bool changed = ImGui::Checkbox(d->name, &win.enabled);
    if (changed)
        Config::MarkDirty();
    return changed;
}

static bool drawInputViewer(const Control::Descriptor* d, Control::Surface)
{
    Ui::WindowState& win = Hud::InputViewer::State();
    const bool changed = ImGui::Checkbox(d->name, &win.enabled);
    if (changed)
        Config::MarkDirty();
    return changed;
}

static bool drawInputViewerOpacity(const Control::Descriptor*, Control::Surface)
{
    if (!Hud::InputViewer::State().enabled)
        return false;
    ImGui::Indent();
    int percent = (int)(Hud::InputViewer::GetOpacity() * 100.0f + 0.5f);
    ImGui::SetNextItemWidth(180.0f);
    bool changed =
        Field::SliderInt("Opacity##InputViewer", &percent, 0, 100, "%d%%");
    if (changed) {
        Hud::InputViewer::SetOpacity(percent / 100.0f);
        Config::MarkDirty();
    }
    bool ints = Hud::InputViewer::GetIntReadout();
    if (ImGui::Checkbox("Sticks as -127..127##InputViewer", &ints)) {
        Hud::InputViewer::SetIntReadout(ints);
        Config::MarkDirty();
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The units the game's pad layer converts the VPAD and KPAD\n"
                          "floats to, instead of -1.00..1.00.");
    ImGui::Unindent();
    return changed;
}

static bool drawCollision(const Control::Descriptor* d, Control::Surface)
{
    const bool changed = ImGui::Checkbox(d->name, &Config::g_settings.collisionView);
    if (changed)
        Config::MarkDirty();
    return changed;
}

static bool drawCollisionOptions(const Control::Descriptor*, Control::Surface)
{
    Config::Settings& s = Config::g_settings;
    if (!s.collisionView)
        return false;

    ImGui::Indent();
    bool changed = false;
    changed |= ImGui::Checkbox("Attack##col", &s.collisionAt);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Hurt##col", &s.collisionTg);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Body##col", &s.collisionCo);
    changed |= ImGui::Checkbox("Grass/trees##col", &s.collisionMass);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Grass, trees and flowers own no collision object; the game\n"
                          "asks a shared cylinder per instance instead. Shown as outlines.");
    changed |= ImGui::Checkbox("Stage mesh##col", &s.collisionMesh);
    changed |= ImGui::Checkbox("Depth test##col", &s.collisionDepth);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Invert##col", &s.collisionDepthInvert);

    int range = (int)s.collisionRange;
    ImGui::SetNextItemWidth(180.0f);
    if (Field::SliderInt("Hitbox range##col", &range, 500, 20000)) {
        s.collisionRange = (float)range;
        changed = true;
    }
    int meshRange = (int)s.collisionMeshRange;
    ImGui::SetNextItemWidth(180.0f);
    if (Field::SliderInt("Mesh range##col", &meshRange, 250, 20000)) {
        s.collisionMeshRange = (float)meshRange;
        changed = true;
    }
    if (changed)
        Config::MarkDirty();
    ImGui::Unindent();
    return changed;
}

void RegisterControls()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    Control::Descriptor mss = { "tools.mss", "MSS",
                                "Tools / Macros",
                                drawMss, nullptr, nullptr };
    Control::Register(mss);

    Control::Descriptor flyCam = { "camera.fly_cam", "Fly Cam",
                                   "Tools / Camera",
                                   drawFlyCam, nullptr, nullptr };
    Control::Register(flyCam);

    Control::Descriptor gameInfo = { "hud.game_info", "Game Info",
                                     "Tools / HUD",
                                     drawGameInfo, nullptr, nullptr };
    Control::Register(gameInfo);

    Control::Descriptor inputViewer = { "hud.input_viewer", "Input Viewer",
                                        "Tools / HUD",
                                        drawInputViewer, drawInputViewerOpacity,
                                        nullptr };
    Control::Register(inputViewer);

    Control::Descriptor frameStats = { "hud.frame_stats", "Frame Stats",
                                       "Tools / HUD",
                                       drawFrameStats, nullptr, nullptr };
    Control::Register(frameStats);

    Control::Descriptor collision = { "hud.collision", "Collision viewer",
                                      "Tools / HUD",
                                      drawCollision, drawCollisionOptions,
                                      nullptr };
    Control::Register(collision);
}

void DrawTools()
{
    QuickAccess::DrawMenuItem();

    ImGui::SeparatorText("Save data");
    DrawInventoryItem();
    DrawSaveStatesItem();
    DrawSaveLoaderItem();

    ImGui::SeparatorText("Macros");
    Control::Draw("tools.mss", Control::SURFACE_MENU);

    ImGui::SeparatorText("Camera");
    Control::Draw("camera.fly_cam", Control::SURFACE_MENU);

    ImGui::SeparatorText("Stage");
    if (ImGui::Button("Reset game"))
        Tools::StageControl::ResetGame();
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_GAME_RESET), "  ", nullptr, true);
    if (ImGui::Button("Reload stage"))
        Tools::StageControl::ReloadStage();
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_STAGE_RELOAD), "  ", nullptr, true);

    ImGui::SeparatorText("HUD");
    Control::Draw("hud.game_info", Control::SURFACE_MENU);
    ImGui::SameLine();
    Hud::GameInfo::DrawSettingsButton();
    Control::Draw("hud.input_viewer", Control::SURFACE_MENU);
    Control::Draw("hud.frame_stats", Control::SURFACE_MENU);
    ImGui::SameLine();
    Hud::FrameStats::DrawSettingsButton();
    Control::Draw("hud.collision", Control::SURFACE_MENU);
}
}
}
