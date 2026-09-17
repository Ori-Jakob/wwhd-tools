#include "ui/panels.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/settings.h"
#include "libwupatch/wupatch.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
void DrawDiagnostics()
{
    const Input::Snapshot& in = Input::Current();
    ImGui::Text("Input: %s", in.valid ? "reading" : "no controller");
    ImGui::Text("held 0x%04X  pressed 0x%04X", (unsigned)in.held, (unsigned)in.pressed);
    ImGui::Text("L (%.2f, %.2f)   R (%.2f, %.2f)",
                (double)in.lx, (double)in.ly, (double)in.rx, (double)in.ry);

    ImGui::Separator();

    ImGui::Text("Config: %s", Config::IsLoaded() ? "loaded" : "pending");
    if (Config::LastError()[0])
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", Config::LastError());
    ImGui::Text("Menu combo: %s",
                Input::DescribeCombo(Hotkeys::Get(Hotkeys::HOTKEY_MENU)));

    ImGui::Separator();

    const char* backend = WuPatch::BackendName();
    ImGui::Text("Patcher: %s, backend %s",
                WuPatch::IsConfigured() ? "configured" : "unconfigured",
                backend[0] ? backend : "(none yet)");
    ImGui::Text("Patches: %d  collisions: %d", WuPatch::Count(), WuPatch::CollisionCount());
    for (int i = 0; i < WuPatch::Count(); ++i)
        ImGui::Text("  %-20s %08X -> %08X %s", WuPatch::OwnerAt(i),
                    (unsigned)WuPatch::LinkAddrAt(i), (unsigned)WuPatch::RuntimeAddrAt(i),
                    WuPatch::StateName(WuPatch::StateAt(i)));
    ImGui::Text("Swaps: %d", WuPatch::Data::Count());
    for (int i = 0; i < WuPatch::Data::Count(); ++i)
        ImGui::Text("  %-20s %08X -> %08X %s", WuPatch::Data::OwnerAt(i),
                    (unsigned)WuPatch::Data::LinkAddrAt(i),
                    (unsigned)WuPatch::Data::RuntimeAddrAt(i),
                    WuPatch::StateName(WuPatch::Data::StateAt(i)));
    for (int i = 0; i < WuPatch::CollisionCount(); ++i) {
        const char* a = nullptr;
        const char* b = nullptr;
        uint32_t at = 0;
        if (WuPatch::GetCollision(i, &a, &b, &at))
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "  collision at %08X: %s vs %s",
                               (unsigned)at, a, b);
    }
    if (ImGui::Button("Log patch state"))
        WuPatch::LogState();
}
}
}
