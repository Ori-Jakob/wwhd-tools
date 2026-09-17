#include "ui/panels.h"

#include "core/config.h"
#include "core/rebind.h"
#include "ui/menu_nav.h"
#include "ui/ui_rebind.h"
#include "ui/ui_window.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static bool s_open = false;

void DrawControlsButton()
{
    Window::Button("Menu Controls", &s_open, "Menu Controls");
}

static void cancelOwnCapture()
{
    if (Rebind::IsActive() && Rebind::CurrentDomain() == Rebind::DOMAIN_MENU)
        Rebind::Cancel();
}

void DrawControlsWindow()
{
    if (!s_open) {
        cancelOwnCapture();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(110.0f, 110.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Menu Controls", &s_open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextDisabled("While the menu is open. A, B, X and the D-pad are fixed.");
        ImGui::Separator();

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##navbindings", 3, flags)) {
            ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (int i = 0; i < Nav::ACTION_COUNT; ++i) {
                const Nav::Action action = (Nav::Action)i;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Nav::ActionName(action));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Nav::ActionHint(action));

                ImGui::TableNextColumn();
                RebindUi::DrawBindingCell(Rebind::DOMAIN_MENU, i, Nav::Binding(action));

                ImGui::TableNextColumn();
                ImGui::PushID(i);
                RebindUi::DrawRebindButton(Rebind::DOMAIN_MENU, i);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        RebindUi::DrawError(Rebind::DOMAIN_MENU);

        ImGui::Spacing();
        ImGui::BeginDisabled(Rebind::IsActive());
        if (ImGui::Button("Reset menu controls")) {
            Nav::ResetBindings();
            Rebind::ClearError(Rebind::DOMAIN_MENU);
            Config::MarkDirty();
        }
        ImGui::EndDisabled();

        RebindUi::DrawConflictPopup(Rebind::DOMAIN_MENU, "Menu control conflict");
    }
    ImGui::End();

    if (!s_open)
        cancelOwnCapture();
}
}
}
