#pragma once

struct ImGuiIO;

namespace Ui {
namespace Menu {
void Draw(ImGuiIO& io);
void OnOpened();

void OnApplicationStart();
}
}
