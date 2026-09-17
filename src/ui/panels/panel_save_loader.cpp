#include "ui/panels.h"

#include "tools/save_loader.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include <string.h>

namespace Ui {
namespace Panels {
namespace Loader = Tools::SaveLoader;

static bool s_open = false;
static const char kWindowName[] = "Save Loader";

void DrawSaveLoaderItem()
{
    Window::Checkbox("Save Loader", &s_open, kWindowName);
}

void DrawSaveLoaderWindow()
{
    if (!s_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(170.0f, 130.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (ImGui::Button("Refresh"))
            Loader::RefreshList();

        ImGui::BeginChild("##files", ImVec2(0.0f, 220.0f), true);
        if (Loader::Count() == 0)
            ImGui::TextDisabled("No .sav files found.");
        for (int i = 0; i < Loader::Count(); ++i) {
            const char* path = Loader::PathAt(i);
            const bool current = strcmp(path, Loader::Opened()) == 0 ||
                                 strcmp(path, Loader::Reading()) == 0;
            ImGui::PushID(i);
            ImGui::BeginDisabled(Loader::IsBusy());
            if (ImGui::Selectable(path, current))
                Loader::Open(path);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (!Loader::Opened()[0]) {
            ImGui::TextDisabled(Loader::Reading()[0] ? "Reading ..." : "Pick a file above.");
        } else {
            ImGui::Text("%s", Loader::Opened());
            for (int slot = 0; slot < Loader::kSlots; ++slot) {
                ImGui::PushID(slot);
                ImGui::BeginDisabled(!Loader::SlotValid(slot) || Loader::IsBusy());
                if (ImGui::Button("Load"))
                    Loader::LoadSlot(slot);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted(Loader::SlotLabel(slot));
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}
}
}
