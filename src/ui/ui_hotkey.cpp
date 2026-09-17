#include "ui/ui_hotkey.h"

#include "core/input.h"

#include "imgui.h"

namespace Ui {
namespace Hotkey {
static const ImVec4 kButtonGreen(64.0f / 255.0f, 207.0f / 255.0f,
                                 142.0f / 255.0f, 1.0f);
static const ImVec4 kButtonGreenDim(64.0f / 255.0f, 207.0f / 255.0f,
                                    142.0f / 255.0f, 0.5f);

static void drawFragment(const char* text, const ImVec4& color, bool& first)
{
    if (!text || !text[0])
        return;
    if (!first)
        ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    first = false;
}

static void drawButtons(uint32_t buttons, uint32_t bright, const ImVec4& baseColor,
                        bool& first)
{
    int labelCount = 0;
    const Input::ButtonLabel* labels = Input::GetButtonLabels(&labelCount);
    bool drewButton = false;
    for (int i = 0; i < labelCount; ++i) {
        if (!(buttons & labels[i].bit))
            continue;
        if (drewButton)
            drawFragment("+", baseColor, first);
        const bool lit = (bright & labels[i].bit) != 0;
        drawFragment(labels[i].name, lit ? kButtonGreen : kButtonGreenDim, first);
        drewButton = true;
    }
    if (!drewButton)
        drawFragment("(unbound)", baseColor, first);
}

void DrawText(uint32_t buttons, const char* prefix, const char* suffix, bool disabled)
{
    const ImVec4 baseColor = ImGui::GetStyleColorVec4(
        disabled ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    bool first = true;
    drawFragment(prefix, baseColor, first);
    drawButtons(buttons, buttons, baseColor, first);
    drawFragment(suffix, baseColor, first);
}

void DrawLive(uint32_t buttons, uint32_t held, const char* suffix)
{
    const ImVec4 baseColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    bool first = true;
    drawButtons(buttons, held, baseColor, first);
    drawFragment(suffix, baseColor, first);
}

void Draw(uint32_t buttons, bool disabled)
{
    DrawText(buttons, nullptr, nullptr, disabled);
}
}
}
