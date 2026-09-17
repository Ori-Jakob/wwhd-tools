#pragma once

#include "imgui.h"
#include "render/renderer.h"

namespace Hud {
inline void DrawText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                     ImU32 color, const char* text, bool bold)
{
    if (!bold) {
        dl->AddText(font, size, pos, color, text);
        return;
    }

    ImFont* face = Renderer::BoldFont();
    if (!face)
        face = font;

    ImFont* halo = Renderer::ShadowFont();
    if (!halo)
        halo = face;

    float w = size * 0.09f;
    if (w < 1.0f)
        w = 1.0f;

    const ImU32 outline = IM_COL32(0, 0, 0, (color >> IM_COL32_A_SHIFT) & 0xFFu);

    dl->AddText(halo, size, ImVec2(pos.x + w, pos.y + w), outline, text);
    dl->AddText(face, size, pos, color, text);
}
}
