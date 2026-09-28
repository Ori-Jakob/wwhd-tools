#include "ui/panels.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/settings.h"
#include "hud/hud_collision.h"
#include "render/scene_depth.h"
#include "hud/hud_frame_stats.h"
#include "tools/camera.h"
#include "hud/hud_game_info.h"
#include "hud/hud_input_viewer.h"
#include "hud/hud_zombie_hover.h"
#include "tools/flycam.h"
#include "tools/mss.h"
#include "tools/stage_control.h"
#include "tools/zombie_hover.h"
#include "ui/quick_access.h"
#include "ui/ui_control.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

#include <math.h>

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

static bool drawModernCam(const Control::Descriptor* d, Control::Surface)
{
    const bool changed = ImGui::Checkbox(d->name, &Config::g_settings.modernCam);
    if (changed)
        Config::MarkDirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Right stick up/down looks up/down instead of zooming, and the camera\n"
                          "stays where you leave it. ZL puts it back behind Link.");
    return changed;
}

static bool drawModernCamOptions(const Control::Descriptor*, Control::Surface)
{
    Config::Settings& s = Config::g_settings;
    if (!s.modernCam)
        return false;
    ImGui::Indent();
    bool changed = ImGui::Checkbox("While sailing##mcam", &s.modernCamSailing);
    ImGui::SetNextItemWidth(180.0f);
    changed |= Field::SliderFloat("Look speed X##mcam", &s.camSensX, 0.25f, 3.0f, "%.2fx");
    ImGui::SetNextItemWidth(180.0f);
    changed |= Field::SliderFloat("Look speed Y##mcam", &s.camSensY, 0.25f, 3.0f, "%.2fx");
    if (changed)
        Config::MarkDirty();
    ImGui::Unindent();
    return changed;
}

static bool drawFov(const Control::Descriptor* d, Control::Surface)
{
    Config::Settings& s = Config::g_settings;
    ImGui::SetNextItemWidth(180.0f);
    bool changed = Field::SliderFloat(d->name, &s.cameraFov, 30.0f, 110.0f, "%.0f");
    if (changed)
        s.cameraFov = floorf(s.cameraFov + 0.5f);
    if (ImGui::IsItemHovered()) {
        const float shown = Tools::Camera::ShownFov();
        if (shown > 0.0f)
            ImGui::SetTooltip("Vertical, in degrees; the game uses 60.\nNow %.1f (game %.1f)",
                              (double)shown, (double)Tools::Camera::GameFov());
        else
            ImGui::SetTooltip("Vertical, in degrees; the game uses 60.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(s.cameraFov == 60.0f);
    if (ImGui::Button("Reset##fov")) {
        s.cameraFov = 60.0f;
        changed = true;
    }
    ImGui::EndDisabled();
    if (changed)
        Config::MarkDirty();
    return changed;
}

static bool drawFovOptions(const Control::Descriptor*, Control::Surface)
{
    Config::Settings& s = Config::g_settings;
    if (s.cameraFov == 60.0f)
        return false;
    ImGui::Indent();
    const bool changed = ImGui::Checkbox("Cutscenes and dialogue too##fov", &s.cameraFovAll);
    if (changed)
        Config::MarkDirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off: only the Link and boat cameras use it; events keep the game's view.");
    ImGui::Unindent();
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

static bool drawZombieHover(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Tools::ZombieHover::IsEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Tools::ZombieHover::SetEnabled(enabled);
        Config::MarkDirty();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rates each hover input (perfect/good/ok/bad) and can heal you instead of a game over.");
    return changed;
}

static bool drawZombieHoverOptions(const Control::Descriptor*, Control::Surface surface)
{
    if (!Tools::ZombieHover::IsEnabled())
        return false;
    ImGui::Indent();
    bool changed = false;
    Ui::WindowState& win = Hud::ZombieHover::State();
    changed |= ImGui::Checkbox("HUD window##zh", &win.enabled);
    if (surface == Control::SURFACE_MENU) {
        ImGui::SameLine();
        Hud::ZombieHover::DrawHistoryButton();
    }
    Config::Settings& s = Config::g_settings;
    changed |= ImGui::Checkbox("Heal instead of game over##zh", &s.zombieHoverHeal);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("At zero life, a quarter heart is given the frame the game would start the\n"
                          "death sequence, so a missed or finished hover never ends in a game over.");
    if (ImGui::Button("1/4 heart, no fairies##zh"))
        Tools::ZombieHover::SetupPractice();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sets life to one quarter heart and empties every bottled fairy, so the\n"
                          "next hit is lethal and nothing revives Link.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!Tools::ZombieHover::CanRestorePractice());
    if (ImGui::Button("Restore##zh"))
        Tools::ZombieHover::RestorePractice();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Puts back the life and the fairies taken, into bottles that are still empty.");
    changed |= ImGui::Checkbox("Auto hover: perfect inputs only##zh", &s.zombieHoverSimPerfect);
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_ZOMBIE_SIM), " (hold ", ")", true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hold it and take a lethal hit: ZL+A gets Link up into the hover, then B is\n"
                          "pressed for you - every 2 frames with this on, otherwise a mix of perfect,\n"
                          "good, ok and bad gaps that still climbs on average.");
    if (changed)
        Config::MarkDirty();
    ImGui::Unindent();
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

    Control::Descriptor zombie = { "tools.zombie_hover", "Zombie Hover",
                                   "Tools / Trainers",
                                   drawZombieHover, drawZombieHoverOptions, nullptr };
    Control::Register(zombie);

    Control::Descriptor flyCam = { "camera.fly_cam", "Fly Cam",
                                   "Tools / Camera",
                                   drawFlyCam, nullptr, nullptr };
    Control::Register(flyCam);

    Control::Descriptor modernCam = { "camera.modern", "Modern camera",
                                      "Tools / Camera",
                                      drawModernCam, drawModernCamOptions, nullptr };
    Control::Register(modernCam);

    Control::Descriptor fov = { "camera.fov", "Field of view",
                                "Tools / Camera",
                                drawFov, drawFovOptions, nullptr };
    Control::Register(fov);

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

    ImGui::SeparatorText("Trainers");
    Control::Draw("tools.zombie_hover", Control::SURFACE_MENU);

    ImGui::SeparatorText("Camera");
    Control::Draw("camera.fly_cam", Control::SURFACE_MENU);
    Control::Draw("camera.modern", Control::SURFACE_MENU);
    Control::Draw("camera.fov", Control::SURFACE_MENU);

    ImGui::SeparatorText("Stage");
    if (ImGui::Button("Reset game"))
        Tools::StageControl::ResetGame();
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_GAME_RESET), "  ", nullptr, true);
    if (ImGui::Button("Reload stage"))
        Tools::StageControl::ReloadStage();
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_STAGE_RELOAD), "  ", nullptr, true);
    DrawCoordinatesItem();
    DrawGreatSeaMapItem();

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
