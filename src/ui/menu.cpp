#include "ui/menu.h"

#include "core/settings.h"
#include "hud/hud_frame_stats.h"
#include "hud/hud_game_info.h"
#include "render/image.h"
#include "ui/menu_nav.h"
#include "ui/panels.h"
#include "ui/quick_access.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include "icon_32_bin.h"

namespace Ui {
namespace Menu {
static const float kLogoX = 7.0f;

void OnApplicationStart()
{
    Panels::RegisterControls();
    Panels::RegisterCheatControls();
    QuickAccess::OnApplicationStart();
}

void OnOpened()
{
    Nav::RequestMenuBarFocus();
}

static void drawToolbar(ImGuiIO& io)
{
    if (ImGui::BeginMainMenuBar()) {
        if (const Image::Texture* icon = Image::Load(icon_32_bin, icon_32_bin_size)) {
            const float side = ImGui::GetFrameHeight();
            ImGui::SetCursorPosX(kLogoX);
            ImGui::Image(icon->id, ImVec2(side, side));
        }
        if (ImGui::BeginMenu("Tools"))       { Panels::DrawTools();       ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Mods"))        { Panels::DrawMods();        ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Settings"))    { Panels::DrawSettings();    ImGui::EndMenu(); }
#ifdef WWHD_TOOLS_DEBUG
        if (ImGui::BeginMenu("Diagnostics"))  { Panels::DrawDiagnostics(); ImGui::EndMenu(); }
#endif
        ImGui::EndMainMenuBar();
    }

    Nav::ApplyMenuBarFocus(io);
}

void Draw(ImGuiIO& io)
{
    drawToolbar(io);
    Hud::GameInfo::DrawSettingsWindow();
    Hud::FrameStats::DrawSettingsWindow();
    Panels::DrawHotkeysWindow();
    Panels::DrawControlsWindow();
    Panels::DrawInventoryWindow();
    Panels::DrawSaveStatesWindow();
    Panels::DrawSaveLoaderWindow();
    QuickAccess::DrawFullWindow();
    Panels::DrawResetConfirm();
}
}
}
