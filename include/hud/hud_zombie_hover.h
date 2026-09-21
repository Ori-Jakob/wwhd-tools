#pragma once

#include "ui/window_state.h"

namespace Hud {
namespace ZombieHover {
static const float MIN_WIDTH = 130.0f;
static const float MAX_WIDTH = 420.0f;

void DrawWindow(bool menuActive);

void DrawHistoryButton();
void DrawHistoryWindow();

Ui::WindowState& State();
void ApplyState();

void ResetToDefaults();
}
}
