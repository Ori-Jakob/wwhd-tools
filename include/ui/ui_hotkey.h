#pragma once

#include <stdint.h>

namespace Ui {
namespace Hotkey {
void Draw(uint32_t buttons, bool disabled = false);
void DrawText(uint32_t buttons, const char* prefix, const char* suffix = nullptr,
              bool disabled = false);
void DrawLive(uint32_t buttons, uint32_t held, const char* suffix = nullptr);
}
}
