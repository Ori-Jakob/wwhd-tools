#include "ui/ui_rebind.h"

#include "ui/ui_hotkey.h"

#include "imgui.h"

namespace Ui {
namespace RebindUi {
static bool s_popupOpen[Rebind::DOMAIN_COUNT] = {};

void DrawBindingCell(Rebind::Domain domain, int index, uint32_t bound)
{
    if (!Rebind::IsCapturing(domain, index)) {
        Hotkey::Draw(bound);
        return;
    }

    const uint32_t mask = Rebind::LiveMask();
    switch (Rebind::CurrentPhase()) {
    case Rebind::PHASE_WAIT_RELEASE:
        ImGui::TextDisabled("release all buttons");
        break;
    case Rebind::PHASE_LISTENING:
        if (mask)
            Hotkey::DrawLive(mask, Rebind::LiveHeld(), "  -- release to set");
        else
            ImGui::TextDisabled("press the buttons  (%ds)", Rebind::SecondsLeft());
        break;
    case Rebind::PHASE_CONFLICT:
        Hotkey::DrawText(mask, nullptr, "  -- conflict");
        break;
    default:
        Hotkey::Draw(bound);
        break;
    }
}

void DrawRebindButton(Rebind::Domain domain, int index)
{
    const bool capturingThis = Rebind::IsCapturing(domain, index);
    ImGui::BeginDisabled(Rebind::IsActive() && !capturingThis);
    if (ImGui::Button(capturingThis ? "Cancel" : "Rebind")) {
        if (capturingThis)
            Rebind::Cancel();
        else
            Rebind::Begin(domain, index);
    }
    ImGui::EndDisabled();
}

void DrawError(Rebind::Domain domain)
{
    if (const char* error = Rebind::Error(domain))
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", error);
}

void DrawConflictPopup(Rebind::Domain domain, const char* title)
{
    const bool pending = Rebind::IsConflictPending() &&
                         Rebind::CurrentDomain() == domain;
    if (pending && !s_popupOpen[domain]) {
        ImGui::OpenPopup(title);
        s_popupOpen[domain] = true;
    }
    if (!pending)
        s_popupOpen[domain] = false;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (!pending) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    char names[192];
    Rebind::ConflictNames(names, sizeof(names));
    Hotkey::DrawText(Rebind::PendingMask(), "\"", "\" is already assigned to:");
    ImGui::TextWrapped("%s", names);
    ImGui::Spacing();
    ImGui::TextUnformatted("Overwrite? The other binding is cleared.");
    ImGui::Spacing();
    if (ImGui::Button("Overwrite", ImVec2(120, 0))) {
        Rebind::ConfirmConflict();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        Rebind::CancelConflict();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
}
}
