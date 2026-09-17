#pragma once

struct ImGuiIO;

namespace Ui {
namespace InitToast {
void Arm();

void Draw(ImGuiIO& io);

bool IsActive();
}
}
