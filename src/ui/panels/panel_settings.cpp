#include "ui/panels.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/settings.h"
#include "libwwhd/libwwhd.h"
#include "ui/notifications.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static const float kItemWidth = 190.0f;

static bool s_confirmReset = false;

void DrawSettings()
{
    Config::Settings& s = Config::g_settings;

    int backgroundPercent = (int)(s.overlayOpacity * 100.0f + 0.5f);
    ImGui::SetNextItemWidth(kItemWidth);
    if (Field::SliderInt("Background opacity", &backgroundPercent, 0, 100, "%d%%")) {
        s.overlayOpacity = backgroundPercent / 100.0f;
        Config::MarkDirty();
    }
    if (ImGui::Checkbox("Bold HUD letters", &s.boldLetters))
        Config::MarkDirty();

    static int  s_scalePercent = 0;
    static bool s_scaleHeld = false;
    if (!s_scaleHeld)
        s_scalePercent = (int)(s.uiScale * 100.0f + 0.5f);
    ImGui::SetNextItemWidth(kItemWidth);
    Field::SliderInt("UI scale", &s_scalePercent, 100, 200, "%d%%");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Larger widgets and text on both screens; the GamePad's touch\n"
                          "targets grow with them.");
    s_scaleHeld = ImGui::IsItemActive() || Field::IsSteeringSlider();
    const float wantedScale = (float)s_scalePercent / 100.0f;
    if (!s_scaleHeld && wantedScale != s.uiScale) {
        s.uiScale = wantedScale;
        Config::MarkDirty();
    }

    ImGui::Separator();

    if (ImGui::Checkbox("Show toasts", &s.toastsEnabled))
        Config::MarkDirty();
    ImGui::SetNextItemWidth(kItemWidth);
    if (Field::SliderFloat("Toast seconds", &s.toastSeconds, 0.5f, 15.0f, "%.1f"))
        Config::MarkDirty();
    ImGui::TextUnformatted("Drawn Screen");
    ImGui::SameLine();
    int drawn = (int)s.drawnScreen;
    bool drawnChanged = false;
    drawnChanged |= ImGui::RadioButton("Both", &drawn, Config::DRAWN_SCREEN_BOTH);
    ImGui::SameLine();
    drawnChanged |= ImGui::RadioButton("Gamepad", &drawn, Config::DRAWN_SCREEN_GAMEPAD);
    ImGui::SameLine();
    drawnChanged |= ImGui::RadioButton("TV", &drawn, Config::DRAWN_SCREEN_TV);
    if (drawnChanged) {
        s.drawnScreen = (uint32_t)drawn;
        Config::MarkDirty();
    }
    if (s.drawnScreen == Config::DRAWN_SCREEN_GAMEPAD) {
        if (ImGui::Checkbox("Render HUD windows to the TV", &s.hudToTv))
            Config::MarkDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Game Info, Input Viewer and Frame Stats go to the TV while the\n"
                              "game is on it. In off-TV play they stay on the GamePad.");
    }
    if (ImGui::Checkbox("HUD windows on the game's screen only", &s.hudOnGameScreen))
        Config::MarkDirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Game Info, Input Viewer and Frame Stats stay off a screen the\n"
                          "game is not on: the GamePad's map in TV play, the TV in off-TV\n"
                          "play. The menu still goes where Drawn Screen sends it.");
    if (ImGui::Checkbox("Startup toast", &s.showInitToast))
        Config::MarkDirty();

    ImGui::SeparatorText("Floating icon");

    if (ImGui::Checkbox("Show floating icon", &s.watermarkEnabled))
        Config::MarkDirty();
    ImGui::BeginDisabled(!s.watermarkEnabled);

    int iconPercent = (int)(s.watermarkOpacity * 100.0f + 0.5f);
    ImGui::SetNextItemWidth(kItemWidth);
    if (Field::SliderInt("Icon opacity", &iconPercent, 5, 100, "%d%%")) {
        s.watermarkOpacity = iconPercent / 100.0f;
        Config::MarkDirty();
    }
    int iconSize = (int)(s.watermarkSize + 0.5f);
    ImGui::SetNextItemWidth(kItemWidth);
    if (Field::SliderInt("Icon size", &iconSize, 32, 320, "%dpx")) {
        s.watermarkSize = (float)iconSize;
        Config::MarkDirty();
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Hotkeys");

    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_MENU), "Menu hotkey: ");
    DrawHotkeysButton();
    ImGui::SameLine();
    DrawControlsButton();

    ImGui::SeparatorText("Game");

    if (wwhd_titleId)
        ImGui::Text("Title ID: %08x%08x", (unsigned)(wwhd_titleId >> 32),
                    (unsigned)(wwhd_titleId & 0xFFFFFFFFu));
    else
        ImGui::TextDisabled("Title ID: unknown");
    ImGui::Text("Region: %s (%s)", wwhd_regionName(), wwhd_regionCode());
    ImGui::Text("Randomizer: %s", wwhd_isRandomizer() ? "loaded" : "no");

    ImGui::Separator();

    if (ImGui::MenuItem("Reset to defaults"))
        s_confirmReset = true;
}

void DrawResetConfirm()
{
    static const char* kTitle = "Reset to defaults?";

    if (s_confirmReset) {
        s_confirmReset = false;
        ImGui::OpenPopup(kTitle);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Are you sure? All settings will be reset to default.");
    ImGui::Spacing();

    if (ImGui::Button("Reset")) {
        Config::ResetToDefaults();
        Config::MarkDirty();
        Config::Flush();
        Notifications::Show(Notifications::Info, "Settings", "Reset to defaults");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}
}
