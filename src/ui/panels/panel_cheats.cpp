#include "ui/panels.h"

#include "cheats/cheat_movement.h"
#include "cheats/cheat_sailing.h"
#include "cheats/cheat_status.h"
#include "cheats/cheat_storage.h"
#include "cheats/cheat_text.h"
#include "cheats/cheats.h"
#include "core/config.h"
#include "core/hotkeys.h"
#include "libwwhd/libwwhd.h"
#include "ui/notifications.h"
#include "ui/ui_control.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static const float kSliderWidth = 180.0f;

static void drawHoldHint(Hotkeys::Id id)
{
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(id), " (hold ", ")", true);
}

static bool drawStatusPin(const Control::Descriptor* d, Control::Surface)
{
    const Cheats::Status::Pin pin = (Cheats::Status::Pin)d->token;
    bool enabled = Cheats::Status::IsEnabled(pin);
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Status::SetEnabled(pin, enabled);
        Config::MarkDirty();
    }
    return changed;
}

static bool drawMoonJump(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Movement::MoonJumpEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Movement::SetMoonJumpEnabled(enabled);
        Config::MarkDirty();
    }
    drawHoldHint(Hotkeys::HOTKEY_MOON_JUMP);
    return changed;
}

static bool drawSpeedModifiers(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Movement::SpeedModifiersEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Movement::SetSpeedModifiersEnabled(enabled);
        Config::MarkDirty();
    }
    return changed;
}

static bool speedSlider(const char* label, Hotkeys::Id hotkey, int value,
                        int lo, int hi, int* out)
{
    ImGui::SetNextItemWidth(kSliderWidth);
    const bool changed = Field::SliderInt(label, &value, lo, hi, "%dx");
    if (changed)
        *out = value;
    drawHoldHint(hotkey);
    return changed;
}

static bool drawSpeedOptions(const Control::Descriptor*, Control::Surface)
{
    if (!Cheats::Movement::SpeedModifiersEnabled())
        return false;

    using namespace Cheats;
    ImGui::Indent();
    bool changed = false;
    int value = 0;

    if (speedSlider("Move speed", Hotkeys::HOTKEY_MOVE_BOOST,
                    Movement::MoveMultiplier(), Movement::MOVE_MULTIPLIER_MIN,
                    Movement::MOVE_MULTIPLIER_MAX, &value)) {
        Movement::SetMoveMultiplier(value);
        changed = true;
    }
    if (speedSlider("Crawl speed", Hotkeys::HOTKEY_CRAWL_BOOST,
                    Movement::CrawlMultiplier(), Movement::CRAWL_MULTIPLIER_MIN,
                    Movement::CRAWL_MULTIPLIER_MAX, &value)) {
        Movement::SetCrawlMultiplier(value);
        changed = true;
    }
    if (speedSlider("Swim speed", Hotkeys::HOTKEY_SWIM_BOOST,
                    Movement::SwimMultiplier(), Movement::SPEED_MULTIPLIER_MIN,
                    Movement::SPEED_MULTIPLIER_MAX, &value)) {
        Movement::SetSwimMultiplier(value);
        changed = true;
    }

    bool controls = Movement::SwimSpeedControlsEnabled();
    if (ImGui::Checkbox("Instant swim speed controls", &controls)) {
        Movement::SetSwimSpeedControlsEnabled(controls);
        changed = true;
    }
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_SWIM_FULL_SPEED),
                     " (", " full, ", true);
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_SWIM_STOP),
                     nullptr, " stop)", true);

    if (changed)
        Config::MarkDirty();
    ImGui::Unindent();
    return changed;
}

static bool drawLaunch(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Movement::LaunchEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Movement::SetLaunchEnabled(enabled);
        Config::MarkDirty();
    }
    drawHoldHint(Hotkeys::HOTKEY_LAUNCH);
    return changed;
}

static bool drawLaunchOptions(const Control::Descriptor*, Control::Surface)
{
    using namespace Cheats;
    if (!Movement::LaunchEnabled())
        return false;

    ImGui::Indent();
    bool changed = false;

    int velocity = Movement::LaunchVelocity();
    ImGui::SetNextItemWidth(kSliderWidth);
    if (Field::SliderInt("Launch velocity", &velocity,
                         Movement::LAUNCH_VELOCITY_MIN,
                         Movement::LAUNCH_VELOCITY_MAX)) {
        Movement::SetLaunchVelocity(velocity);
        changed = true;
    }

    float ramp = Movement::LaunchRampSeconds();
    ImGui::SetNextItemWidth(kSliderWidth);
    if (Field::SliderFloat("Ramp time", &ramp, Movement::LAUNCH_RAMP_MIN_SECONDS,
                           Movement::LAUNCH_RAMP_MAX_SECONDS, "%.1f s")) {
        Movement::SetLaunchRampSeconds(ramp);
        changed = true;
    }

    bool reversed = Movement::LaunchReversed();
    if (ImGui::Checkbox("Negative velocity", &reversed)) {
        Movement::SetLaunchReversed(reversed);
        changed = true;
    }
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_LAUNCH_STOP), "  (", " stops)", true);
    ImGui::TextDisabled("Launches the boat instead while sailing.");

    if (changed)
        Config::MarkDirty();
    ImGui::Unindent();
    return changed;
}

static bool drawWindOptions(const Control::Descriptor*, Control::Surface)
{
    using namespace Cheats::Sailing;

    ImGui::Indent();
    ImGui::BeginDisabled(AutoWindEnabled());

    const int current = WindDirection();
    bool changed = false;
    ImGui::SetNextItemWidth(kSliderWidth);
    if (ImGui::BeginCombo("Wind direction", WindDirectionName(current))) {
        for (int i = 0; i < WIND_DIR_COUNT; ++i) {
            const bool selected = i == current;
            if (ImGui::Selectable(WindDirectionName(i), selected)) {
                SetWindDirection(i);
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::EndDisabled();
    if (changed)
        Config::MarkDirty();
    ImGui::Unindent();
    return changed;
}

static bool drawAutoWind(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Sailing::AutoWindEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Sailing::SetAutoWindEnabled(enabled);
        Config::MarkDirty();
    }
    return changed;
}

static bool drawBoatMoonJump(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Sailing::MoonJumpEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Sailing::SetMoonJumpEnabled(enabled);
        Config::MarkDirty();
    }
    drawHoldHint(Hotkeys::HOTKEY_BOAT_MOON_JUMP);
    return changed;
}

static bool drawBoatBoost(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Sailing::BoostEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Sailing::SetBoostEnabled(enabled);
        Config::MarkDirty();
    }
    drawHoldHint(Hotkeys::HOTKEY_BOAT_BOOST);
    return changed;
}

static bool drawBoatMultiplier(const Control::Descriptor*, Control::Surface)
{
    if (!Cheats::Sailing::BoostEnabled())
        return false;
    ImGui::Indent();
    int value = Cheats::Sailing::BoostMultiplier();
    ImGui::SetNextItemWidth(kSliderWidth);
    const bool changed = Field::SliderInt("Boat speed", &value,
                                          Cheats::Sailing::BOOST_MULTIPLIER_MIN,
                                          Cheats::Sailing::BOOST_MULTIPLIER_MAX,
                                          "%dx");
    if (changed) {
        Cheats::Sailing::SetBoostMultiplier(value);
        Config::MarkDirty();
    }
    ImGui::Unindent();
    return changed;
}

static bool drawTextAdvance(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Text::AutoAdvanceEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Text::SetAutoAdvanceEnabled(enabled);
        Config::MarkDirty();
    }
    drawHoldHint(Hotkeys::HOTKEY_TEXT_ADVANCE);
    if (!wwhd_regionResolved)
        ImGui::TextDisabled("Unavailable in this build.");
    return changed;
}

static bool drawTeleportToBoat(const Control::Descriptor* d, Control::Surface)
{
    ImGui::BeginDisabled(!daShip_isAlive());
    if (ImGui::Button(d->name))
        Cheats::Sailing::TeleportLinkToBoat();
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_TELEPORT_TO_BOAT),
                     "  ", nullptr, true);
    return false;
}

static bool drawStorage(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Storage::Enabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Storage::SetEnabled(enabled);
        Config::MarkDirty();
    }
    return changed;
}

static bool storageRow(const char* label, bool isSet,
                       const char* setLabel, const char* restoreLabel)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    const bool pressed = ImGui::Button(isSet ? restoreLabel : setLabel);
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    return pressed;
}

static bool drawStorageOptions(const Control::Descriptor*, Control::Surface)
{
    namespace St = Cheats::Storage;
    if (!St::Enabled())
        return false;

    const bool armed = St::IsArmed();
    const St::Collision collision = St::CurrentCollision();
    const bool chest = collision == St::COLLISION_CHEST;
    const bool door  = collision == St::COLLISION_DOOR;

    ImGui::Indent();
    if (ImGui::BeginTable("##storageRows", 3, ImGuiTableFlags_SizingFixedFit)) {
        if (storageRow("Storage", armed, "Set##storage", "Restore##storage")) {
            if (armed)
                St::Clear();
            else
                St::Arm();
        }
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_STORAGE_ARM), "(", ", ", true);
        ImGui::SameLine(0.0f, 0.0f);
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_STORAGE_CLEAR), nullptr, ")", true);

        if (storageRow("Chest", chest, "Set##chest", "Restore##chest")) {
            if (chest)
                St::RestoreCollision();
            else
                St::SetCollision(St::COLLISION_CHEST);
        }
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_STORAGE_CHEST), "(", ")", true);

        if (storageRow("Door", door, "Set##door", "Restore##door")) {
            if (door)
                St::RestoreCollision();
            else
                St::SetCollision(St::COLLISION_DOOR);
        }
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_STORAGE_DOOR), "(", ")", true);

        ImGui::EndTable();
    }
    ImGui::Unindent();
    return false;
}

void RegisterCheatControls()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    static const char kStatusPath[]  = "Mods / Cheats / Always full";
    static const char kFootPath[]    = "Mods / Cheats / On foot";
    static const char kSailPath[]    = "Mods / Cheats / Sailing";
    static const char kQolSailPath[] = "Mods / QoL / Sailing";
    static const char kQolTextPath[] = "Mods / QoL / Text";
    static const char kGlitchPath[]  = "Mods / Glitches";

    for (int i = 0; i < Cheats::Status::PIN_COUNT; ++i) {
        static const char* const kIds[Cheats::Status::PIN_COUNT] = {
            "cheat.infinite_hearts", "cheat.infinite_magic",
            "cheat.infinite_ammo",   "cheat.infinite_rupees",
            "cheat.infinite_swim_stamina", "cheat.invincible",
        };
        const Cheats::Status::Pin pin = (Cheats::Status::Pin)i;
        Control::Descriptor d = { kIds[i], Cheats::Status::Name(pin), kStatusPath,
                                  drawStatusPin, nullptr, nullptr, i };
        Control::Register(d);
    }

    Control::Descriptor moonJump = { "cheat.moon_jump", "Moon Jump", kFootPath,
                                     drawMoonJump, nullptr, nullptr, 0 };
    Control::Register(moonJump);

    Control::Descriptor speed = { "cheat.speed_modifiers", "Speed modifiers",
                                  kFootPath, drawSpeedModifiers,
                                  drawSpeedOptions, nullptr, 0 };
    Control::Register(speed);

    Control::Descriptor launch = { "cheat.launch", "Launch Link", kFootPath,
                                   drawLaunch, drawLaunchOptions, nullptr, 0 };
    Control::Register(launch);

    Control::Descriptor wind = { "cheat.auto_wind", "Automatic wind direction",
                                 kQolSailPath, drawAutoWind, drawWindOptions,
                                 nullptr, 0 };
    Control::Register(wind);

    Control::Descriptor boatJump = { "cheat.boat_moon_jump", "Boat Moon Jump",
                                     kSailPath, drawBoatMoonJump, nullptr,
                                     nullptr, 0 };
    Control::Register(boatJump);

    Control::Descriptor boatBoost = { "cheat.boat_boost", "Hold for boat speed",
                                      kSailPath, drawBoatBoost,
                                      drawBoatMultiplier, nullptr, 0 };
    Control::Register(boatBoost);

    Control::Descriptor warp = { "cheat.teleport_to_boat", "Teleport to boat",
                                 kQolSailPath, drawTeleportToBoat, nullptr,
                                 nullptr, 0 };
    Control::Register(warp);

    Control::Descriptor textAdvance = { "cheat.text_auto_advance",
                                        "Hold to advance text", kQolTextPath,
                                        drawTextAdvance, nullptr, nullptr, 0 };
    Control::Register(textAdvance);

    Control::Descriptor storage = { "cheat.storage", "Storage", kGlitchPath,
                                    drawStorage, drawStorageOptions, nullptr, 0 };
    Control::Register(storage);
}

static void drawCheatsSection()
{
    Control::Draw("cheat.infinite_hearts", Control::SURFACE_MENU);
    Control::Draw("cheat.infinite_magic",  Control::SURFACE_MENU);
    Control::Draw("cheat.infinite_ammo",   Control::SURFACE_MENU);
    Control::Draw("cheat.infinite_rupees", Control::SURFACE_MENU);
    Control::Draw("cheat.infinite_swim_stamina", Control::SURFACE_MENU);
    Control::Draw("cheat.invincible",      Control::SURFACE_MENU);

    ImGui::Separator();
    Control::Draw("cheat.moon_jump",       Control::SURFACE_MENU);
    Control::Draw("cheat.speed_modifiers", Control::SURFACE_MENU);
    Control::Draw("cheat.launch",          Control::SURFACE_MENU);

    ImGui::Separator();
    Control::Draw("cheat.boat_moon_jump",  Control::SURFACE_MENU);
    Control::Draw("cheat.boat_boost",      Control::SURFACE_MENU);
}

static void drawGlitchesSection()
{
    Control::Draw("cheat.storage", Control::SURFACE_MENU);
}

static void drawQolSection()
{
    Control::Draw("cheat.auto_wind",        Control::SURFACE_MENU);
    Control::Draw("cheat.teleport_to_boat", Control::SURFACE_MENU);
    ImGui::TextDisabled("%s", daShip_isAlive() ? "Boat is spawned."
                                               : "No boat in this area.");

    ImGui::Separator();
    Control::Draw("cheat.text_auto_advance", Control::SURFACE_MENU);
}

void DrawMods()
{
    if (ImGui::BeginMenu("Cheats"))   { drawCheatsSection();   ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Glitches")) { drawGlitchesSection(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("QoL"))      { drawQolSection();      ImGui::EndMenu(); }

    ImGui::Separator();
    if (ImGui::Button("Turn all mods off")) {
        Cheats::ResetToDefaults();
        Config::MarkDirty();
        Notifications::Show(Notifications::Info, "Mods", "Every mod is off");
    }
}
}
}
