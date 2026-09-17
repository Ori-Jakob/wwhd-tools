#include "ui/panels.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/rebind.h"
#include "tools/save_states.h"
#include "ui/ui_rebind.h"
#include "ui/ui_window.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static bool s_open = false;

void DrawHotkeysButton()
{
    Window::Button("Rebind Hotkeys", &s_open, "Rebind Hotkeys");
}

static void cancelOwnCapture()
{
    if (Rebind::IsActive() && Rebind::CurrentDomain() == Rebind::DOMAIN_HOTKEYS)
        Rebind::Cancel();
}

void DrawHotkeysWindow()
{
    if (!s_open) {
        cancelOwnCapture();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(90.0f, 90.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Rebind Hotkeys", &s_open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##hotkeys", 3, flags)) {
            ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Hotkey",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (int i = 0; i < Hotkeys::HOTKEY_COUNT; ++i) {
                const Hotkeys::Id id = (Hotkeys::Id)i;
                if (Tools::SaveStates::kHidden &&
                    (id == Hotkeys::HOTKEY_SAVE_STATE || id == Hotkeys::HOTKEY_LOAD_STATE))
                    continue;
                if (const char* group = Hotkeys::Group(id)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", group);
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Hotkeys::Name(id));
                if (Hotkeys::IsHold(id)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(hold)");
                }

                ImGui::TableNextColumn();
                RebindUi::DrawBindingCell(Rebind::DOMAIN_HOTKEYS, i, Hotkeys::Get(id));

                ImGui::TableNextColumn();
                ImGui::PushID(i);
                RebindUi::DrawRebindButton(Rebind::DOMAIN_HOTKEYS, i);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        RebindUi::DrawError(Rebind::DOMAIN_HOTKEYS);

        ImGui::Spacing();
        ImGui::BeginDisabled(Rebind::IsActive());
        if (ImGui::Button("Reset all hotkeys")) {
            Hotkeys::ResetToDefaults();
            Rebind::ClearError(Rebind::DOMAIN_HOTKEYS);
            Config::MarkDirty();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Hold hotkeys are modifiers and may share a button.");

        RebindUi::DrawConflictPopup(Rebind::DOMAIN_HOTKEYS, "Hotkey conflict");
    }
    ImGui::End();

    if (!s_open)
        cancelOwnCapture();
}
}
}
