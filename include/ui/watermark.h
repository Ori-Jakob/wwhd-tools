#pragma once

namespace Ui {
namespace Watermark {
bool Tick(bool acceptTouch);
bool IsInteracting();
bool HitTest(float x, float y);
void Draw();
}
}
