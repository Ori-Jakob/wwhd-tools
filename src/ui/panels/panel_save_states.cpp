#include "ui/panels.h"

#include "core/hotkeys.h"
#include "tools/save_states.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include <string.h>

namespace Ui {
namespace Panels {
namespace States = Tools::SaveStates;

static bool s_open = false;
static const char kWindowName[] = "Save States";
static char s_name[States::kNameMax] = "";

void DrawSaveStatesItem()
{
    if (States::kHidden)
        return;
    Window::Checkbox("Save States", &s_open, kWindowName);
}

static void drawRow(int index)
{
    char name[States::kNameMax];
    strncpy(name, States::NameAt(index), sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    ImGui::PushID(index);
    ImGui::BeginDisabled(States::IsBusy());
    if (ImGui::Button("Load"))
        States::Load(name);
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
        ImGui::OpenPopup("Delete state?");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Selectable(name, strcmp(name, States::Selected()) == 0))
        States::SetSelected(name);

    if (ImGui::BeginPopupModal("Delete state?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"?", name);
        ImGui::Spacing();
        if (ImGui::Button("Delete")) {
            States::Delete(name);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void DrawSaveStatesWindow()
{
    if (States::kHidden || !s_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(540.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(140.0f, 110.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_SAVE_STATE),
                         "Save: ", nullptr, true);
        ImGui::SameLine();
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_LOAD_STATE),
                         "   Load selected: ", nullptr, true);

        ImGui::SetNextItemWidth(240.0f);
        Field::TextWithHint("##name", "name (optional)", s_name, sizeof(s_name));
        ImGui::SameLine();
        ImGui::BeginDisabled(States::IsBusy());
        if (ImGui::Button("Save") && States::Save(s_name))
            s_name[0] = '\0';
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            States::RefreshList();

        if (States::Status()[0])
            ImGui::TextWrapped("%s", States::Status());
        ImGui::Separator();

        ImGui::BeginChild("##states", ImVec2(0.0f, 0.0f), true);
        if (States::Count() == 0)
            ImGui::TextDisabled("No states yet.");
        for (int i = 0; i < States::Count(); ++i)
            drawRow(i);
        ImGui::EndChild();
    }
    ImGui::End();
}
}
}
