#pragma once

#include "ui/window_state.h"

namespace Hud {
namespace InputViewer {
static const float MIN_WIDTH = 240.0f;
static const float MAX_WIDTH = 560.0f;

void DrawWindow(bool menuActive);

Ui::WindowState& State();
void ApplyState();

float GetOpacity();
void SetOpacity(float opacity);

bool GetIntReadout();
void SetIntReadout(bool on);

void ResetToDefaults();
}
}
