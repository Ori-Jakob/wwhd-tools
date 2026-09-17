#include "ui/ui_window.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <string.h>

namespace Ui {
namespace Window {
static char s_pending[96] = {};
static char s_cycleFocus[96] = {};

static void copyName(char* dst, size_t size, const char* src)
{
    if (!src)
        src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

void SetCycleFocus(const char* windowName)
{
    copyName(s_cycleFocus, sizeof(s_cycleFocus), windowName);
}

const char* CycleFocus() { return s_cycleFocus; }

void RequestFocus(const char* windowName)
{
    if (!windowName || !windowName[0])
        return;
    copyName(s_pending, sizeof(s_pending), windowName);
}

bool HasPendingFocus() { return s_pending[0] != '\0'; }

void ClearFocusRequest() { s_pending[0] = '\0'; }

void ResolveFocusRequest()
{
    if (!s_pending[0])
        return;
    ImGuiWindow* window = ImGui::FindWindowByName(s_pending);

    if (!window || !window->Active)
        return;
    ImGui::FocusWindow(window, ImGuiFocusRequestFlags_RestoreFocusedChild);
    if (window->NavLastIds[ImGuiNavLayer_Main] == 0)
        ImGui::NavInitWindow(window, true);
    SetCycleFocus(window->Name);
    s_pending[0] = '\0';
}

bool Checkbox(const char* label, bool* open, const char* windowName)
{
    if (!open)
        return false;
    const bool wasOpen = *open;
    const bool changed = ImGui::Checkbox(label, open);
    if (changed && !wasOpen && *open) {
        ImGui::CloseCurrentPopup();
        RequestFocus(windowName);
    }
    return changed;
}

bool Button(const char* label, bool* open, const char* windowName)
{
    if (!open || !ImGui::Button(label))
        return false;
    *open = true;
    ImGui::CloseCurrentPopup();
    RequestFocus(windowName);
    return true;
}
}
}
