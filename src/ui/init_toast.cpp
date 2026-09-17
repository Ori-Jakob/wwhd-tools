#include "ui/init_toast.h"

#include "core/input.h"
#include "core/hotkeys.h"
#include "core/settings.h"
#include "core/version.h"
#include "render/image.h"
#include "render/renderer.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

#include <stdint.h>

#include "icon_64_bin.h"

namespace Ui {
namespace InitToast {
static const float kTotalSeconds = 3.0f;
static const float kFadeSeconds  = 1.0f;

static const ImVec4 kGreen(64.0f / 255.0f, 207.0f / 255.0f, 142.0f / 255.0f, 1.0f);
static const ImVec4 kGold(255.0f / 255.0f, 214.0f / 255.0f, 92.0f / 255.0f, 1.0f);

static const char kName[]     = "WWHD Tools";
static const char kHintPre[]  = "Press  ";
static const char kHintPost[] = "  to open the menu.";

static float s_remaining = 0.0f;

void Arm() { s_remaining = kTotalSeconds; }

bool IsActive() { return s_remaining > 0.0f; }

void Draw(ImGuiIO& io)
{
    if (s_remaining <= 0.0f)
        return;

    const float alpha = s_remaining < kFadeSeconds ? s_remaining / kFadeSeconds : 1.0f;
    const uint32_t combo = Hotkeys::Get(Hotkeys::HOTKEY_MENU);

    const Image::Texture* logo = Image::Load(icon_64_bin, icon_64_bin_size);

    const float k = 1.0f / Renderer::UiScale();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 padding(style.WindowPadding.x * k, style.WindowPadding.y * k);
    const ImVec2 spacing(style.ItemSpacing.x * k, style.ItemSpacing.y * k);
    const float spaceW = ImGui::CalcTextSize(" ").x * k;
    const float titleW = (ImGui::CalcTextSize(kName).x +
                          ImGui::CalcTextSize(WWHD_TOOLS_VERSION).x) * k + spaceW;
    const float hintW = (ImGui::CalcTextSize(kHintPre).x +
                         ImGui::CalcTextSize(Input::DescribeCombo(combo)).x +
                         ImGui::CalcTextSize(kHintPost).x) * k;
    const float logoW = logo ? (float)logo->width * k : 0.0f;
    const float logoH = logo ? (float)logo->height * k : 0.0f;

    float contentW = titleW > hintW ? titleW : hintW;
    if (logoW > contentW)
        contentW = logoW;
    const float minW = contentW + padding.x * 2.0f;
    float windowW = io.DisplaySize.x * 0.22f;
    if (windowW < minW)
        windowW = minW;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);
    ImGui::SetNextWindowBgAlpha(Renderer::BackdropAlpha(0.75f * alpha));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.06f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(windowW, 0.0f), ImGuiCond_Always);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##wwhd_init_toast", nullptr, flags)) {
        ImGui::SetWindowFontScale(k);

        if (logo) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 (ImGui::GetContentRegionAvail().x - logoW) * 0.5f);
            ImGui::Image(logo->id, ImVec2(logoW, logoH));
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (ImGui::GetContentRegionAvail().x - titleW) * 0.5f);
        ImGui::TextColored(kGreen, "%s", kName);
        ImGui::SameLine(0.0f, spaceW);
        ImGui::TextColored(kGold, "%s", WWHD_TOOLS_VERSION);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (ImGui::GetContentRegionAvail().x - hintW) * 0.5f);
        Hotkey::DrawText(combo, kHintPre, kHintPost);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    s_remaining -= io.DeltaTime;
}
}
}
