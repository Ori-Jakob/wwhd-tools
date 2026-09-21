#include "ui/panels.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "tools/coordinates.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include <stdio.h>
#include <string.h>

namespace Ui {
namespace Panels {
namespace Coords = Tools::Coordinates;

static bool s_open = false;
static const char kWindowName[] = "Coordinates";

void DrawCoordinatesItem()
{
    Window::Checkbox("Coordinates", &s_open, kWindowName);
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_COORD_SAVE), "  save ", nullptr, true);
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_COORD_LOAD), "  load ", nullptr, true);
}

void DrawCoordinatesWindow()
{
    if (!s_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(150.0f, 110.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted("Save Link's stage, room, position, facing and camera.");
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_COORD_SAVE), nullptr, " saves");
        ImGui::SameLine(0.0f, 0.0f);
        Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_COORD_LOAD), ", ", " loads.");
        ImGui::Separator();

        const bool busy = Coords::IsBusy();
        for (int i = 0; i < Coords::SLOT_COUNT; ++i) {
            ImGui::PushID(i);
            const Coords::Slot& s = Coords::At(i);

            int selected = Coords::Selected();
            if (ImGui::RadioButton("##sel", &selected, i)) {
                Coords::SetSelected(i);
                Config::MarkDirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The slot the hotkeys use.");
            ImGui::SameLine();
            ImGui::Text("%2d", i + 1);
            ImGui::SameLine();

            char name[Coords::NAME_MAX];
            memcpy(name, s.name, sizeof(name));
            ImGui::SetNextItemWidth(150.0f);
            if (Field::TextWithHint("##name", "name", name, sizeof(name))) {
                Coords::Slot copy = s;
                memcpy(copy.name, name, sizeof(copy.name));
                Coords::Set(i, copy);
                Config::MarkDirty();
            }
            ImGui::SameLine();

            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Save"))
                Coords::SaveSlot(i);
            ImGui::SameLine();
            ImGui::BeginDisabled(!s.valid);
            if (ImGui::Button("Load"))
                Coords::LoadSlot(i);
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
                Coords::ClearSlot(i);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::SameLine();

            char where[96];
            Coords::Describe(i, where, sizeof(where));
            if (s.valid) {
                ImGui::TextUnformatted(where);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n(%.1f, %.1f, %.1f) facing %d%s", where,
                                      (double)s.pos.x, (double)s.pos.y, (double)s.pos.z,
                                      (int)(uint16_t)s.angle,
                                      s.hasCamera ? "\ncamera saved" : "");
            } else {
                ImGui::TextDisabled("%s", where);
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        const char* status = Coords::Status();
        if (busy)
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", status);
        else if (status[0])
            ImGui::TextDisabled("%s", status);
    }
    ImGui::End();
}
}
}
